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
public sealed partial class Direct2DRenderBackend : IRenderBackendWithText, IIncrementalRenderBackend
{
    private readonly nint _windowHandle;
    private readonly DriverType? _forcedDriverType;
    private readonly ID2D1Factory1 _d2dFactory;
    private readonly IDWriteFactory2 _writeFactory;
    private readonly Dictionary<ColorRgba, ID2D1SolidColorBrush> _brushes = new();
    private readonly Dictionary<TextFormatKey, IDWriteTextFormat> _textFormats = new();
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
    private readonly bool _incrementalDebug;

    private int _lastTransformPushes;
    private int _lastTransformPops;

    // A clip is expressed in the coordinate space that was active when it was pushed. Keeping
    // that matrix with the clip is essential when a text-shadow cache miss or backdrop snapshot
    // temporarily closes and rebuilds the Direct2D draw segment: restoring the clip under the
    // transform current at that later point would change its shape and position.
    private readonly record struct ClipScope(
        PushClipCommand Command,
        Matrix3x2 Transform,
        int TransformDepth,
        bool UsesLayer);

    internal int LastTransformPushes => _lastTransformPushes;
    internal int LastTransformPops => _lastTransformPops;

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

        var pixels = new byte[checked(_targetPixelWidth * _targetPixelHeight * 4)];
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

        InvalidateDiagnosticCaptureResult();
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
        CheckDiagnosticRenderThread();
        try
        {
            RenderCore(displayList, clearColor, allowDeviceRetry: true);
            CompleteDiagnosticRender();
        }
        catch
        {
            InvalidateDiagnosticCaptureResult();
            throw;
        }
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

        CopyDiagnosticCandidate();

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
        InvalidateDiagnosticCaptureResult();
        _diagnosticDeviceGeneration++;
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
