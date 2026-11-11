using System.Drawing;
using System.Numerics;
using System.Runtime.InteropServices;
using AetherUI.Css;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using SharpGen.Runtime;
using Vortice;
using Vortice.Direct2D1;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DirectWrite;
using Vortice.DXGI;
using Vortice.Mathematics;
using DWriteFontWeight = Vortice.DirectWrite.FontWeight;
using DWriteTextAlignment = Vortice.DirectWrite.TextAlignment;
using UiFontWeight = AetherUI.Css.FontWeight;
using UiTextAlign = AetherUI.Css.TextAlign;
using UiTextMetrics = AetherUI.Layout.TextMetrics;

namespace AetherUI.Windows.Graphics;

/// <summary>
/// Direct2D renderer region: Retained-frame cache and the incremental render pipeline.
/// </summary>
public sealed partial class Direct2DRenderBackend
{
    private ID2D1Bitmap1? _retainedFrameBitmap;
    private DisplayList? _retainedFrameDisplayList;

    private int _lastIncrementalStartIndex = -1;
    private int _lastIncrementalScannedCommands;
    private int _lastIncrementalPaintedCommands;

    internal int LastIncrementalStartIndex => _lastIncrementalStartIndex;
    internal int LastIncrementalScannedCommands => _lastIncrementalScannedCommands;
    internal int LastIncrementalPaintedCommands => _lastIncrementalPaintedCommands;

    public bool TryRenderIncremental(DisplayList displayList, ColorRgba clearColor)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ArgumentNullException.ThrowIfNull(displayList);
        if (clearColor.A < .999f)
        {
            return false;
        }

        if (_retainedFrameBitmap is null || _retainedFrameDisplayList is null)
        {
            return false;
        }

        if (!TryCreateDamagePlan(_retainedFrameDisplayList, displayList, out var plan))
        {
            if (_incrementalDebug)
            {
                Console.WriteLine("TryCreateDamagePlan rejected; fallback required.");
            }

            return false;
        }

        // Only an armed, no-damage capture needs the caller's normal full-render fallback.
        if (_diagnosticCaptureRequest is not null && plan.DirtyRects.Count == 0)
        {
            CheckDiagnosticRenderThread();
            return false;
        }

        if (plan.DirtyRects.Count == 0)
        {
            if (_incrementalDebug)
            {
                Console.WriteLine($"TryCreateDamagePlan no changes: first={plan.FirstChangedIndex}.");
            }

            _retainedFrameDisplayList = displayList;
            return true;
        }

        _textShadowMetrics.Reset();
        _backdropMetrics.Reset();
        _scrollTileMetrics.Reset();
        _lastTransformPushes = 0;
        _lastTransformPops = 0;
        _lastIncrementalScannedCommands = 0;
        _lastIncrementalPaintedCommands = 0;
        CheckDiagnosticRenderThread();
        try
        {
            RenderIncrementalCore(displayList, clearColor, plan, allowDeviceRetry: true);
            _lastIncrementalStartIndex = plan.FirstChangedIndex;
            CompleteDiagnosticRender();
        }
        catch
        {
            InvalidateDiagnosticCaptureResult();
            throw;
        }
        return true;
    }

    private void RenderIncrementalCore(DisplayList displayList, ColorRgba clearColor, DamagePlan plan, bool allowDeviceRetry)
    {
        EnsureDeviceResources();
        if (_retainedFrameBitmap is null)
        {
            RenderCore(displayList, clearColor, allowDeviceRetry);
            return;
        }

        var target = _renderTarget!;
        ConfigureSceneTarget(usesBackdropFilters: false);
        BeginDrawSegment(target);
        target.Transform = Matrix3x2.Identity;
        target.Clear(ToColor4(clearColor));
        DrawRetainedFrame(target);
        for (var i = 0; i < plan.DirtyRects.Count; i++)
        {
            var dirtyRect = plan.DirtyRects[i];
            target.PushAxisAlignedClip(ToRawRect(dirtyRect), AntialiasMode.PerPrimitive);
            try
            {
                ClearDirtyRect(target, clearColor, dirtyRect);
                RenderSimpleCommandRange(
                    target,
                    displayList.Commands,
                    plan.ChangedRanges,
                    plan.StateCommandIndices,
                    [dirtyRect],
                    out var scannedCommands,
                    out var paintedCommands);

                _lastIncrementalScannedCommands += scannedCommands;
                _lastIncrementalPaintedCommands += paintedCommands;
            }
            finally
            {
                target.PopAxisAlignedClip();
            }
        }

        var result = EndDrawSegment(target);
        if (result.Failure)
        {
            if (RecoverFromDeviceLoss(result, allowDeviceRetry))
            {
                RenderCore(displayList, clearColor, allowDeviceRetry: false);
                return;
            }

            result.CheckError();
        }

        CopyDiagnosticCandidate();

        UpdateRetainedFrameCache(target, displayList, plan.DirtyRects);

        var presentResult = _swapChain!.Present(1, PresentFlags.None);
        if (presentResult.Failure)
        {
            if (RecoverFromDeviceLoss(presentResult, allowDeviceRetry))
            {
                RenderCore(displayList, clearColor, allowDeviceRetry: false);
                return;
            }

            presentResult.CheckError();
        }
    }

    private void DrawRetainedFrame(ID2D1DeviceContext target)
    {
        if (_retainedFrameBitmap is null)
        {
            return;
        }

        target.DrawImage(_retainedFrameBitmap, Vector2.Zero, null,
            Vortice.Direct2D1.InterpolationMode.NearestNeighbor, CompositeMode.SourceCopy);
    }

    private void UpdateRetainedFrameCache(ID2D1DeviceContext target, DisplayList displayList, IReadOnlyList<RectF>? dirtyRects = null)
    {
        if (_targetBitmap is null || _targetPixelWidth <= 0 || _targetPixelHeight <= 0 || _targetDpiScale <= 0)
        {
            ReleaseRetainedFrameCache();
            return;
        }

        if (_retainedFrameBitmap is null)
        {
            var dpi = 96f * _targetDpiScale;
            _retainedFrameBitmap = target.CreateBitmap(
                new SizeI(_targetPixelWidth, _targetPixelHeight),
                IntPtr.Zero,
                0,
                new BitmapProperties1(
                    new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
                    dpi,
                    dpi,
                    BitmapOptions.None));
        }

        if (dirtyRects is { Count: > 0 })
        {
            foreach (var dirtyRect in dirtyRects)
            {
                var pixelRect = ToPixelRectangle(dirtyRect);
                if (pixelRect.Width <= 0 || pixelRect.Height <= 0)
                {
                    continue;
                }

                _retainedFrameBitmap.CopyFromBitmap(
                    new Point(pixelRect.Left, pixelRect.Top),
                    _targetBitmap,
                    pixelRect);
            }
        }
        else
        {
            _retainedFrameBitmap.CopyFromBitmap(_targetBitmap);
        }

        _retainedFrameDisplayList = displayList;
    }

    private void ReleaseRetainedFrameCache()
    {
        _retainedFrameBitmap?.Dispose();
        _retainedFrameBitmap = null;
        _retainedFrameDisplayList = null;
    }

    private void RenderSimpleCommandRange(
        ID2D1DeviceContext target,
        IReadOnlyList<PaintCommand> commands,
        IReadOnlyList<CommandRange> changedRanges,
        IReadOnlyList<int> stateCommandIndices,
        IReadOnlyList<RectF> dirtyRects,
        out int scannedCommands,
        out int paintedCommands)
    {
        scannedCommands = 0;
        paintedCommands = 0;
        var clipStack = new Stack<ClipScope>();
        var scrollStack = new Stack<Matrix3x2>();
        var scrollOffsetStack = new Stack<ScrollVisualOffset>();
        var currentScrollOffset = new ScrollVisualOffset(0, 0);

        var selectedCommands = new List<int>(0);
        foreach (var range in changedRanges)
        {
            for (var i = range.Start; i < range.End; ++i)
            {
                selectedCommands.Add(i);
            }
        }

        foreach (var stateIndex in stateCommandIndices)
        {
            selectedCommands.Add(stateIndex);
        }

        if (selectedCommands.Count == 0)
        {
            return;
        }

        selectedCommands.Sort();
        var deduped = new List<int>(selectedCommands.Count);
        var previous = -1;
        for (var i = 0; i < selectedCommands.Count; i++)
        {
            if (i == 0 || selectedCommands[i] != previous)
            {
                deduped.Add(selectedCommands[i]);
            }

            previous = selectedCommands[i];
        }

        var rangeIndex = 0;
        foreach (var i in deduped)
        {
            while (rangeIndex < changedRanges.Count && i >= changedRanges[rangeIndex].End)
            {
                rangeIndex++;
            }

            var shouldPaint = rangeIndex < changedRanges.Count &&
                i >= changedRanges[rangeIndex].Start;
            var command = commands[i];
            if (_incrementalDebug)
            {
                Console.WriteLine($"[{changedRanges.Count}] {i} shouldPaint={shouldPaint} type={command.GetType().Name} scroll=({currentScrollOffset.X},{currentScrollOffset.Y})");
            }

            scannedCommands++;
            switch (command)
            {
                case ApplyBackdropFilterCommand:
                case PushTransformCommand:
                case PopTransformCommand:
                    if (shouldPaint)
                    {
                        throw new InvalidOperationException($"Unsupported incremental command in paint range: {command.GetType().Name}.");
                    }

                    break;
                case FillRoundedRectangleCommand fill:
                    if (_incrementalDebug && shouldPaint)
                    {
                        Console.WriteLine($"  fill rect={fill.Rectangle} color={fill.Color.R},{fill.Color.G},{fill.Color.B}");
                    }

                    if (shouldPaint && CommandIntersectsDirtyRects(fill.Rectangle, dirtyRects, currentScrollOffset))
                    {
                        FillRoundedGeometry(target, fill.Rectangle, fill.Radii, GetBrush(fill.Color));
                        paintedCommands++;
                    }

                    break;
                case FillRoundedRectangleGradientCommand gradientFill:
                    if (shouldPaint &&
                        CommandIntersectsDirtyRects(gradientFill.Rectangle, dirtyRects, currentScrollOffset))
                    {
                        DrawGradientFill(target, gradientFill);
                        paintedCommands++;
                    }

                    break;
                case DrawBoxShadowCommand shadow:
                    if (shouldPaint &&
                        TryGetCommandBounds(shadow, out var shadowBounds) &&
                        CommandIntersectsDirtyRects(shadowBounds, dirtyRects, currentScrollOffset))
                    {
                        DrawBoxShadow(target, shadow);
                        paintedCommands++;
                    }

                    break;
                case StrokeRoundedRectangleCommand stroke:
                    if (shouldPaint &&
                        TryGetCommandBounds(stroke, out var strokeBounds) &&
                        CommandIntersectsDirtyRects(strokeBounds, dirtyRects, currentScrollOffset))
                    {
                        StrokeRoundedGeometry(target, stroke.Rectangle, stroke.Radii, GetBrush(stroke.Color), MathF.Max(.25f, stroke.StrokeWidth));
                        paintedCommands++;
                    }

                    break;
                case FillBorderSideCommand borderSide:
                    if (shouldPaint &&
                        CommandIntersectsDirtyRects(borderSide.OuterRectangle, dirtyRects, currentScrollOffset))
                    {
                        Direct2DBorderSidePainter.Draw(_d2dFactory, target, borderSide, GetBrush);
                        paintedCommands++;
                    }

                    break;
                case DrawTextCommand text:
                    if (shouldPaint &&
                        CommandIntersectsDirtyRects(text.Rectangle, dirtyRects, currentScrollOffset))
                    {
                        DrawText(target, text);
                        paintedCommands++;
                    }

                    break;
                case DrawTextGradientCommand textGradient:
                    if (shouldPaint &&
                        CommandIntersectsDirtyRects(textGradient.Rectangle, dirtyRects, currentScrollOffset))
                    {
                        DrawTextGradient(target, textGradient);
                        paintedCommands++;
                    }

                    break;
                case DrawBackgroundImageCommand image:
                    if (shouldPaint &&
                        TryGetCommandBounds(image, out var imageBounds) &&
                        CommandIntersectsDirtyRects(imageBounds, dirtyRects, currentScrollOffset))
                    {
                        DrawBackgroundImage(target, image);
                        paintedCommands++;
                    }

                    break;
                case PushClipCommand push:
                    PushClip(target, push, clipStack, transformDepth: 0);
                    break;
                case PopClipCommand:
                    if (clipStack.Count == 0)
                    {
                        throw new InvalidOperationException("An incremental render popped a clip that was not active.");
                    }

                    PopClip(target, clipStack.Pop());
                    break;
                case PushScrollOffsetCommand scroll:
                    scrollStack.Push(target.Transform);
                    scrollOffsetStack.Push(currentScrollOffset);
                    var offset = _scrollStateProvider?.Invoke(scroll.ScrollContainerId).Offset ?? default;
                    currentScrollOffset = new ScrollVisualOffset(
                        currentScrollOffset.X + offset.X,
                        currentScrollOffset.Y + offset.Y);
                    target.Transform = Matrix3x2.CreateTranslation(-offset.X, -offset.Y) * target.Transform;
                    break;
                case PopScrollOffsetCommand:
                    if (scrollStack.Count == 0)
                    {
                        throw new InvalidOperationException("An incremental render popped a scroll offset that was not active.");
                    }

                    target.Transform = scrollStack.Pop();
                    currentScrollOffset = scrollOffsetStack.Pop();
                    break;
                case DrawScrollbarChromeCommand chrome:
                    if (shouldPaint)
                    {
                        paintedCommands++;
                        DrawScrollbarChrome(target, _scrollStateProvider?.Invoke(chrome.ScrollContainerId).Chrome ?? default);
                    }

                    break;
                default:
                    throw new InvalidOperationException($"The command {command.GetType().Name} cannot be rendered incrementally.");
            }
        }

        if (clipStack.Count != 0)
        {
            throw new InvalidOperationException("An incremental render ended with an active clip.");
        }

        if (scrollStack.Count != 0)
        {
            throw new InvalidOperationException("An incremental render ended with an active scroll offset.");
        }
    }

    private void ClearDirtyRect(ID2D1RenderTarget target, ColorRgba clearColor, RectF dirtyRect)
    {
        target.FillRectangle(ToRawRect(dirtyRect), GetBrush(clearColor));
    }

    private Rectangle ToPixelRectangle(RectF rectangle)
    {
        var left = Math.Clamp((int)MathF.Floor(rectangle.Left * _targetDpiScale), 0, _targetPixelWidth);
        var top = Math.Clamp((int)MathF.Floor(rectangle.Top * _targetDpiScale), 0, _targetPixelHeight);
        var right = Math.Clamp((int)MathF.Ceiling(rectangle.Right * _targetDpiScale), 0, _targetPixelWidth);
        var bottom = Math.Clamp((int)MathF.Ceiling(rectangle.Bottom * _targetDpiScale), 0, _targetPixelHeight);
        return Rectangle.FromLTRB(left, top, right, bottom);
    }

}
