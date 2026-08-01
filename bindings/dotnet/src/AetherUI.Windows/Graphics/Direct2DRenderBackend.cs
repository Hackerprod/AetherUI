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
/// Direct2D/DirectWrite implementation of AetherUI's retained display-list backend.
/// A single D3D11/DXGI/Direct2D device-context pipeline owns every GPU resource.
/// </summary>
public sealed class Direct2DRenderBackend : IRenderBackend, IIncrementalRenderBackend, ITextMeasurer
{
    private readonly nint _windowHandle;
    private readonly DriverType? _forcedDriverType;
    private readonly ID2D1Factory1 _d2dFactory;
    private readonly IDWriteFactory2 _writeFactory;
    private readonly Dictionary<ColorRgba, ID2D1SolidColorBrush> _brushes = new();
    private readonly Dictionary<TextFormatKey, IDWriteTextFormat> _textFormats = new();
    private readonly TextShadowMaskCache _textShadowMasks = new();
    private readonly TextShadowRenderMetrics _textShadowMetrics = new();
    private readonly BackdropCompositionMetrics _backdropMetrics = new();
    private readonly ScrollTileCache _scrollTileCache = new();
    private readonly ScrollTileRenderMetrics _scrollTileMetrics = new();
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
    private ID2D1Bitmap1? _retainedFrameBitmap;
    private DisplayList? _retainedFrameDisplayList;
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
    private Func<long, ScrollVisualState>? _scrollStateProvider;
    private bool _scrollTileCompositionEnabled;
    private bool _disposed;
    private readonly bool _incrementalDebug;

    public void SetScrollOffsetProvider(Func<long, ScrollVisualOffset>? provider) =>
        _scrollStateProvider = provider is null ? null : id => new ScrollVisualState(provider(id), default);

    public void SetScrollVisualStateProvider(Func<long, ScrollVisualState>? provider) =>
        _scrollStateProvider = provider;

    public void SetScrollCompositionMode(bool enabled) =>
        _scrollTileCompositionEnabled = enabled;
    private int _lastTransformPushes;
    private int _lastTransformPops;
    private bool _diagnosticCaptureRequested;
    private byte[]? _diagnosticCapturedFrame;
    private long _scrollTileDisplayListGeneration;
    private int _lastIncrementalStartIndex = -1;
    private int _lastIncrementalScannedCommands;
    private int _lastIncrementalPaintedCommands;

    // A clip is expressed in the coordinate space that was active when it was pushed. Keeping
    // that matrix with the clip is essential when a text-shadow cache miss or backdrop snapshot
    // temporarily closes and rebuilds the Direct2D draw segment: restoring the clip under the
    // transform current at that later point would change its shape and position.
    private readonly record struct ClipScope(
        PushClipCommand Command,
        Matrix3x2 Transform,
        int TransformDepth,
        bool UsesLayer);

    internal int LastTextShadowCacheHits => _textShadowMetrics.CacheHits;
    internal int LastTextShadowSceneSuspensions => _textShadowMetrics.SceneSuspensions;
    internal int LastBackdropAllocations => _backdropMetrics.Allocations;
    internal int LastBackdropCompositionPasses => _backdropMetrics.CompositionPasses;
    internal int LastBackdropSnapshotPasses => _backdropMetrics.SnapshotPasses;
    internal int LastTransformPushes => _lastTransformPushes;
    internal int LastTransformPops => _lastTransformPops;
    internal int LastScrollTileCacheHits => _scrollTileMetrics.CacheHits;
    internal int LastScrollTileCacheMisses => _scrollTileMetrics.CacheMisses;
    internal int LastScrollTileRasterizedCommands => _scrollTileMetrics.RasterizedCommands;
    internal int LastScrollTileEvictions => _scrollTileMetrics.Evictions;
    internal int LastScrollTileCacheBypasses => _scrollTileMetrics.Bypasses;
    internal long LastScrollTileBytes => _scrollTileCache.Bytes;
    internal int LastIncrementalStartIndex => _lastIncrementalStartIndex;
    internal int LastIncrementalScannedCommands => _lastIncrementalScannedCommands;
    internal int LastIncrementalPaintedCommands => _lastIncrementalPaintedCommands;

    /// <summary>
    /// Renders through the production swap-chain path and copies that exact completed
    /// frame before Present changes the current flip-sequential back buffer.
    /// </summary>
    internal byte[] RenderAndCaptureBackBufferForDiagnostics(DisplayList displayList, ColorRgba clearColor)
    {
        RequestBackBufferCaptureForDiagnostics();
        try
        {
            Render(displayList, clearColor);
            return TakeBackBufferCaptureForDiagnostics();
        }
        finally
        {
            _diagnosticCaptureRequested = false;
            _diagnosticCapturedFrame = null;
        }
    }

    /// <summary>Arms one production render for a deterministic, pre-Present back-buffer copy.</summary>
    internal void RequestBackBufferCaptureForDiagnostics()
    {
        _diagnosticCapturedFrame = null;
        _diagnosticCaptureRequested = true;
    }

    /// <summary>Returns the frame requested by <see cref="RequestBackBufferCaptureForDiagnostics"/>.</summary>
    internal byte[] TakeBackBufferCaptureForDiagnostics()
    {
        var capture = _diagnosticCapturedFrame
            ?? throw new InvalidOperationException("The Direct2D diagnostic frame was not captured before Present.");
        _diagnosticCaptureRequested = false;
        _diagnosticCapturedFrame = null;
        return capture;
    }

    private unsafe byte[] CopyBackBufferForDiagnostics()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_isDrawSegmentActive)
        {
            throw new InvalidOperationException("Cannot capture the back buffer while a Direct2D draw segment is active.");
        }

        var device = _d3dDevice ?? throw new InvalidOperationException("The D3D11 device is not initialized.");
        var context = _d3dContext ?? throw new InvalidOperationException("The D3D11 context is not initialized.");
        using var source = _swapChain?.GetBuffer<ID3D11Texture2D>(0)
            ?? throw new InvalidOperationException("The swap-chain back buffer is not initialized.");
        using var staging = device.CreateTexture2D(new Texture2DDescription(
            Format.B8G8R8A8_UNorm, (uint)_targetPixelWidth, (uint)_targetPixelHeight, 1, 1,
            BindFlags.None, ResourceUsage.Staging, CpuAccessFlags.Read, 1, 0, ResourceOptionFlags.None));
        context.CopyResource(staging, source);
        context.Flush();

        var pixels = new byte[_targetPixelWidth * _targetPixelHeight * 4];
        var mapped = context.Map(staging, 0, MapMode.Read, Vortice.Direct3D11.MapFlags.None);
        try
        {
            var sourceBytes = (byte*)mapped.DataPointer;
            for (var row = 0; row < _targetPixelHeight; ++row)
            {
                Marshal.Copy((nint)(sourceBytes + row * mapped.RowPitch), pixels,
                    row * _targetPixelWidth * 4, _targetPixelWidth * 4);
            }
        }
        finally
        {
            context.Unmap(staging, 0);
        }
        return pixels;
    }

    public Direct2DRenderBackend(nint windowHandle, int pixelWidth, int pixelHeight, float dpiScale)
        : this(windowHandle, pixelWidth, pixelHeight, dpiScale, null)
    {
    }

    // The friend regression assembly uses this overload to exercise the exact
    // swap-chain pipeline deterministically on WARP.
    internal Direct2DRenderBackend(nint windowHandle, int pixelWidth, int pixelHeight, float dpiScale,
        DriverType forcedDriverType)
        : this(windowHandle, pixelWidth, pixelHeight, dpiScale, (DriverType?)forcedDriverType)
    {
    }

    private Direct2DRenderBackend(nint windowHandle, int pixelWidth, int pixelHeight, float dpiScale,
        DriverType? forcedDriverType)
    {
        if (windowHandle == nint.Zero)
        {
            throw new ArgumentException("Se requiere un HWND válido.", nameof(windowHandle));
        }

        _windowHandle = windowHandle;
        _forcedDriverType = forcedDriverType;
        _pixelWidth = Math.Max(1, pixelWidth);
        _pixelHeight = Math.Max(1, pixelHeight);
        _dpiScale = NormalizeDpiScale(dpiScale);
        _incrementalDebug = string.Equals(
            Environment.GetEnvironmentVariable("AETHER_INC_DIAG"), "1", StringComparison.Ordinal);

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
        _scrollTileMetrics.Reset();
        _lastTransformPushes = 0;
        _lastTransformPops = 0;
        _lastIncrementalStartIndex = -1;
        _lastIncrementalScannedCommands = 0;
        _lastIncrementalPaintedCommands = 0;
        if (_scrollTileDisplayListGeneration != displayList.Generation)
        {
            _scrollTileCache.Clear();
            _scrollTileDisplayListGeneration = displayList.Generation;
        }
        RenderCore(displayList, clearColor, allowDeviceRetry: true);
    }

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
        RenderIncrementalCore(displayList, clearColor, plan, allowDeviceRetry: true);
        _lastIncrementalStartIndex = plan.FirstChangedIndex;
        return true;
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

        var clipStack = new Stack<ClipScope>();
        var transformStack = new Stack<Matrix3x2>();
        var scrollStack = new Stack<Matrix3x2>();
        for (var commandIndex = 0; commandIndex < displayList.Commands.Count; commandIndex++)
        {
            var command = displayList.Commands[commandIndex];
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
                    Direct2DBorderSidePainter.Draw(_d2dFactory, target, borderSide, GetBrush);
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
                    PushClip(target, push, clipStack, transformStack.Count);
                    break;
                case PopClipCommand:
                    if (clipStack.Count == 0)
                    {
                        throw new InvalidOperationException("The display list popped a clip that was not active.");
                    }
                    PopClip(target, clipStack.Pop());
                    break;
                case PushTransformCommand pushTransform:
                    transformStack.Push(target.Transform);
                    // Direct2D and the native ABI both use row-vector affine matrices. A child
                    // point must first pass through its local matrix and then through the
                    // ancestor matrix that is already active on the target.
                    target.Transform = ToDirect2DMatrix(pushTransform.Matrix) * target.Transform;
                    _lastTransformPushes++;
                    break;
                case PopTransformCommand:
                    if (transformStack.Count == 0)
                    {
                        throw new InvalidOperationException("The display list popped a transform that was not active.");
                    }

                    // Brackets cannot cross: a clip opened inside a transform must be closed
                    // before restoring the parent transform. Native display lists obey this;
                    // validating it here prevents a malformed foreign binding from leaking a
                    // device-space clip into siblings.
                    if (clipStack.Any(scope => scope.TransformDepth >= transformStack.Count))
                    {
                        throw new InvalidOperationException("The display list popped a transform while a nested clip was still active.");
                    }

                    target.Transform = transformStack.Pop();
                    _lastTransformPops++;
                    break;
                case PushScrollOffsetCommand scroll:
                {
                    var popIndex = FindMatchingScrollPop(displayList.Commands, commandIndex + 1);
                    if (_scrollTileCompositionEnabled &&
                        TryDrawCachedScrollSegment(target, displayList, commandIndex + 1, popIndex,
                            scroll.ScrollContainerId, clipStack, transformStack, clearColor, allowDeviceRetry,
                            out var restarted))
                    {
                        if (restarted)
                        {
                            return;
                        }

                        commandIndex = popIndex;
                        break;
                    }

                    scrollStack.Push(target.Transform);
                    var offset = _scrollStateProvider?.Invoke(scroll.ScrollContainerId).Offset ?? default;
                    target.Transform = Matrix3x2.CreateTranslation(-offset.X, -offset.Y) * target.Transform;
                    break;
                }
                case PopScrollOffsetCommand:
                    if (scrollStack.Count == 0)
                        throw new InvalidOperationException("The display list popped a scroll offset that was not active.");
                    target.Transform = scrollStack.Pop();
                    break;
                case DrawScrollbarChromeCommand chrome:
                    DrawScrollbarChrome(target, _scrollStateProvider?.Invoke(chrome.ScrollContainerId).Chrome ?? default);
                    break;
            }
        }

        while (clipStack.Count > 0)
        {
            PopClip(target, clipStack.Pop());
        }

        if (transformStack.Count != 0)
        {
            throw new InvalidOperationException("The display list ended with an active transform.");
        }
        if (scrollStack.Count != 0)
            throw new InvalidOperationException("The display list ended with an active scroll offset.");

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

        if (_diagnosticCaptureRequested)
        {
            _diagnosticCapturedFrame = CopyBackBufferForDiagnostics();
            _diagnosticCaptureRequested = false;
        }

        UpdateRetainedFrameCache(target, displayList);

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

        if (_diagnosticCaptureRequested)
        {
            _diagnosticCapturedFrame = CopyBackBufferForDiagnostics();
            _diagnosticCaptureRequested = false;
        }

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

    private sealed record CommandRange(int Start, int End);

    private sealed record DamagePlan(
        int FirstChangedIndex,
        IReadOnlyList<RectF> DirtyRects,
        IReadOnlyList<CommandRange> ChangedRanges,
        IReadOnlyList<int> StateCommandIndices);

    private bool TryCreateDamagePlan(DisplayList previous, DisplayList current, out DamagePlan plan)
    {
        plan = new DamagePlan(0, Array.Empty<RectF>(), Array.Empty<CommandRange>(), Array.Empty<int>());

        static bool Reject(string reason, bool incrementalDebug, int index = -1)
        {
            if (incrementalDebug)
            {
                Console.WriteLine(reason);
            }

            return false;
        }

        var oldCommands = previous.Commands;
        var newCommands = current.Commands;
        if (oldCommands.Count != newCommands.Count || oldCommands.Count == 0)
        {
            return Reject(
                $"TryCreateDamagePlan reject: count mismatch old={oldCommands.Count} new={newCommands.Count}",
                _incrementalDebug);
        }

        var dirtyRects = new List<RectF>();
        var firstChanged = -1;
        var scrollOffsets = new Stack<ScrollVisualOffset>();
        var currentScrollOffset = new ScrollVisualOffset(0, 0);
        for (var i = 0; i < oldCommands.Count; i++)
        {
            var oldCommand = oldCommands[i];
            var newCommand = newCommands[i];
            if (oldCommand.GetType() != newCommand.GetType())
            {
                return Reject(
                    $"TryCreateDamagePlan reject: type mismatch index={i} old={oldCommand.GetType().Name} new={newCommand.GetType().Name}",
                    _incrementalDebug);
            }

            if (RequiresFullRenderForDamage(oldCommand))
            {
            return Reject(
                $"TryCreateDamagePlan reject: unsupported command for incremental index={i} type={oldCommand.GetType().Name}",
                _incrementalDebug);
        }

            if (oldCommand is PushScrollOffsetCommand oldScroll)
            {
                if (!PaintCommandsEqualForDamage(oldCommand, newCommand))
                {
                    return Reject(
                        $"TryCreateDamagePlan reject: scroll container changed at index={i} type={oldCommand.GetType().Name}",
                        _incrementalDebug);
                }

                scrollOffsets.Push(currentScrollOffset);
                var offset = _scrollStateProvider?.Invoke(oldScroll.ScrollContainerId).Offset ?? default;
                currentScrollOffset = new ScrollVisualOffset(
                    currentScrollOffset.X + offset.X,
                    currentScrollOffset.Y + offset.Y);
                continue;
            }

            if (oldCommand is PopScrollOffsetCommand)
            {
                if (scrollOffsets.Count == 0)
                {
                    return Reject(
                        $"TryCreateDamagePlan reject: unmatched scroll pop index={i}",
                        _incrementalDebug);
                }

                currentScrollOffset = scrollOffsets.Pop();
                continue;
            }

            if (oldCommand is PushClipCommand or PopClipCommand or DrawScrollbarChromeCommand)
            {
                if (!PaintCommandsEqualForDamage(oldCommand, newCommand))
                {
                    return Reject(
                        $"TryCreateDamagePlan reject: structural command changed index={i} type={oldCommand.GetType().Name}",
                        _incrementalDebug);
                }

                continue;
            }

            if (PaintCommandsEqualForDamage(oldCommand, newCommand))
            {
                continue;
            }

            if (!TryGetCommandBounds(oldCommand, out var oldBounds) ||
                !TryGetCommandBounds(newCommand, out var newBounds))
            {
                return Reject(
                    $"TryCreateDamagePlan reject: bounds unavailable index={i} type={oldCommand.GetType().Name}",
                    _incrementalDebug);
            }

            firstChanged = firstChanged < 0 ? i : firstChanged;
            oldBounds = TranslateForScroll(oldBounds, currentScrollOffset);
            newBounds = TranslateForScroll(newBounds, currentScrollOffset);
            AddDirtyRect(dirtyRects, oldBounds.Inflate(3));
            AddDirtyRect(dirtyRects, newBounds.Inflate(3));
        }

        if (firstChanged < 0)
        {
            plan = new DamagePlan(0, Array.Empty<RectF>(), Array.Empty<CommandRange>(), Array.Empty<int>());
            return true;
        }

        if (scrollOffsets.Count != 0)
        {
            return Reject("TryCreateDamagePlan reject: unmatched scroll push", _incrementalDebug);
        }

        var viewport = new RectF(0, 0, _targetPixelWidth / _targetDpiScale, _targetPixelHeight / _targetDpiScale);
        var clippedDirtyRects = new List<RectF>(dirtyRects.Count);
        var dirtyArea = 0f;
        foreach (var dirtyRect in dirtyRects)
        {
            var clipped = Intersect(dirtyRect, viewport);
            if (clipped.IsEmpty)
            {
                continue;
            }

            clippedDirtyRects.Add(clipped);
            dirtyArea += clipped.Width * clipped.Height;
        }

        if (_incrementalDebug && clippedDirtyRects.Count > 0)
        {
            Console.WriteLine($"TryCreateDamagePlan firstChanged={firstChanged} dirtyRects={dirtyRects.Count} clipped={clippedDirtyRects.Count}");
            for (var i = 0; i < clippedDirtyRects.Count; i++)
            {
                var rect = clippedDirtyRects[i];
                Console.WriteLine($"  dirty[{i}] x={rect.X:F2} y={rect.Y:F2} w={rect.Width:F2} h={rect.Height:F2}");
            }
        }

        if (clippedDirtyRects.Count == 0)
        {
            return Reject("TryCreateDamagePlan reject: no visible dirty area", _incrementalDebug);
        }

        if (dirtyArea > viewport.Width * viewport.Height * 0.45f)
        {
            if (_incrementalDebug)
            {
                Console.WriteLine(
                    $"TryCreateDamagePlan reject: dirty area too large area={dirtyArea} viewport={viewport.Width * viewport.Height}");
            }

            return false;
        }

        var changedRanges = CollectRepaintRanges(newCommands, clippedDirtyRects);
        if (changedRanges.Count == 0)
        {
            return Reject("TryCreateDamagePlan reject: dirty area has no repaintable commands", _incrementalDebug);
        }

        if (_incrementalDebug && changedRanges.Count > 0)
        {
            Console.WriteLine($"TryCreateDamagePlan changed ranges: {changedRanges.Count}.");
            for (var i = 0; i < changedRanges.Count; i++)
            {
                Console.WriteLine($"  range[{i}]: {changedRanges[i].Start}-{changedRanges[i].End}");
            }
        }

        if (!TryCollectStateCommandIndices(oldCommands, changedRanges, out var stateCommandIndices))
        {
            return Reject("TryCreateDamagePlan reject: unbalanced state stack.", _incrementalDebug);
        }

        plan = new DamagePlan(firstChanged, clippedDirtyRects, changedRanges, stateCommandIndices);
        return true;
    }

    private List<CommandRange> CollectRepaintRanges(
        IReadOnlyList<PaintCommand> commands,
        IReadOnlyList<RectF> dirtyRects)
    {
        var repaintIndices = new List<int>();
        var scrollOffsets = new Stack<ScrollVisualOffset>();
        var currentScrollOffset = new ScrollVisualOffset(0, 0);

        for (var i = 0; i < commands.Count; i++)
        {
            var command = commands[i];
            switch (command)
            {
                case PushScrollOffsetCommand scroll:
                {
                    scrollOffsets.Push(currentScrollOffset);
                    var offset = _scrollStateProvider?.Invoke(scroll.ScrollContainerId).Offset ?? default;
                    currentScrollOffset = new ScrollVisualOffset(
                        currentScrollOffset.X + offset.X,
                        currentScrollOffset.Y + offset.Y);
                    continue;
                }
                case PopScrollOffsetCommand:
                    if (scrollOffsets.Count == 0)
                    {
                        return [];
                    }

                    currentScrollOffset = scrollOffsets.Pop();
                    continue;
                case PushClipCommand or PopClipCommand or DrawScrollbarChromeCommand:
                    continue;
            }

            if (TryGetCommandBounds(command, out var bounds) &&
                CommandIntersectsDirtyRects(bounds, dirtyRects, currentScrollOffset))
            {
                repaintIndices.Add(i);
            }
        }

        if (scrollOffsets.Count != 0 || repaintIndices.Count == 0)
        {
            return [];
        }

        return ToCommandRanges(repaintIndices);
    }

    private static List<CommandRange> ToCommandRanges(List<int> indices)
    {
        indices.Sort();
        var ranges = new List<CommandRange>(indices.Count);
        var rangeStart = indices[0];
        var rangeEnd = rangeStart + 1;
        for (var i = 1; i < indices.Count; i++)
        {
            var index = indices[i];
            if (index == rangeEnd)
            {
                rangeEnd = index + 1;
            }
            else
            {
                ranges.Add(new CommandRange(rangeStart, rangeEnd));
                rangeStart = index;
                rangeEnd = index + 1;
            }
        }

        ranges.Add(new CommandRange(rangeStart, rangeEnd));
        return ranges;
    }

    private static bool TryCollectStateCommandIndices(
        IReadOnlyList<PaintCommand> commands,
        IReadOnlyList<CommandRange> changedRanges,
        out IReadOnlyList<int> stateCommandIndices)
    {
        stateCommandIndices = Array.Empty<int>();

        var stateStack = new Stack<(int index, bool isScroll)>();
        var statePairs = new List<(int pushIndex, int popIndex)>();

        for (var i = 0; i < commands.Count; i++)
        {
            switch (commands[i])
            {
                case PushClipCommand:
                    stateStack.Push((i, isScroll: false));
                    break;
                case PopClipCommand:
                    if (stateStack.Count == 0)
                    {
                        return false;
                    }

                    var statePop = stateStack.Pop();
                    if (statePop.isScroll)
                    {
                        return false;
                    }

                    statePairs.Add((statePop.index, i));
                    break;
                case PushScrollOffsetCommand:
                    stateStack.Push((i, isScroll: true));
                    break;
                case PopScrollOffsetCommand:
                    if (stateStack.Count == 0)
                    {
                        return false;
                    }

                    var statePush = stateStack.Pop();
                    if (!statePush.isScroll)
                    {
                        return false;
                    }

                    statePairs.Add((statePush.index, i));
                    break;
            }
        }

        if (stateStack.Count != 0)
        {
            return false;
        }

        static bool SpansChanged(int push, int pop, IReadOnlyList<CommandRange> ranges)
        {
            foreach (var range in ranges)
            {
                if (range.Start < pop && range.End > push)
                {
                    return true;
                }

                if (range.Start > pop)
                {
                    return false;
                }
            }

            return false;
        }

        var changedStateCommands = new HashSet<int>(Math.Min(commands.Count, 128));
        foreach (var (push, pop) in statePairs)
        {
            if (SpansChanged(push, pop, changedRanges))
            {
                changedStateCommands.Add(push);
                changedStateCommands.Add(pop);
            }
        }

        if (changedStateCommands.Count == 0)
        {
            return true;
        }

        var stateIndices = changedStateCommands.ToArray();
        Array.Sort(stateIndices);
        stateCommandIndices = stateIndices;
        return true;
    }

    private static bool RequiresFullRenderForDamage(PaintCommand command) =>
        command is PushTransformCommand or PopTransformCommand or
            ApplyBackdropFilterCommand or DrawTextShadowCommand;

    private static RectF TranslateForScroll(RectF rect, ScrollVisualOffset offset) =>
        new(rect.X - offset.X, rect.Y - offset.Y, rect.Width, rect.Height);

    private static void AddDirtyRect(List<RectF> dirtyRects, RectF rect)
    {
        if (rect.IsEmpty)
        {
            return;
        }

        for (var i = 0; i < dirtyRects.Count; i++)
        {
            if (!RectsIntersect(dirtyRects[i], rect))
            {
                continue;
            }

            dirtyRects[i] = Union(dirtyRects[i], rect);
            return;
        }

        dirtyRects.Add(rect);
    }

    private static bool PaintCommandsEqualForDamage(PaintCommand oldCommand, PaintCommand newCommand)
    {
        if (ReferenceEquals(oldCommand, newCommand))
        {
            return true;
        }

        if (oldCommand.GetType() != newCommand.GetType())
        {
            return false;
        }

        return oldCommand switch
        {
            FillRoundedRectangleGradientCommand oldGradient
                when newCommand is FillRoundedRectangleGradientCommand newGradient =>
                    oldGradient.Rectangle.Equals(newGradient.Rectangle) &&
                    oldGradient.Radii.Equals(newGradient.Radii) &&
                    oldGradient.GradientRectangle.Equals(newGradient.GradientRectangle) &&
                    oldGradient.Opacity.Equals(newGradient.Opacity) &&
                    LinearGradientsEqual(oldGradient.Gradient, newGradient.Gradient),

            DrawTextGradientCommand oldTextGradient
                when newCommand is DrawTextGradientCommand newTextGradient =>
                    string.Equals(oldTextGradient.Text, newTextGradient.Text, StringComparison.Ordinal) &&
                    oldTextGradient.Rectangle.Equals(newTextGradient.Rectangle) &&
                    string.Equals(oldTextGradient.FontFamily, newTextGradient.FontFamily, StringComparison.Ordinal) &&
                    oldTextGradient.FontSize.Equals(newTextGradient.FontSize) &&
                    oldTextGradient.FontWeight.Equals(newTextGradient.FontWeight) &&
                    oldTextGradient.Alignment.Equals(newTextGradient.Alignment) &&
                    oldTextGradient.Opacity.Equals(newTextGradient.Opacity) &&
                    LinearGradientsEqual(oldTextGradient.Gradient, newTextGradient.Gradient),

            _ => oldCommand.Equals(newCommand)
        };
    }

    private static bool LinearGradientsEqual(LinearGradient oldGradient, LinearGradient newGradient)
    {
        if (!oldGradient.DirectionX.Equals(newGradient.DirectionX) ||
            !oldGradient.DirectionY.Equals(newGradient.DirectionY) ||
            oldGradient.Stops.Count != newGradient.Stops.Count)
        {
            return false;
        }

        for (var i = 0; i < oldGradient.Stops.Count; i++)
        {
            if (!oldGradient.Stops[i].Equals(newGradient.Stops[i]))
            {
                return false;
            }
        }

        return true;
    }

    private static RectF? Union(RectF? current, RectF next) =>
        current is { } rect ? Union(rect, next) : next;

    private static RectF Union(RectF a, RectF b) =>
        RectF.FromEdges(MathF.Min(a.Left, b.Left), MathF.Min(a.Top, b.Top),
            MathF.Max(a.Right, b.Right), MathF.Max(a.Bottom, b.Bottom));

    private static bool RectsIntersect(RectF a, RectF b) =>
        a.Left < b.Right && a.Right > b.Left && a.Top < b.Bottom && a.Bottom > b.Top;

    private static bool TryGetCommandBounds(PaintCommand command, out RectF bounds)
    {
        switch (command)
        {
            case FillRoundedRectangleCommand fill:
                bounds = fill.Rectangle;
                return true;
            case FillRoundedRectangleGradientCommand fill:
                bounds = fill.Rectangle;
                return true;
            case DrawBoxShadowCommand shadow:
            {
                var spread = MathF.Abs(shadow.Shadow.SpreadRadius);
                var blur = MathF.Max(0, shadow.Shadow.BlurRadius);
                var inflated = shadow.BorderBoxRectangle.Inflate(spread + (blur * 2) + 4);
                bounds = inflated.Offset(shadow.Shadow.OffsetX, shadow.Shadow.OffsetY);
                return true;
            }
            case StrokeRoundedRectangleCommand stroke:
                bounds = stroke.Rectangle.Inflate(MathF.Max(.25f, stroke.StrokeWidth) * .5f + 2);
                return true;
            case FillBorderSideCommand border:
                bounds = border.OuterRectangle.Inflate(2);
                return true;
            case DrawTextCommand text:
                bounds = text.Rectangle.Inflate(2);
                return true;
            case DrawTextGradientCommand text:
                bounds = text.Rectangle.Inflate(2);
                return true;
            case DrawBackgroundImageCommand image:
                bounds = image.HasResolvedGeometry ? image.DestinationRectangle : image.PaintRectangle;
                return true;
            case PushClipCommand or PopClipCommand:
                bounds = default;
                return false;
            default:
                bounds = default;
                return false;
        }
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

    private static bool CommandIntersectsDirtyRects(RectF commandBounds, IReadOnlyList<RectF> dirtyRects, ScrollVisualOffset scrollOffset)
    {
        var translated = TranslateForScroll(commandBounds, scrollOffset);
        for (var i = 0; i < dirtyRects.Count; i++)
        {
            if (RectsIntersect(translated, dirtyRects[i]))
            {
                return true;
            }
        }

        return false;
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

    private static int FindMatchingScrollPop(IReadOnlyList<PaintCommand> commands, int start)
    {
        var depth = 1;
        for (var i = start; i < commands.Count; i++)
        {
            switch (commands[i])
            {
                case PushScrollOffsetCommand:
                    depth++;
                    break;
                case PopScrollOffsetCommand:
                    depth--;
                    if (depth == 0)
                    {
                        return i;
                    }
                    break;
            }
        }

        throw new InvalidOperationException("The display list pushed a scroll offset without a matching pop.");
    }

    private bool TryDrawCachedScrollSegment(
        ID2D1DeviceContext target,
        DisplayList displayList,
        int segmentStart,
        int segmentEnd,
        long scrollContainerId,
        Stack<ClipScope> clipStack,
        Stack<Matrix3x2> transformStack,
        ColorRgba clearColor,
        bool allowDeviceRetry,
        out bool restarted)
    {
        restarted = false;
        if (!IsScrollSegmentCacheable(target, displayList.Commands, segmentStart, segmentEnd, clipStack, transformStack))
        {
            _scrollTileMetrics.RecordBypass();
            return false;
        }

        var viewport = clipStack.Peek().Command.Rectangle;
        if (viewport.IsEmpty)
        {
            return true;
        }

        var state = _scrollStateProvider?.Invoke(scrollContainerId) ?? default;
        var offset = state.Offset;
        var tilePixels = ScrollTileCache.TilePixelSize;
        var tileSize = tilePixels / MathF.Max(.0001f, _dpiScale);
        var visibleContent = new RectF(viewport.X + offset.X, viewport.Y + offset.Y, viewport.Width, viewport.Height);
        var firstTileX = (int)MathF.Floor(visibleContent.Left / tileSize);
        var lastTileX = (int)MathF.Floor(MathF.Max(visibleContent.Left, visibleContent.Right - .001f) / tileSize);
        var firstTileY = (int)MathF.Floor(visibleContent.Top / tileSize);
        var lastTileY = (int)MathF.Floor(MathF.Max(visibleContent.Top, visibleContent.Bottom - .001f) / tileSize);

        try
        {
            for (var tileY = firstTileY; tileY <= lastTileY; tileY++)
            {
                for (var tileX = firstTileX; tileX <= lastTileX; tileX++)
                {
                    var tileRect = new RectF(tileX * tileSize, tileY * tileSize, tileSize, tileSize);
                    var visibleInTile = Intersect(visibleContent, tileRect);
                    if (visibleInTile.IsEmpty)
                    {
                        continue;
                    }

                    var key = new ScrollTileKey(displayList.Generation, scrollContainerId,
                        FloatBits(_dpiScale), tilePixels, tileX, tileY);
                    if (_scrollTileCache.TryGet(key, out var tile))
                    {
                        _scrollTileMetrics.RecordCacheHit();
                    }
                    else
                    {
                        _scrollTileMetrics.RecordCacheMiss();
                        tile = RasterizeScrollTile(target, displayList, segmentStart, segmentEnd, tileRect,
                            clipStack, clearColor);
                        var evictions = _scrollTileCache.TryAdd(key, tile);
                        _scrollTileMetrics.RecordEvictions(evictions);
                    }

                    DrawScrollTile(target, tile.Bitmap, tileRect, visibleInTile, offset);
                }
            }

            return true;
        }
        catch (SharpGenException exception) when (IsDeviceLoss(exception.ResultCode) && allowDeviceRetry)
        {
            ReleaseDeviceResources();
            CreateDeviceResources();
            RenderCore(displayList, clearColor, allowDeviceRetry: false);
            restarted = true;
            return true;
        }
    }

    private static bool IsScrollSegmentCacheable(
        ID2D1DeviceContext target,
        IReadOnlyList<PaintCommand> commands,
        int segmentStart,
        int segmentEnd,
        Stack<ClipScope> clipStack,
        Stack<Matrix3x2> transformStack)
    {
        if (clipStack.Count == 0 || transformStack.Count != 0 || target.Transform != Matrix3x2.Identity)
        {
            return false;
        }

        for (var i = segmentStart; i < segmentEnd; i++)
        {
            switch (commands[i])
            {
                case ApplyBackdropFilterCommand:
                case DrawTextShadowCommand:
                case PushTransformCommand:
                case PopTransformCommand:
                case PushScrollOffsetCommand:
                case PopScrollOffsetCommand:
                case DrawScrollbarChromeCommand:
                    return false;
            }
        }

        return true;
    }

    private ScrollTile RasterizeScrollTile(
        ID2D1DeviceContext target,
        DisplayList displayList,
        int segmentStart,
        int segmentEnd,
        RectF tileRect,
        Stack<ClipScope> sceneClipStack,
        ColorRgba clearColor)
    {
        using var sceneTarget = target.Target
            ?? throw new InvalidOperationException("The Direct2D scene target is not initialized.");
        var sceneTransform = target.Transform;
        ID2D1Bitmap1? bitmap = null;
        var success = false;
        var restoreScene = false;

        try
        {
            SuspendClips(target, sceneClipStack);
            var boundary = EndDrawSegment(target);
            boundary.CheckError();
            restoreScene = true;

            bitmap = CreateScrollTileBitmap(target);
            target.Target = bitmap;
            BeginDrawSegment(target);
            target.Transform = Matrix3x2.CreateTranslation(-tileRect.X, -tileRect.Y);
            target.Clear(new Color4(0, 0, 0, 0));
            var rasterizedCommands = RenderScrollTileCommandRange(target, displayList.Commands, segmentStart, segmentEnd);
            var tileResult = EndDrawSegment(target);
            tileResult.CheckError();
            _scrollTileMetrics.RecordRasterizedCommands(rasterizedCommands);
            success = true;
            return new ScrollTile(bitmap);
        }
        finally
        {
            if (_isDrawSegmentActive)
            {
                try
                {
                    _ = EndDrawSegment(target);
                }
                catch
                {
                    // The original exception remains the actionable failure. Device recovery
                    // happens at the outer render boundary.
                }
            }

            target.Target = sceneTarget;
            target.Transform = sceneTransform;
            if (restoreScene)
            {
                BeginDrawSegment(target);
                RestoreClips(target, sceneClipStack);
            }
            if (!success)
            {
                bitmap?.Dispose();
            }
        }
    }

    private ID2D1Bitmap1 CreateScrollTileBitmap(ID2D1DeviceContext target)
    {
        var dpi = 96f * _dpiScale;
        return target.CreateBitmap(
            new SizeI(ScrollTileCache.TilePixelSize, ScrollTileCache.TilePixelSize),
            IntPtr.Zero,
            0,
            new BitmapProperties1(
                new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
                dpi,
                dpi,
                BitmapOptions.Target));
    }

    private int RenderScrollTileCommandRange(
        ID2D1DeviceContext target,
        IReadOnlyList<PaintCommand> commands,
        int start,
        int end)
    {
        var rasterizedCommands = 0;
        var clipStack = new Stack<ClipScope>();
        for (var i = start; i < end; i++)
        {
            switch (commands[i])
            {
                case FillRoundedRectangleCommand fill:
                    FillRoundedGeometry(target, fill.Rectangle, fill.Radii, GetBrush(fill.Color));
                    rasterizedCommands++;
                    break;
                case FillRoundedRectangleGradientCommand gradientFill:
                    DrawGradientFill(target, gradientFill);
                    rasterizedCommands++;
                    break;
                case DrawBoxShadowCommand shadow:
                    DrawBoxShadow(target, shadow);
                    rasterizedCommands++;
                    break;
                case StrokeRoundedRectangleCommand stroke:
                    StrokeRoundedGeometry(target, stroke.Rectangle, stroke.Radii, GetBrush(stroke.Color), MathF.Max(.25f, stroke.StrokeWidth));
                    rasterizedCommands++;
                    break;
                case FillBorderSideCommand borderSide:
                    Direct2DBorderSidePainter.Draw(_d2dFactory, target, borderSide, GetBrush);
                    rasterizedCommands++;
                    break;
                case DrawTextCommand text:
                    DrawText(target, text);
                    rasterizedCommands++;
                    break;
                case DrawTextGradientCommand textGradient:
                    DrawTextGradient(target, textGradient);
                    rasterizedCommands++;
                    break;
                case DrawBackgroundImageCommand image:
                    DrawBackgroundImage(target, image);
                    rasterizedCommands++;
                    break;
                case PushClipCommand push:
                    PushClip(target, push, clipStack, transformDepth: 0);
                    break;
                case PopClipCommand:
                    if (clipStack.Count == 0)
                    {
                        throw new InvalidOperationException("A cached scroll tile popped a clip that was not active.");
                    }

                    PopClip(target, clipStack.Pop());
                    break;
            }
        }

        if (clipStack.Count != 0)
        {
            throw new InvalidOperationException("A cached scroll tile ended with an active clip.");
        }

        return rasterizedCommands;
    }

    private static void DrawScrollTile(
        ID2D1DeviceContext target,
        ID2D1Bitmap1 bitmap,
        RectF tileRect,
        RectF visibleInTile,
        ScrollVisualOffset offset)
    {
        var destination = new RectF(
            visibleInTile.X - offset.X,
            visibleInTile.Y - offset.Y,
            visibleInTile.Width,
            visibleInTile.Height);
        var source = new RectF(
            visibleInTile.X - tileRect.X,
            visibleInTile.Y - tileRect.Y,
            visibleInTile.Width,
            visibleInTile.Height);
        target.DrawBitmap(bitmap, ToRawRect(destination), 1f,
            Vortice.Direct2D1.InterpolationMode.Linear, ToRawRect(source), Matrix4x4.Identity);
    }

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
        Stack<ClipScope> clipStack)
    {
        if (_backdropSceneBitmap is null || _backdropSnapshotBitmap is null)
        {
            throw new InvalidOperationException("Backdrop composition surfaces were not initialized.");
        }

        var activeTransform = target.Transform;
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
        target.Transform = activeTransform;
        RestoreClips(target, clipStack);

        // The snapshot is an image in scene coordinates, while the filter rectangle is in the
        // element's local CSS coordinate system. Keep the transformed geometric clip active but
        // draw the snapshot with identity so its source pixels are neither translated nor rotated
        // a second time. This is also why transformed backdrop-filter needs more than simply
        // inheriting the ordinary target.Transform used by fills and text.
        var localClip = ApplyClip(target, new PushClipCommand(command.Rectangle, command.Radii), activeTransform, transformDepth: 0);
        try
        {
            target.Transform = Matrix3x2.Identity;
            Direct2DBackdropFilterPainter.Draw(
                target,
                _backdropSnapshotBitmap,
                command.Filters,
                TransformBounds(command.Rectangle, activeTransform));
        }
        finally
        {
            target.Transform = activeTransform;
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

    private void DrawScrollbarChrome(ID2D1DeviceContext target, ScrollbarChrome chrome)
    {
        var thumbBrush = GetBrush(new ColorRgba(.5f, .5f, .5f, .55f));
        var cornerBrush = GetBrush(new ColorRgba(.5f, .5f, .5f, .28f));
        if (chrome.VerticalVisible) DrawScrollbarThumb(target, chrome.VerticalThumb, thumbBrush);
        if (chrome.HorizontalVisible) DrawScrollbarThumb(target, chrome.HorizontalThumb, thumbBrush);
        if (chrome.VerticalVisible && chrome.HorizontalVisible && !chrome.Corner.IsEmpty)
        {
            target.FillRectangle(ToRawRect(chrome.Corner), cornerBrush);
        }
    }

    private static void DrawScrollbarThumb(ID2D1DeviceContext target, RectF thumb, ID2D1Brush brush)
    {
        if (thumb.IsEmpty) return;
        var radius = MathF.Min(thumb.Width, thumb.Height) / 2f;
        target.FillRoundedRectangle(ToRoundedRectangle(thumb, radius), brush);
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
        var gradientRect = command.Rectangle;
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
        var centerX = gradientRect.X + gradientRect.Width / 2f;
        var centerY = gradientRect.Y + gradientRect.Height / 2f;
        var halfLength = MathF.Abs(gradientRect.Width / 2f * gradient.DirectionX) + MathF.Abs(gradientRect.Height / 2f * gradient.DirectionY);
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
        var gradientRect = command.GradientRectangle;
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

        var centerX = gradientRect.X + gradientRect.Width / 2f;
        var centerY = gradientRect.Y + gradientRect.Height / 2f;
        var halfLength = MathF.Abs(gradientRect.Width / 2f * gradient.DirectionX) + MathF.Abs(gradientRect.Height / 2f * gradient.DirectionY);
        var start = new Vector2(centerX - gradient.DirectionX * halfLength, centerY - gradient.DirectionY * halfLength);
        var end = new Vector2(centerX + gradient.DirectionX * halfLength, centerY + gradient.DirectionY * halfLength);

        using var brush = target.CreateLinearGradientBrush(
            new LinearGradientBrushProperties { StartPoint = start, EndPoint = end },
            stopCollection);

        FillRoundedGeometry(target, rect, command.Radii, brush);
    }

    /// <summary>
    /// A plain rect uses the cheap axis-aligned clip only while the current coordinate system is
    /// axis-aligned. Any transformed clip uses a geometric layer: an axis-aligned Direct2D clip
    /// cannot represent a rotated or skewed CSS overflow clip. The scope records the transform
    /// that defined the geometry so an interrupted scene can rebuild it exactly.
    /// </summary>
    private void PushClip(
        ID2D1RenderTarget target,
        PushClipCommand command,
        Stack<ClipScope> clipStack,
        int transformDepth)
    {
        clipStack.Push(ApplyClip(target, command, target.Transform, transformDepth));
    }

    private ClipScope ApplyClip(
        ID2D1RenderTarget target,
        PushClipCommand command,
        Matrix3x2 transform,
        int transformDepth)
    {
        var previousTransform = target.Transform;
        target.Transform = transform;
        try
        {
            // PushAxisAlignedClip deliberately cannot encode rotation or skew. The geometric
            // path also handles a zero-radius rectangle, preserving the full affine clip shape.
            var usesLayer = !command.Radii.IsZero || transform != Matrix3x2.Identity;
            if (usesLayer)
            {
                using var geometry = CreateRoundedGeometry(command.Rectangle, command.Radii);
                var layerParameters = new LayerParameters
                {
                    // ContentBounds lives in the target's current coordinate space, just like
                    // draw operations. ApplyClip installs the saved world transform above, so it
                    // must remain local here; pre-transforming it would translate/rotate it a
                    // second time and can discard the entire layer.
                    ContentBounds = ToRawRect(command.Rectangle),
                    GeometricMask = geometry,
                    MaskAntialiasMode = AntialiasMode.PerPrimitive,
                    // Direct2D applies MaskTransform relative to the target's world transform.
                    // ApplyClip temporarily installs the saved world transform above, so an
                    // identity mask matrix maps the local geometry exactly once. Supplying the
                    // saved matrix here as well would transform the clip twice.
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

            return new ClipScope(command, transform, transformDepth, usesLayer);
        }
        finally
        {
            target.Transform = previousTransform;
        }
    }

    private static void PopClip(ID2D1RenderTarget target, ClipScope clip)
    {
        if (clip.UsesLayer)
        {
            target.PopLayer();
        }
        else
        {
            target.PopAxisAlignedClip();
        }
    }

    private static void SuspendClips(ID2D1RenderTarget target, Stack<ClipScope> clipStack)
    {
        foreach (var clip in clipStack)
        {
            PopClip(target, clip);
        }
    }

    private void RestoreClips(ID2D1RenderTarget target, Stack<ClipScope> clipStack)
    {
        foreach (var clip in clipStack.Reverse())
        {
            var restored = ApplyClip(target, clip.Command, clip.Transform, clip.TransformDepth);
            if (restored.UsesLayer != clip.UsesLayer)
            {
                throw new InvalidOperationException("A clip changed representation while restoring the Direct2D scene.");
            }
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
        if (_forcedDriverType is { } forcedDriverType)
        {
            CreateD3DDevice(forcedDriverType);
            CreateD2DAndSwapChain();
            return;
        }
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
        _scrollTileCache.Clear();
        ReleaseRetainedFrameCache();
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
        _scrollTileCache.Clear();
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

    private static Matrix3x2 ToDirect2DMatrix(TransformMatrix matrix) => new(
        matrix.M11,
        matrix.M12,
        matrix.M21,
        matrix.M22,
        matrix.Dx,
        matrix.Dy);

    /// <summary>Returns the device-space bounding box of an affine-transformed CSS rectangle.</summary>
    private static RectF TransformBounds(RectF rectangle, Matrix3x2 transform)
    {
        var topLeft = Vector2.Transform(new Vector2(rectangle.Left, rectangle.Top), transform);
        var topRight = Vector2.Transform(new Vector2(rectangle.Right, rectangle.Top), transform);
        var bottomLeft = Vector2.Transform(new Vector2(rectangle.Left, rectangle.Bottom), transform);
        var bottomRight = Vector2.Transform(new Vector2(rectangle.Right, rectangle.Bottom), transform);
        var left = MathF.Min(MathF.Min(topLeft.X, topRight.X), MathF.Min(bottomLeft.X, bottomRight.X));
        var top = MathF.Min(MathF.Min(topLeft.Y, topRight.Y), MathF.Min(bottomLeft.Y, bottomRight.Y));
        var right = MathF.Max(MathF.Max(topLeft.X, topRight.X), MathF.Max(bottomLeft.X, bottomRight.X));
        var bottom = MathF.Max(MathF.Max(topLeft.Y, topRight.Y), MathF.Max(bottomLeft.Y, bottomRight.Y));
        return new RectF(left, top, MathF.Max(0f, right - left), MathF.Max(0f, bottom - top));
    }

    private static RectF Intersect(RectF a, RectF b)
    {
        var left = MathF.Max(a.Left, b.Left);
        var top = MathF.Max(a.Top, b.Top);
        var right = MathF.Min(a.Right, b.Right);
        var bottom = MathF.Min(a.Bottom, b.Bottom);
        return RectF.FromEdges(left, top, right, bottom);
    }

    private static int FloatBits(float value) => BitConverter.SingleToInt32Bits(value);

    private Rectangle ToPixelRectangle(RectF rectangle)
    {
        var left = Math.Clamp((int)MathF.Floor(rectangle.Left * _targetDpiScale), 0, _targetPixelWidth);
        var top = Math.Clamp((int)MathF.Floor(rectangle.Top * _targetDpiScale), 0, _targetPixelHeight);
        var right = Math.Clamp((int)MathF.Ceiling(rectangle.Right * _targetDpiScale), 0, _targetPixelWidth);
        var bottom = Math.Clamp((int)MathF.Ceiling(rectangle.Bottom * _targetDpiScale), 0, _targetPixelHeight);
        return Rectangle.FromLTRB(left, top, right, bottom);
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

    private readonly record struct ScrollTileKey(
        long DisplayListGeneration,
        long ScrollContainerId,
        int DpiBits,
        int TilePixels,
        int TileX,
        int TileY);

    private sealed class ScrollTile : IDisposable
    {
        internal ScrollTile(ID2D1Bitmap1 bitmap)
        {
            Bitmap = bitmap;
            Bytes = checked(bitmap.PixelSize.Width * bitmap.PixelSize.Height * 4L);
        }

        internal ID2D1Bitmap1 Bitmap { get; }
        internal long Bytes { get; }

        public void Dispose() => Bitmap.Dispose();
    }

    private sealed class ScrollTileCache : IDisposable
    {
        internal const int TilePixelSize = 512;
        private const long BudgetBytes = 64L * 1024L * 1024L;
        private readonly Dictionary<ScrollTileKey, LinkedListNode<Entry>> _entries = new();
        private readonly LinkedList<Entry> _lru = new();

        internal long Bytes { get; private set; }

        internal bool TryGet(ScrollTileKey key, out ScrollTile tile)
        {
            if (_entries.TryGetValue(key, out var node))
            {
                _lru.Remove(node);
                _lru.AddFirst(node);
                tile = node.Value.Tile;
                return true;
            }

            tile = null!;
            return false;
        }

        internal int TryAdd(ScrollTileKey key, ScrollTile tile)
        {
            if (tile.Bytes > BudgetBytes)
            {
                tile.Dispose();
                return 0;
            }

            if (_entries.TryGetValue(key, out var existing))
            {
                Bytes -= existing.Value.Tile.Bytes;
                existing.Value.Tile.Dispose();
                _lru.Remove(existing);
                _entries.Remove(key);
            }

            var node = new LinkedListNode<Entry>(new Entry(key, tile));
            _lru.AddFirst(node);
            _entries.Add(key, node);
            Bytes += tile.Bytes;

            var evictions = 0;
            while (Bytes > BudgetBytes && _lru.Last is { } last)
            {
                _lru.RemoveLast();
                _entries.Remove(last.Value.Key);
                Bytes -= last.Value.Tile.Bytes;
                last.Value.Tile.Dispose();
                evictions++;
            }

            return evictions;
        }

        internal void Clear()
        {
            foreach (var entry in _lru)
            {
                entry.Tile.Dispose();
            }

            _entries.Clear();
            _lru.Clear();
            Bytes = 0;
        }

        public void Dispose() => Clear();

        private readonly record struct Entry(ScrollTileKey Key, ScrollTile Tile);
    }

    private sealed class ScrollTileRenderMetrics
    {
        internal int CacheHits { get; private set; }
        internal int CacheMisses { get; private set; }
        internal int RasterizedCommands { get; private set; }
        internal int Evictions { get; private set; }
        internal int Bypasses { get; private set; }

        internal void Reset()
        {
            CacheHits = 0;
            CacheMisses = 0;
            RasterizedCommands = 0;
            Evictions = 0;
            Bypasses = 0;
        }

        internal void RecordCacheHit() => CacheHits++;
        internal void RecordCacheMiss() => CacheMisses++;
        internal void RecordRasterizedCommands(int count) => RasterizedCommands += Math.Max(0, count);
        internal void RecordEvictions(int count) => Evictions += Math.Max(0, count);
        internal void RecordBypass() => Bypasses++;
    }
}
