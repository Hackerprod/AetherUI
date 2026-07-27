using System.Drawing;
using System.Numerics;
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
/// Direct2D/DirectWrite implementation of AetherUI's retained display-list backend.
/// A single D3D11/DXGI/Direct2D device-context pipeline owns every GPU resource.
/// </summary>
public sealed class Direct2DRenderBackend : IRenderBackend, ITextMeasurer
{
    private readonly nint _windowHandle;
    private readonly ID2D1Factory1 _d2dFactory;
    private readonly IDWriteFactory2 _writeFactory;
    private readonly Dictionary<ColorRgba, ID2D1SolidColorBrush> _brushes = new();
    private readonly Dictionary<TextFormatKey, IDWriteTextFormat> _textFormats = new();
    private readonly TextShadowMaskCache _textShadowMasks = new();
    private readonly TextShadowRenderMetrics _textShadowMetrics = new();
    private readonly BackdropCompositionMetrics _backdropMetrics = new();
    // CPU pixels live in ImageResourceResolver; this cache holds only device-bound bitmaps and is
    // deliberately cleared whenever the Direct2D device is recreated.
    private readonly Dictionary<AetherUI.Runtime.ImageResourceResult, ID2D1Bitmap1> _imageBitmaps =
        new(ReferenceEqualityComparer.Instance);
    private ID3D11Device? _d3dDevice;
    private ID3D11DeviceContext? _d3dContext;
    private IDXGISwapChain1? _swapChain;
    private ID2D1Device? _d2dDevice;
    private ID2D1DeviceContext? _renderTarget;
    private ID2D1Bitmap1? _targetBitmap;
    // Allocated only for a frame that actually contains a backdrop operation. The scene bitmap
    // remains drawable and the snapshot remains sampleable; the DXGI back buffer cannot serve
    // both roles because it is intentionally created with CannotDraw.
    private ID2D1Bitmap1? _backdropSceneBitmap;
    private ID2D1Bitmap1? _backdropSnapshotBitmap;
    private int _pixelWidth;
    private int _pixelHeight;
    private float _dpiScale;
    // Physical properties of the currently bound DXGI surface / D2D target. They intentionally
    // differ from the requested values above while a real resize is in flight.
    private int _targetPixelWidth;
    private int _targetPixelHeight;
    private float _targetDpiScale;
    // This is the sole BeginDraw/EndDraw state for both the scene and text-shadow mask passes.
    // Direct2D exposes no query for it; every segment transition must use the helpers below.
    private bool _isDrawSegmentActive;
    private bool _disposed;

    internal int LastTextShadowCacheHits => _textShadowMetrics.CacheHits;
    internal int LastTextShadowSceneSuspensions => _textShadowMetrics.SceneSuspensions;
    internal int LastBackdropAllocations => _backdropMetrics.Allocations;
    internal int LastBackdropCompositionPasses => _backdropMetrics.CompositionPasses;
    internal int LastBackdropSnapshotPasses => _backdropMetrics.SnapshotPasses;

    public Direct2DRenderBackend(nint windowHandle, int pixelWidth, int pixelHeight, float dpiScale)
    {
        if (windowHandle == nint.Zero)
        {
            throw new ArgumentException("Se requiere un HWND válido.", nameof(windowHandle));
        }

        _windowHandle = windowHandle;
        _pixelWidth = Math.Max(1, pixelWidth);
        _pixelHeight = Math.Max(1, pixelHeight);
        _dpiScale = NormalizeDpiScale(dpiScale);

        var d2dFactory = D2D1.D2D1CreateFactory<ID2D1Factory1>();
        IDWriteFactory2? writeFactory = null;
        try
        {
            writeFactory = DWrite.DWriteCreateFactory<IDWriteFactory2>();
            _d2dFactory = d2dFactory;
            _writeFactory = writeFactory;
            CreateDeviceResources();
        }
        catch
        {
            ReleaseDeviceResources();
            writeFactory?.Dispose();
            d2dFactory.Dispose();
            throw;
        }
    }

    public UiTextMetrics MeasureText(string text, ComputedStyle style, float maxWidth = float.PositiveInfinity)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ArgumentNullException.ThrowIfNull(text);
        ArgumentNullException.ThrowIfNull(style);

        if (text.Length == 0)
        {
            return new UiTextMetrics(0, style.FontSize * style.LineHeight, style.FontSize * .8f);
        }

        var width = float.IsFinite(maxWidth) && maxWidth > 0 ? maxWidth : 1_000_000f;
        var height = MathF.Max(style.FontSize * style.LineHeight * 4, 256f);
        var format = GetTextFormat(style.FontFamily, style.FontSize, style.FontWeight, UiTextAlign.Start, wrap: false, verticalCenter: false);
        using var layout = _writeFactory.CreateTextLayout(text, format, width, height);
        var metrics = layout.Metrics;
        var lines = new LineMetrics[1];
        layout.GetLineMetrics(lines, out var lineCount);
        var measuredWidth = MathF.Max(0, metrics.WidthIncludingTrailingWhitespace);
        var measuredHeight = MathF.Max(style.FontSize * style.LineHeight, metrics.Height);
        // The native layout engine positions mixed-size inline runs from this baseline. It must
        // come from the same DirectWrite layout that DrawText uses; a font-size multiplier only
        // happens to work for some families and leaves units/superscripts visibly displaced.
        var baseline = lineCount > 0 && float.IsFinite(lines[0].Baseline)
            ? lines[0].Baseline
            : style.FontSize * .8f;
        return new UiTextMetrics(measuredWidth, measuredHeight, baseline);
    }

    public void Resize(int pixelWidth, int pixelHeight, float dpiScale)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (pixelWidth <= 0 || pixelHeight <= 0)
        {
            return;
        }

        _pixelWidth = pixelWidth;
        _pixelHeight = pixelHeight;
        _dpiScale = NormalizeDpiScale(dpiScale);
        EnsureDeviceResources();

        var sizeChanged = _pixelWidth != _targetPixelWidth || _pixelHeight != _targetPixelHeight;
        var dpiChanged = _dpiScale != _targetDpiScale;
        if (!sizeChanged && !dpiChanged)
        {
            return;
        }

        _textShadowMasks.Clear();
        ReleaseBackdropCompositionResources();
        if (sizeChanged)
        {
            ResizeSwapChain();
        }
        else
        {
            RecreateTargetForDpiChange();
        }
    }

    public void Render(DisplayList displayList, ColorRgba clearColor)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ArgumentNullException.ThrowIfNull(displayList);
        _textShadowMetrics.Reset();
        _backdropMetrics.Reset();
        RenderCore(displayList, clearColor, allowDeviceRetry: true);
    }

    private void RenderCore(DisplayList displayList, ColorRgba clearColor, bool allowDeviceRetry)
    {
        EnsureDeviceResources();

        var usesBackdropFilters = RequiresBackdropComposition(displayList);
        ConfigureSceneTarget(usesBackdropFilters);
        var target = _renderTarget!;
        BeginDrawSegment(target);
        target.Transform = Matrix3x2.Identity;
        target.Clear(ToColor4(clearColor));

        var clipStack = new Stack<PushClipCommand>();
        foreach (var command in displayList.Commands)
        {
            switch (command)
            {
                case FillRoundedRectangleCommand fill:
                    FillRoundedGeometry(target, fill.Rectangle, fill.Radii, GetBrush(fill.Color));
                    break;
                case FillRoundedRectangleGradientCommand gradientFill:
                    DrawGradientFill(target, gradientFill);
                    break;
                case DrawBoxShadowCommand shadow:
                    DrawBoxShadow(target, shadow);
                    break;
                case StrokeRoundedRectangleCommand stroke:
                    StrokeRoundedGeometry(target, stroke.Rectangle, stroke.Radii, GetBrush(stroke.Color), MathF.Max(.25f, stroke.StrokeWidth));
                    break;
                case FillBorderSideCommand borderSide:
                    DrawBorderSide(target, borderSide);
                    break;
                case DrawTextCommand text:
                    DrawText(target, text);
                    break;
                case DrawTextGradientCommand textGradient:
                    DrawTextGradient(target, textGradient);
                    break;
                case DrawTextShadowCommand textShadow:
                {
                    var mask = Direct2DTextShadowPainter.GlyphMask.Empty;
                    var disposeMaskAfterDraw = false;
                    try
                    {
                        var key = TextShadowMaskKey.From(textShadow, _dpiScale);
                        if (_textShadowMasks.TryGet(key, out mask))
                        {
                            // Cache hits draw the effect directly into the active scene. They do
                            // not interrupt BeginDraw or touch the active clip stack.
                            _textShadowMetrics.RecordCacheHit();
                        }
                        else
                        {
                            _textShadowMetrics.RecordSceneSuspension();
                            SuspendClips(target, clipStack);
                            var boundaryResult = EndDrawSegment(target);
                            if (boundaryResult.Failure)
                            {
                                if (RecoverFromDeviceLoss(boundaryResult, allowDeviceRetry))
                                {
                                    RenderCore(displayList, clearColor, allowDeviceRetry: false);
                                    return;
                                }

                                boundaryResult.CheckError();
                            }

                            mask = CreateTextShadowMask(target, textShadow);
                            disposeMaskAfterDraw = !_textShadowMasks.TryAdd(key, mask);
                            BeginDrawSegment(target);
                            RestoreClips(target, clipStack);
                        }

                        Direct2DTextShadowPainter.Draw(target, mask, textShadow.Rectangle, textShadow.Shadow);
                    }
                    catch (SharpGenException exception) when (IsDeviceLoss(exception.ResultCode) && allowDeviceRetry)
                    {
                        ReleaseDeviceResources();
                        CreateDeviceResources();
                        RenderCore(displayList, clearColor, allowDeviceRetry: false);
                        return;
                    }
                    finally
                    {
                        if (disposeMaskAfterDraw)
                        {
                            mask?.Dispose();
                        }
                    }

                    break;
                }
                case ApplyBackdropFilterCommand backdropFilter:
                    try
                    {
                        ApplyBackdropFilter(target, backdropFilter, clipStack);
                    }
                    catch (SharpGenException exception) when (IsDeviceLoss(exception.ResultCode) && allowDeviceRetry)
                    {
                        ReleaseDeviceResources();
                        CreateDeviceResources();
                        RenderCore(displayList, clearColor, allowDeviceRetry: false);
                        return;
                    }
                    break;
                case DrawBackgroundImageCommand image:
                    DrawBackgroundImage(target, image);
                    break;
                case PushClipCommand push:
                    PushClip(target, push, clipStack);
                    break;
                case PopClipCommand:
                    if (clipStack.Count > 0)
                    {
                        PopClip(target, clipStack.Pop());
                    }
                    break;
            }
        }

        while (clipStack.Count > 0)
        {
            PopClip(target, clipStack.Pop());
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

        if (usesBackdropFilters)
        {
            try
            {
                CompositeBackdropScene(target, clearColor);
            }
            catch (SharpGenException exception) when (IsDeviceLoss(exception.ResultCode) && allowDeviceRetry)
            {
                ReleaseDeviceResources();
                CreateDeviceResources();
                RenderCore(displayList, clearColor, allowDeviceRetry: false);
                return;
            }
        }

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

    private void ConfigureSceneTarget(bool usesBackdropFilters)
    {
        var target = _renderTarget ?? throw new InvalidOperationException("The Direct2D render target is not initialized.");
        if (!usesBackdropFilters)
        {
            // A page can stop using backdrop-filter after a markup/state update. Release both
            // intermediate surfaces before the next direct frame so they never become a global
            // rendering tax for ordinary applications.
            ReleaseBackdropCompositionResources();
            target.Target = _targetBitmap;
            return;
        }

        EnsureBackdropCompositionResources();
        target.Target = _backdropSceneBitmap;
    }

    internal static bool RequiresBackdropComposition(DisplayList displayList) =>
        displayList.Commands.Any(static command => command is ApplyBackdropFilterCommand);

    private void EnsureBackdropCompositionResources()
    {
        if (_backdropSceneBitmap is not null && _backdropSnapshotBitmap is not null)
        {
            return;
        }

        ReleaseBackdropCompositionResources();
        var target = _renderTarget ?? throw new InvalidOperationException("The Direct2D render target is not initialized.");
        var dpi = 96f * _dpiScale;
        var properties = new BitmapProperties1(
            new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
            dpi,
            dpi,
            BitmapOptions.Target);
        var size = new SizeI(Math.Max(1, _pixelWidth), Math.Max(1, _pixelHeight));
        _backdropSceneBitmap = target.CreateBitmap(size, IntPtr.Zero, 0, properties);
        _backdropMetrics.RecordAllocation();
        try
        {
            // The snapshot never becomes a render target. It is a stable source for an effect
            // while the scene bitmap receives the filtered result.
            var snapshotProperties = new BitmapProperties1(
                new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
                dpi,
                dpi,
                BitmapOptions.None);
            _backdropSnapshotBitmap = target.CreateBitmap(size, IntPtr.Zero, 0, snapshotProperties);
            _backdropMetrics.RecordAllocation();
        }
        catch
        {
            _backdropSceneBitmap.Dispose();
            _backdropSceneBitmap = null;
            throw;
        }
    }

    private void ReleaseBackdropCompositionResources()
    {
        // Detach before releasing: a device context retains its target independently from the
        // managed wrapper, and a stale target would make resize/device-loss recovery depend on
        // COM reference timing.
        if (_renderTarget is not null)
        {
            _renderTarget.Target = null;
        }
        _backdropSnapshotBitmap?.Dispose();
        _backdropSnapshotBitmap = null;
        _backdropSceneBitmap?.Dispose();
        _backdropSceneBitmap = null;
    }

    private void ApplyBackdropFilter(
        ID2D1DeviceContext target,
        ApplyBackdropFilterCommand command,
        Stack<PushClipCommand> clipStack)
    {
        if (_backdropSceneBitmap is null || _backdropSnapshotBitmap is null)
        {
            throw new InvalidOperationException("Backdrop composition surfaces were not initialized.");
        }

        // The source must contain precisely the scene painted before this command. Detach the
        // target and close the segment before CopyFromBitmap to avoid a read/write feedback loop.
        SuspendClips(target, clipStack);
        var boundary = EndDrawSegment(target);
        boundary.CheckError();
        target.Target = null;
        _backdropSnapshotBitmap.CopyFromBitmap(_backdropSceneBitmap);
        _backdropMetrics.RecordSnapshotPass();
        target.Target = _backdropSceneBitmap;
        BeginDrawSegment(target);
        RestoreClips(target, clipStack);

        var localClip = new PushClipCommand(command.Rectangle, command.Radii);
        ApplyClip(target, localClip);
        try
        {
            Direct2DBackdropFilterPainter.Draw(target, _backdropSnapshotBitmap, command);
        }
        finally
        {
            PopClip(target, localClip);
        }
    }

    private void CompositeBackdropScene(ID2D1DeviceContext target, ColorRgba clearColor)
    {
        if (_backdropSceneBitmap is null)
        {
            throw new InvalidOperationException("Backdrop scene bitmap was not initialized.");
        }

        target.Target = _targetBitmap;
        BeginDrawSegment(target);
        target.Transform = Matrix3x2.Identity;
        target.Clear(ToColor4(clearColor));
        target.DrawImage(_backdropSceneBitmap, Vector2.Zero, null, Vortice.Direct2D1.InterpolationMode.Linear, CompositeMode.SourceOver);
        var result = EndDrawSegment(target);
        result.CheckError();
        _backdropMetrics.RecordCompositionPass();
    }

    private Direct2DTextShadowPainter.GlyphMask CreateTextShadowMask(
        ID2D1DeviceContext target,
        DrawTextShadowCommand command)
    {
        var mask = Direct2DTextShadowPainter.CreateMask(target, command, _dpiScale);
        if (mask.IsEmpty)
        {
            return mask;
        }

        using var sceneTarget = target.Target ?? throw new InvalidOperationException("The Direct2D scene target is not initialized.");
        var sceneTransform = target.Transform;
        try
        {
            target.Target = mask.Bitmap;
            BeginDrawSegment(target);
            Direct2DTextShadowPainter.RenderGlyphMask(target, _writeFactory, command, mask, _dpiScale);
            var result = EndDrawSegment(target);
            if (result.Failure)
            {
                result.CheckError();
            }

            return mask;
        }
        catch
        {
            if (_isDrawSegmentActive)
            {
                _ = EndDrawSegment(target);
            }

            mask.Dispose();
            throw;
        }
        finally
        {
            target.Target = sceneTarget;
            target.Transform = sceneTransform;
        }
    }

    private void DrawBackgroundImage(ID2D1DeviceContext target, DrawBackgroundImageCommand command)
    {
        if (!command.HasResolvedGeometry || command.Resource is null || command.Opacity <= 0 ||
            command.DestinationRectangle.IsEmpty || command.SourceRectangle.IsEmpty)
        {
            return;
        }

        var bitmap = GetImageBitmap(target, command.Resource);
        if (bitmap is null)
        {
            return;
        }

        Direct2DBackgroundImagePainter.Draw(target, bitmap, command);
    }

    private unsafe ID2D1Bitmap1? GetImageBitmap(ID2D1DeviceContext target, AetherUI.Runtime.ImageResourceResult resource)
    {
        if (_imageBitmaps.TryGetValue(resource, out var cached))
        {
            return cached;
        }

        var dimensions = resource.Dimensions;
        if (dimensions.Width <= 0 || dimensions.Height <= 0)
        {
            return null;
        }

        int byteCount;
        int stride;
        try
        {
            stride = checked(dimensions.Width * 4);
            byteCount = checked(stride * dimensions.Height);
        }
        catch (OverflowException)
        {
            return null;
        }
        if (resource.Pixels.Length < byteCount)
        {
            return null;
        }

        using var pinnedPixels = resource.Pixels.Pin();
        var bitmap = target.CreateBitmap(
            new SizeI(dimensions.Width, dimensions.Height),
            (IntPtr)pinnedPixels.Pointer,
            (uint)stride,
            new BitmapProperties1(
                new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
                96f,
                96f,
                BitmapOptions.None));
        _imageBitmaps.Add(resource, bitmap);
        return bitmap;
    }

    private void DrawText(ID2D1RenderTarget target, DrawTextCommand command)
    {
        if (string.IsNullOrEmpty(command.Text) || command.Rectangle.IsEmpty)
        {
            return;
        }

        var format = GetTextFormat(
            command.FontFamily,
            command.FontSize,
            command.FontWeight,
            command.Alignment,
            command.Wrap,
            verticalCenter: false);
        var rectangle = ToRect(command.Rectangle);
        using var layout = _writeFactory.CreateTextLayout(command.Text, format, rectangle.Width, rectangle.Height);
        target.PushAxisAlignedClip(rectangle, AntialiasMode.PerPrimitive);
        try
        {
            layout.Draw(IntPtr.Zero,
                new ColorGlyphTextRenderer(_writeFactory, target, GetBrush(command.Color), _dpiScale),
                rectangle.Left, rectangle.Top);
        }
        finally
        {
            target.PopAxisAlignedClip();
        }
    }

    private void DrawTextGradient(ID2D1RenderTarget target, DrawTextGradientCommand command)
    {
        if (string.IsNullOrEmpty(command.Text) || command.Rectangle.IsEmpty || command.Gradient.Stops.Count < 2)
        {
            return;
        }

        var format = GetTextFormat(
            command.FontFamily,
            command.FontSize,
            command.FontWeight,
            command.Alignment,
            wrap: false,
            verticalCenter: false);

        var rect = command.Rectangle;
        var gradient = command.Gradient;
        var stops = new Vortice.Direct2D1.GradientStop[gradient.Stops.Count];
        for (var i = 0; i < gradient.Stops.Count; i++)
        {
            var stop = gradient.Stops[i];
            var alpha = Math.Clamp(stop.Color.A * command.Opacity, 0f, 1f);
            stops[i] = new Vortice.Direct2D1.GradientStop
            {
                Position = stop.Offset,
                Color = ToColor4(stop.Color.WithAlpha(alpha)),
            };
        }

        using var stopCollection = target.CreateGradientStopCollection(stops);
        var centerX = rect.X + rect.Width / 2f;
        var centerY = rect.Y + rect.Height / 2f;
        var halfLength = MathF.Abs(rect.Width / 2f * gradient.DirectionX) + MathF.Abs(rect.Height / 2f * gradient.DirectionY);
        var start = new Vector2(centerX - gradient.DirectionX * halfLength, centerY - gradient.DirectionY * halfLength);
        var end = new Vector2(centerX + gradient.DirectionX * halfLength, centerY + gradient.DirectionY * halfLength);

        using var brush = target.CreateLinearGradientBrush(
            new LinearGradientBrushProperties { StartPoint = start, EndPoint = end },
            stopCollection);

        target.DrawText(command.Text, format, ToRect(rect), brush, DrawTextOptions.Clip);
    }

    private IDWriteTextFormat GetTextFormat(
        string family,
        float fontSize,
        UiFontWeight weight,
        UiTextAlign alignment,
        bool wrap,
        bool verticalCenter)
    {
        family = string.IsNullOrWhiteSpace(family) ? "Segoe UI" : family;
        fontSize = float.IsFinite(fontSize) && fontSize > 0 ? fontSize : 16f;
        var key = new TextFormatKey(family, fontSize, weight, alignment, wrap, verticalCenter);
        if (_textFormats.TryGetValue(key, out var cached))
        {
            return cached;
        }

        var format = _writeFactory.CreateTextFormat(
            family,
            (DWriteFontWeight)(ushort)weight,
            Vortice.DirectWrite.FontStyle.Normal,
            FontStretch.Normal,
            fontSize);
        format.TextAlignment = alignment switch
        {
            UiTextAlign.Center => DWriteTextAlignment.Center,
            UiTextAlign.End or UiTextAlign.Right => DWriteTextAlignment.Trailing,
            _ => DWriteTextAlignment.Leading,
        };
        format.ParagraphAlignment = verticalCenter ? ParagraphAlignment.Center : ParagraphAlignment.Near;
        format.WordWrapping = wrap ? WordWrapping.Wrap : WordWrapping.NoWrap;
        _textFormats.Add(key, format);
        return format;
    }

    private ID2D1SolidColorBrush GetBrush(ColorRgba color)
    {
        if (_brushes.TryGetValue(color, out var brush))
        {
            return brush;
        }

        var target = _renderTarget ?? throw new InvalidOperationException("El render target no está inicializado.");
        brush = target.CreateSolidColorBrush(ToColor4(color));
        _brushes.Add(color, brush);
        return brush;
    }

    /// <summary>
    /// The gradient line's start/end points span the full box along the direction vector, matching
    /// how CSS sizes a linear-gradient's axis against its box (project the box's half-extent onto
    /// the axis rather than just using the box's corners).
    /// </summary>
    /// <summary>
    /// This render target has no Direct2D effects pipeline set up (that needs an
    /// <c>ID2D1DeviceContext</c>, not the plain <c>ID2D1HwndRenderTarget</c> used here), so a real
    /// Gaussian blur isn't available. Approximating it with several concentric rounded rects —
    /// growing outward, fading out — gives a soft-edged glow that's visually close for the blur
    /// radii box-shadow is normally used with, without pulling in the effects/device-context stack.
    /// </summary>
    private void DrawBoxShadow(ID2D1RenderTarget target, DrawBoxShadowCommand command)
    {
        Direct2DBoxShadowPainter.Draw(_d2dFactory, target, command, GetBrush);
    }

    private void DrawGradientFill(ID2D1RenderTarget target, FillRoundedRectangleGradientCommand command)
    {
        var rect = command.Rectangle;
        var gradient = command.Gradient;
        if (gradient.Stops.Count < 2 || rect.Width <= 0 || rect.Height <= 0)
        {
            return;
        }

        var stops = new Vortice.Direct2D1.GradientStop[gradient.Stops.Count];
        for (var i = 0; i < gradient.Stops.Count; i++)
        {
            var stop = gradient.Stops[i];
            var alpha = Math.Clamp(stop.Color.A * command.Opacity, 0f, 1f);
            stops[i] = new Vortice.Direct2D1.GradientStop
            {
                Position = stop.Offset,
                Color = ToColor4(stop.Color.WithAlpha(alpha)),
            };
        }

        using var stopCollection = target.CreateGradientStopCollection(stops);

        var centerX = rect.X + rect.Width / 2f;
        var centerY = rect.Y + rect.Height / 2f;
        var halfLength = MathF.Abs(rect.Width / 2f * gradient.DirectionX) + MathF.Abs(rect.Height / 2f * gradient.DirectionY);
        var start = new Vector2(centerX - gradient.DirectionX * halfLength, centerY - gradient.DirectionY * halfLength);
        var end = new Vector2(centerX + gradient.DirectionX * halfLength, centerY + gradient.DirectionY * halfLength);

        using var brush = target.CreateLinearGradientBrush(
            new LinearGradientBrushProperties { StartPoint = start, EndPoint = end },
            stopCollection);

        FillRoundedGeometry(target, rect, command.Radii, brush);
    }

    /// <summary>
    /// A plain rect uses the cheap axis-aligned clip; a rounded one needs a geometry-masked layer,
    /// since Direct2D's axis-aligned clip has no notion of corner radius. <paramref name="clipStack"/>
    /// records which kind was pushed so <see cref="PopClip"/> pairs it with the matching pop call.
    /// </summary>
    private void PushClip(ID2D1RenderTarget target, PushClipCommand command, Stack<PushClipCommand> clipStack)
    {
        ApplyClip(target, command);
        clipStack.Push(command);
    }

    private void ApplyClip(ID2D1RenderTarget target, PushClipCommand command)
    {
        if (!command.Radii.IsZero)
        {
            using var geometry = CreateRoundedGeometry(command.Rectangle, command.Radii);
            var layerParameters = new LayerParameters
            {
                ContentBounds = ToRawRect(command.Rectangle),
                GeometricMask = geometry,
                MaskAntialiasMode = AntialiasMode.PerPrimitive,
                MaskTransform = Matrix3x2.Identity,
                Opacity = 1f,
                LayerOptions = LayerOptions.None,
            };
            target.PushLayer(layerParameters, null);
        }
        else
        {
            target.PushAxisAlignedClip(ToRawRect(command.Rectangle), AntialiasMode.PerPrimitive);
        }
    }

    private static void PopClip(ID2D1RenderTarget target, PushClipCommand command)
    {
        if (!command.Radii.IsZero)
        {
            target.PopLayer();
        }
        else
        {
            target.PopAxisAlignedClip();
        }
    }

    private static void SuspendClips(ID2D1RenderTarget target, Stack<PushClipCommand> clipStack)
    {
        foreach (var clip in clipStack)
        {
            PopClip(target, clip);
        }
    }

    private void RestoreClips(ID2D1RenderTarget target, Stack<PushClipCommand> clipStack)
    {
        foreach (var clip in clipStack.Reverse())
        {
            ApplyClip(target, clip);
        }
    }

    private void EnsureDeviceResources()
    {
        if (_renderTarget is null || _swapChain is null || _targetBitmap is null)
        {
            CreateDeviceResources();
        }
    }

    private void CreateDeviceResources()
    {
        ReleaseDeviceResources();
        try
        {
            CreateD3DDevice(DriverType.Hardware);
            CreateD2DAndSwapChain();
        }
        catch
        {
            // A software fallback keeps the renderer usable on machines whose active adapter
            // cannot create a BGRA-capable device. It still uses the exact same pipeline.
            ReleaseDeviceResources();
            CreateD3DDevice(DriverType.Warp);
            CreateD2DAndSwapChain();
        }
    }

    private void CreateD3DDevice(DriverType driverType)
    {
        var device = D3D11.D3D11CreateDevice(
            driverType,
            DeviceCreationFlags.BgraSupport);
        _d3dDevice = device;
        _d3dContext = device.ImmediateContext;
    }

    private void CreateD2DAndSwapChain()
    {
        using var dxgiDevice = _d3dDevice!.QueryInterface<IDXGIDevice>();
        using var adapter = dxgiDevice.GetAdapter();
        using var factory = adapter.GetParent<IDXGIFactory2>();

        _d2dDevice = _d2dFactory.CreateDevice(dxgiDevice);
        _renderTarget = _d2dDevice.CreateDeviceContext(DeviceContextOptions.None);
        _swapChain = factory.CreateSwapChainForHwnd(
            _d3dDevice,
            _windowHandle,
            new SwapChainDescription1(
                (uint)Math.Max(1, _pixelWidth),
                (uint)Math.Max(1, _pixelHeight),
                Format.B8G8R8A8_UNorm,
                false,
                Usage.RenderTargetOutput,
                2,
                Scaling.Stretch,
                SwapEffect.FlipSequential,
                AlphaMode.Ignore,
                SwapChainFlags.None));
        factory.MakeWindowAssociation(_windowHandle, WindowAssociationFlags.IgnoreAltEnter);
        CreateTargetBitmap();
    }

    private void ResizeSwapChain()
    {
        if (_renderTarget is null || _swapChain is null)
        {
            return;
        }

        if (!CloseActiveDrawSegmentForResize())
        {
            return;
        }

        ReleaseTargetBitmap();
        _d3dContext?.ClearState();
        _d3dContext?.Flush();
        var result = _swapChain.ResizeBuffers(
            0,
            (uint)Math.Max(1, _pixelWidth),
            (uint)Math.Max(1, _pixelHeight),
            Format.Unknown,
            SwapChainFlags.None);
        if (result.Failure)
        {
            if (result == Vortice.DXGI.ResultCode.DeviceRemoved || result == Vortice.DXGI.ResultCode.DeviceReset)
            {
                ReleaseDeviceResources();
                CreateDeviceResources();
                return;
            }

            result.CheckError();
        }

        CreateTargetBitmap();
    }

    private void RecreateTargetForDpiChange()
    {
        if (_renderTarget is null || _targetBitmap is null || !CloseActiveDrawSegmentForResize())
        {
            return;
        }

        // DPI belongs to the Direct2D target bitmap, not the DXGI swap chain. Keep the existing
        // buffers and only rebind their surface with the updated DPI metadata.
        ReleaseTargetBitmap();
        CreateTargetBitmap();
    }

    private bool CloseActiveDrawSegmentForResize()
    {
        var target = _renderTarget!;
        // A target can be detached during a valid active segment; the WARP probe verifies Flush
        // remains valid there. It must not be called when no BeginDraw has occurred.
        target.Target = null;
        if (!_isDrawSegmentActive)
        {
            return true;
        }

        try
        {
            target.Flush(out _, out _);
        }
        catch (SharpGenException exception) when (IsDeviceLoss(exception.ResultCode))
        {
            ReleaseDeviceResources();
            CreateDeviceResources();
            return false;
        }

        var result = EndDrawSegment(target);
        if (!result.Failure)
        {
            return true;
        }

        if (RecoverFromDeviceLoss(result, allowRetry: true))
        {
            return false;
        }

        result.CheckError();
        return false;
    }

    private void BeginDrawSegment(ID2D1DeviceContext target)
    {
        if (_isDrawSegmentActive)
        {
            throw new InvalidOperationException("A Direct2D draw segment is already active.");
        }

        target.BeginDraw();
        _isDrawSegmentActive = true;
    }

    private Result EndDrawSegment(ID2D1DeviceContext target)
    {
        if (!_isDrawSegmentActive)
        {
            throw new InvalidOperationException("No Direct2D draw segment is active.");
        }

        try
        {
            return target.EndDraw();
        }
        finally
        {
            _isDrawSegmentActive = false;
        }
    }

    private static bool IsDeviceLoss(Result result) =>
        result == Vortice.DXGI.ResultCode.DeviceRemoved || result == Vortice.DXGI.ResultCode.DeviceReset;

    private bool RecoverFromDeviceLoss(Result result, bool allowRetry)
    {
        if (!IsDeviceLoss(result))
        {
            return false;
        }

        ReleaseDeviceResources();
        if (!allowRetry)
        {
            result.CheckError();
        }

        CreateDeviceResources();
        return true;
    }

    private void CreateTargetBitmap()
    {
        using var surface = _swapChain!.GetBuffer<IDXGISurface>(0);
        var dpi = 96f * _dpiScale;
        _targetBitmap = _renderTarget!.CreateBitmapFromDxgiSurface(
            surface,
            new BitmapProperties1(
                new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
                dpi,
                dpi,
                BitmapOptions.Target | BitmapOptions.CannotDraw));
        _renderTarget.Target = _targetBitmap;
        _renderTarget.Dpi = new Vortice.Mathematics.Size(dpi, dpi);
        _renderTarget.AntialiasMode = AntialiasMode.PerPrimitive;
        _renderTarget.TextAntialiasMode = Vortice.Direct2D1.TextAntialiasMode.Grayscale;
        _targetPixelWidth = _pixelWidth;
        _targetPixelHeight = _pixelHeight;
        _targetDpiScale = _dpiScale;
    }

    private void ReleaseTargetBitmap()
    {
        if (_renderTarget is not null)
        {
            _renderTarget.Target = null;
        }

        foreach (var brush in _brushes.Values)
        {
            brush.Dispose();
        }

        _brushes.Clear();
        _targetBitmap?.Dispose();
        _targetBitmap = null;
        _targetPixelWidth = 0;
        _targetPixelHeight = 0;
        _targetDpiScale = 0;
    }

    private void ReleaseDeviceResources()
    {
        _isDrawSegmentActive = false;
        _textShadowMasks.Clear();
        ReleaseBackdropCompositionResources();
        foreach (var bitmap in _imageBitmaps.Values)
        {
            bitmap.Dispose();
        }
        _imageBitmaps.Clear();
        ReleaseTargetBitmap();
        _renderTarget?.Dispose();
        _renderTarget = null;
        _d2dDevice?.Dispose();
        _d2dDevice = null;
        _swapChain?.Dispose();
        _swapChain = null;
        _d3dContext?.Dispose();
        _d3dContext = null;
        _d3dDevice?.Dispose();
        _d3dDevice = null;
    }

    private static RoundedRectangle ToRoundedRectangle(RectF rectangle, float radius)
    {
        var width = MathF.Max(0, rectangle.Width);
        var height = MathF.Max(0, rectangle.Height);
        var safeRadius = MathF.Max(0, MathF.Min(radius, MathF.Min(width, height) / 2f));
        return new RoundedRectangle(
            new RectangleF(rectangle.X, rectangle.Y, width, height),
            safeRadius,
            safeRadius);
    }

    private void FillRoundedGeometry(ID2D1RenderTarget target, RectF rectangle, CornerRadii radii, ID2D1Brush brush)
    {
        if (radii.IsZero) { target.FillRectangle(ToRawRect(rectangle), brush); return; }
        using var geometry = CreateRoundedGeometry(rectangle, radii);
        target.FillGeometry(geometry, brush);
    }

    private void StrokeRoundedGeometry(ID2D1RenderTarget target, RectF rectangle, CornerRadii radii, ID2D1Brush brush, float width)
    {
        using var geometry = CreateRoundedGeometry(rectangle, radii);
        target.DrawGeometry(geometry, brush, width);
    }

    private void DrawBorderSide(ID2D1RenderTarget target, FillBorderSideCommand command)
    {
        using var ring = CreateRingGeometry(command.OuterRectangle, command.OuterRadii, command.InnerRectangle, command.InnerRadii);
        using var partition = CreateBorderSidePartition(command.OuterRectangle, command.InnerRectangle, command.Side);
        var bounds = ToRawRect(command.OuterRectangle);
        var layer = new LayerParameters { ContentBounds = bounds, MaskAntialiasMode = AntialiasMode.PerPrimitive, MaskTransform = Matrix3x2.Identity, Opacity = 1f, LayerOptions = LayerOptions.None };
        layer.GeometricMask = ring;
        target.PushLayer(layer, null);
        layer.GeometricMask = partition;
        target.PushLayer(layer, null);
        try { FillBorderStyle(target, command); }
        finally { target.PopLayer(); target.PopLayer(); }
    }

    private void FillBorderStyle(ID2D1RenderTarget target, FillBorderSideCommand command)
    {
        var outer = command.OuterRectangle; var inner = command.InnerRectangle;
        var horizontal = command.Side is 0 or 2;
        var thickness = horizontal ? MathF.Abs(inner.Top - outer.Top) : MathF.Abs(inner.Left - outer.Left);
        if (command.Side == 2) thickness = MathF.Abs(outer.Bottom - inner.Bottom);
        if (command.Side == 1) thickness = MathF.Abs(outer.Right - inner.Right);
        var color = command.Color;
        ColorRgba Shade(float factor) => new(Math.Clamp(color.R * factor, 0, 1), Math.Clamp(color.G * factor, 0, 1), Math.Clamp(color.B * factor, 0, 1), color.A);
        if (command.Style is 5 or 6) { target.FillRectangle(ToRawRect(outer), GetBrush(Shade(command.Style == 5 ? (command.Side is 0 or 3 ? .65f : 1.35f) : (command.Side is 0 or 3 ? 1.35f : .65f)))); return; }
        if (command.Style is 7 or 8 && thickness >= 2) {
            var first = thickness / 2; var dark = command.Style == 7 ? command.Side is 0 or 3 : command.Side is 1 or 2;
            if (horizontal) { target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y, outer.Width, first)), GetBrush(Shade(dark ? .65f : 1.35f))); target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y + first, outer.Width, outer.Height - first)), GetBrush(Shade(dark ? 1.35f : .65f))); }
            else { target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y, first, outer.Height)), GetBrush(Shade(dark ? .65f : 1.35f))); target.FillRectangle(ToRawRect(new RectF(outer.X + first, outer.Y, outer.Width - first, outer.Height)), GetBrush(Shade(dark ? 1.35f : .65f))); }
            return;
        }
        if (command.Style == 4 && thickness >= 3) {
            var band = thickness / 3;
            if (horizontal) { target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y, outer.Width, band)), GetBrush(color)); target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Bottom - band, outer.Width, band)), GetBrush(color)); }
            else { target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y, band, outer.Height)), GetBrush(color)); target.FillRectangle(ToRawRect(new RectF(outer.Right - band, outer.Y, band, outer.Height)), GetBrush(color)); }
            return;
        }
        if (command.Style is 2 or 3) {
            var length = horizontal ? outer.Width : outer.Height; var segment = command.Style == 2 ? thickness : MathF.Max(3 * thickness, 3); var step = segment + MathF.Max(thickness, 1);
            for (var p = 0f; p < length; p += step) {
                var part = horizontal ? new RectF(outer.X + p, outer.Y, MathF.Min(segment, length - p), outer.Height) : new RectF(outer.X, outer.Y + p, outer.Width, MathF.Min(segment, length - p));
                target.FillRectangle(ToRawRect(part), GetBrush(color));
            }
            return;
        }
        target.FillRectangle(ToRawRect(outer), GetBrush(color));
    }

    private ID2D1PathGeometry CreateRingGeometry(RectF outer, CornerRadii outerRadii, RectF inner, CornerRadii innerRadii)
    {
        var geometry = _d2dFactory.CreatePathGeometry(); using var sink = geometry.Open(); sink.SetFillMode(Vortice.Direct2D1.FillMode.Alternate);
        AddRoundedFigure(sink, outer, NormalizeRadii(outer, outerRadii));
        AddRoundedFigure(sink, inner, NormalizeRadii(inner, innerRadii)); sink.Close(); return geometry;
    }

    private ID2D1PathGeometry CreateBorderSidePartition(RectF outer, RectF inner, int side)
    {
        var points = side switch {
            0 => new[] { new Vector2(outer.Left, outer.Top), new Vector2(outer.Right, outer.Top), new Vector2(inner.Right, inner.Top), new Vector2(inner.Left, inner.Top) },
            1 => new[] { new Vector2(outer.Right, outer.Top), new Vector2(outer.Right, outer.Bottom), new Vector2(inner.Right, inner.Bottom), new Vector2(inner.Right, inner.Top) },
            2 => new[] { new Vector2(outer.Right, outer.Bottom), new Vector2(outer.Left, outer.Bottom), new Vector2(inner.Left, inner.Bottom), new Vector2(inner.Right, inner.Bottom) },
            _ => new[] { new Vector2(outer.Left, outer.Bottom), new Vector2(outer.Left, outer.Top), new Vector2(inner.Left, inner.Top), new Vector2(inner.Left, inner.Bottom) }, };
        var geometry = _d2dFactory.CreatePathGeometry(); using var sink = geometry.Open(); sink.BeginFigure(points[0], FigureBegin.Filled); sink.AddLines(points[1..]); sink.EndFigure(FigureEnd.Closed); sink.Close(); return geometry;
    }

    private static void AddRoundedFigure(ID2D1GeometrySink sink, RectF rectangle, CornerRadii r)
    {
        sink.BeginFigure(new Vector2(rectangle.Left + r.TopLeftX, rectangle.Top), FigureBegin.Filled);
        sink.AddLine(new Vector2(rectangle.Right - r.TopRightX, rectangle.Top)); sink.AddArc(Arc(rectangle.Right, rectangle.Top + r.TopRightY, r.TopRightX, r.TopRightY));
        sink.AddLine(new Vector2(rectangle.Right, rectangle.Bottom - r.BottomRightY)); sink.AddArc(Arc(rectangle.Right - r.BottomRightX, rectangle.Bottom, r.BottomRightX, r.BottomRightY));
        sink.AddLine(new Vector2(rectangle.Left + r.BottomLeftX, rectangle.Bottom)); sink.AddArc(Arc(rectangle.Left, rectangle.Bottom - r.BottomLeftY, r.BottomLeftX, r.BottomLeftY));
        sink.AddLine(new Vector2(rectangle.Left, rectangle.Top + r.TopLeftY)); sink.AddArc(Arc(rectangle.Left + r.TopLeftX, rectangle.Top, r.TopLeftX, r.TopLeftY)); sink.EndFigure(FigureEnd.Closed);
    }

    private ID2D1PathGeometry CreateRoundedGeometry(RectF rectangle, CornerRadii input)
    {
        var r = NormalizeRadii(rectangle, input);
        var geometry = _d2dFactory.CreatePathGeometry();
        using var sink = geometry.Open();
        sink.BeginFigure(new Vector2(rectangle.Left + r.TopLeftX, rectangle.Top), FigureBegin.Filled);
        sink.AddLine(new Vector2(rectangle.Right - r.TopRightX, rectangle.Top));
        sink.AddArc(Arc(rectangle.Right, rectangle.Top + r.TopRightY, r.TopRightX, r.TopRightY));
        sink.AddLine(new Vector2(rectangle.Right, rectangle.Bottom - r.BottomRightY));
        sink.AddArc(Arc(rectangle.Right - r.BottomRightX, rectangle.Bottom, r.BottomRightX, r.BottomRightY));
        sink.AddLine(new Vector2(rectangle.Left + r.BottomLeftX, rectangle.Bottom));
        sink.AddArc(Arc(rectangle.Left, rectangle.Bottom - r.BottomLeftY, r.BottomLeftX, r.BottomLeftY));
        sink.AddLine(new Vector2(rectangle.Left, rectangle.Top + r.TopLeftY));
        sink.AddArc(Arc(rectangle.Left + r.TopLeftX, rectangle.Top, r.TopLeftX, r.TopLeftY));
        sink.EndFigure(FigureEnd.Closed);
        sink.Close();
        return geometry;
    }

    private static ArcSegment Arc(float x, float y, float radiusX, float radiusY) => new()
    {
        Point = new Vector2(x, y), Size = new Vortice.Mathematics.Size(radiusX, radiusY), RotationAngle = 0,
        SweepDirection = SweepDirection.Clockwise, ArcSize = ArcSize.Small,
    };

    private static CornerRadii NormalizeRadii(RectF rectangle, CornerRadii r)
    {
        var scale = MathF.Min(1f, MathF.Min(
            rectangle.Width / MathF.Max(.0001f, r.TopLeftX + r.TopRightX),
            MathF.Min(rectangle.Width / MathF.Max(.0001f, r.BottomLeftX + r.BottomRightX),
            MathF.Min(rectangle.Height / MathF.Max(.0001f, r.TopLeftY + r.BottomLeftY), rectangle.Height / MathF.Max(.0001f, r.TopRightY + r.BottomRightY)))));
        return scale < 1 ? new CornerRadii(r.TopLeftX * scale, r.TopLeftY * scale, r.TopRightX * scale, r.TopRightY * scale, r.BottomRightX * scale, r.BottomRightY * scale, r.BottomLeftX * scale, r.BottomLeftY * scale) : r;
    }

    private static RawRectF ToRawRect(RectF rectangle) =>
        new(rectangle.Left, rectangle.Top, rectangle.Right, rectangle.Bottom);

    private static Rect ToRect(RectF rectangle)
    {
        var left = (int)MathF.Floor(rectangle.Left);
        var top = (int)MathF.Floor(rectangle.Top);
        var right = (int)MathF.Ceiling(rectangle.Right);
        var bottom = (int)MathF.Ceiling(rectangle.Bottom);
        return new Rect(left, top, Math.Max(1, right - left), Math.Max(1, bottom - top));
    }

    private static Color4 ToColor4(ColorRgba color) =>
        new(Math.Clamp(color.R, 0, 1), Math.Clamp(color.G, 0, 1), Math.Clamp(color.B, 0, 1), Math.Clamp(color.A, 0, 1));

    private static float NormalizeDpiScale(float value) =>
        float.IsFinite(value) && value > 0 ? value : 1f;

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        ReleaseDeviceResources();
        foreach (var format in _textFormats.Values)
        {
            format.Dispose();
        }

        _textFormats.Clear();
        _writeFactory.Dispose();
        _d2dFactory.Dispose();
        _disposed = true;
        GC.SuppressFinalize(this);
    }

    private readonly record struct TextFormatKey(
        string Family,
        float Size,
        UiFontWeight Weight,
        UiTextAlign Alignment,
        bool Wrap,
        bool VerticalCenter);
}
