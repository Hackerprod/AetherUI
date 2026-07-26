using System.Drawing;
using System.Numerics;
using AetherUI.Css;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using Vortice;
using Vortice.Direct2D1;
using Vortice.DirectWrite;
using Vortice.Mathematics;
using DWriteFontWeight = Vortice.DirectWrite.FontWeight;
using DWriteTextAlignment = Vortice.DirectWrite.TextAlignment;
using UiFontWeight = AetherUI.Css.FontWeight;
using UiTextAlign = AetherUI.Css.TextAlign;
using UiTextMetrics = AetherUI.Layout.TextMetrics;

namespace AetherUI.Windows.Graphics;

/// <summary>
/// Direct2D/DirectWrite implementation of AetherUI's retained display-list backend.
/// The HWND render target uses hardware acceleration when the Windows graphics stack permits it.
/// </summary>
public sealed class Direct2DRenderBackend : IRenderBackend, ITextMeasurer
{
    private readonly nint _windowHandle;
    private readonly ID2D1Factory _d2dFactory;
    private readonly IDWriteFactory _writeFactory;
    private readonly Dictionary<ColorRgba, ID2D1SolidColorBrush> _brushes = new();
    private readonly Dictionary<TextFormatKey, IDWriteTextFormat> _textFormats = new();
    private ID2D1HwndRenderTarget? _renderTarget;
    private int _pixelWidth;
    private int _pixelHeight;
    private float _dpiScale;
    private bool _disposed;

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

        var d2dFactory = D2D1.D2D1CreateFactory<ID2D1Factory>();
        IDWriteFactory? writeFactory = null;
        try
        {
            writeFactory = DWrite.DWriteCreateFactory<IDWriteFactory>();
            _d2dFactory = d2dFactory;
            _writeFactory = writeFactory;
            CreateRenderTarget();
        }
        catch
        {
            _renderTarget?.Dispose();
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
        var measuredWidth = MathF.Max(0, metrics.WidthIncludingTrailingWhitespace);
        var measuredHeight = MathF.Max(style.FontSize * style.LineHeight, metrics.Height);
        return new UiTextMetrics(measuredWidth, measuredHeight, style.FontSize * .8f);
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
        EnsureRenderTarget();
        _renderTarget!.Dpi = new Vortice.Mathematics.Size(96f * _dpiScale, 96f * _dpiScale);
        _renderTarget.Resize(new SizeI(_pixelWidth, _pixelHeight));
    }

    public void Render(DisplayList displayList, ColorRgba clearColor)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ArgumentNullException.ThrowIfNull(displayList);
        RenderCore(displayList, clearColor, allowDeviceRetry: true);
    }

    private void RenderCore(DisplayList displayList, ColorRgba clearColor, bool allowDeviceRetry)
    {
        EnsureRenderTarget();

        var target = _renderTarget!;
        target.BeginDraw();
        target.Transform = Matrix3x2.Identity;
        target.Clear(ToColor4(clearColor));

        var clipStack = new Stack<bool>();
        foreach (var command in displayList.Commands)
        {
            switch (command)
            {
                case FillRoundedRectangleCommand fill:
                    target.FillRoundedRectangle(ToRoundedRectangle(fill.Rectangle, fill.Radius), GetBrush(fill.Color));
                    break;
                case FillRoundedRectangleGradientCommand gradientFill:
                    DrawGradientFill(target, gradientFill);
                    break;
                case DrawBoxShadowCommand shadow:
                    DrawBoxShadow(target, shadow);
                    break;
                case StrokeRoundedRectangleCommand stroke:
                    target.DrawRoundedRectangle(
                        ToRoundedRectangle(stroke.Rectangle, stroke.Radius),
                        GetBrush(stroke.Color),
                        MathF.Max(.25f, stroke.StrokeWidth));
                    break;
                case DrawTextCommand text:
                    DrawText(target, text);
                    break;
                case DrawTextGradientCommand textGradient:
                    DrawTextGradient(target, textGradient);
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

        var result = target.EndDraw();
        if (result.Failure)
        {
            ReleaseRenderTarget();
            if (allowDeviceRetry)
            {
                CreateRenderTarget();
                RenderCore(displayList, clearColor, allowDeviceRetry: false);
                return;
            }

            throw new InvalidOperationException($"Direct2D EndDraw falló después de recrear el render target: {result}.");
        }
    }

    private void DrawText(ID2D1HwndRenderTarget target, DrawTextCommand command)
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
            verticalCenter: true);
        var rectangle = ToRect(command.Rectangle);
        target.DrawText(command.Text, format, rectangle, GetBrush(command.Color), DrawTextOptions.Clip);
    }

    private void DrawTextGradient(ID2D1HwndRenderTarget target, DrawTextGradientCommand command)
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
            verticalCenter: true);

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
            UiTextAlign.End => DWriteTextAlignment.Trailing,
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
        var shadow = command.Shadow;
        if (shadow.Color.A <= 0)
        {
            return;
        }

        var baseRect = command.BorderBoxRectangle
            .Inflate(shadow.SpreadRadius)
            .Offset(shadow.OffsetX, shadow.OffsetY);
        var baseRadius = MathF.Max(0, command.Radius + shadow.SpreadRadius);

        if (shadow.BlurRadius <= 0.5f)
        {
            target.FillRoundedRectangle(ToRoundedRectangle(baseRect, baseRadius), GetBrush(shadow.Color));
            return;
        }

        const int steps = 8;
        for (var i = steps; i >= 1; i--)
        {
            var t = i / (float)steps;
            var expand = shadow.BlurRadius * t;
            // Roughly gaussian-shaped falloff: strongest near the box edge, tapering to ~0 at the
            // full blur radius, split across `steps` concentric layers so no single ring reads as
            // a hard band.
            var alpha = shadow.Color.A * (1f - t) * (1f - t) / steps * 2.2f;
            if (alpha <= 0.002f)
            {
                continue;
            }

            var rect = baseRect.Inflate(expand);
            var radius = baseRadius + expand;
            target.FillRoundedRectangle(ToRoundedRectangle(rect, radius), GetBrush(shadow.Color.WithAlpha(MathF.Min(1f, alpha))));
        }
    }

    private static void DrawGradientFill(ID2D1RenderTarget target, FillRoundedRectangleGradientCommand command)
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

        target.FillRoundedRectangle(ToRoundedRectangle(rect, command.Radius), brush);
    }

    /// <summary>
    /// A plain rect uses the cheap axis-aligned clip; a rounded one needs a geometry-masked layer,
    /// since Direct2D's axis-aligned clip has no notion of corner radius. <paramref name="clipStack"/>
    /// records which kind was pushed so <see cref="PopClip"/> pairs it with the matching pop call.
    /// </summary>
    private void PushClip(ID2D1HwndRenderTarget target, PushClipCommand command, Stack<bool> clipStack)
    {
        if (command.Radius > 0.5f)
        {
            using var geometry = _d2dFactory.CreateRoundedRectangleGeometry(
                ToRoundedRectangle(command.Rectangle, command.Radius));
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
            clipStack.Push(true);
        }
        else
        {
            target.PushAxisAlignedClip(ToRawRect(command.Rectangle), AntialiasMode.PerPrimitive);
            clipStack.Push(false);
        }
    }

    private static void PopClip(ID2D1HwndRenderTarget target, bool wasLayer)
    {
        if (wasLayer)
        {
            target.PopLayer();
        }
        else
        {
            target.PopAxisAlignedClip();
        }
    }

    private void EnsureRenderTarget()
    {
        if (_renderTarget is null)
        {
            CreateRenderTarget();
        }
    }

    private void CreateRenderTarget()
    {
        var properties = new HwndRenderTargetProperties
        {
            Hwnd = _windowHandle,
            PixelSize = new SizeI(Math.Max(1, _pixelWidth), Math.Max(1, _pixelHeight)),
            PresentOptions = PresentOptions.None,
        };
        _renderTarget = _d2dFactory.CreateHwndRenderTarget(new RenderTargetProperties(), properties);
        _renderTarget.Dpi = new Vortice.Mathematics.Size(96f * _dpiScale, 96f * _dpiScale);
        _renderTarget.AntialiasMode = AntialiasMode.PerPrimitive;
        // ClearType ("Default") assumes a light background and applies per-subpixel RGB color
        // correction — on an arbitrary (often dark) UI background color it produces visible red/
        // blue fringing on glyph edges, which can look like clipped/missing ink on thin strokes.
        // Grayscale antialiasing has no background-color assumption, matching a themeable UI.
        _renderTarget.TextAntialiasMode = Vortice.Direct2D1.TextAntialiasMode.Grayscale;
    }

    private void ReleaseRenderTarget()
    {
        foreach (var brush in _brushes.Values)
        {
            brush.Dispose();
        }

        _brushes.Clear();
        _renderTarget?.Dispose();
        _renderTarget = null;
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

        ReleaseRenderTarget();
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
