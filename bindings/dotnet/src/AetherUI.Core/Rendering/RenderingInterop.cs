using System.Text;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Layout;
using AetherUI.Primitives;

namespace AetherUI.Rendering;

/// <summary>Decodes the native tagged display-list ABI into the renderer's existing command model.</summary>
internal sealed class RenderingInterop : IDisposable
{
    private readonly ITextMeasurer _textMeasurer;
    private readonly NativeDisplayListInterop _native;

    public RenderingInterop(ITextMeasurer textMeasurer, Func<string, (float Width, float Height)?>? imageDimensions = null)
    {
        _textMeasurer = textMeasurer ?? throw new ArgumentNullException(nameof(textMeasurer));
        _native = new NativeDisplayListInterop(Measure, imageDimensions);
    }

    public DisplayList Build(NativeLayoutResult layout, IntPtr focusedElement) =>
        new(_native.Build(layout, focusedElement).Select(Decode).ToArray());

    public void Dispose() => _native.Dispose();

    private PaintCommand Decode(NativePaintCommand command)
    {
        var bytes = command.Bytes;
        return command.Tag switch
        {
            0 => new FillRoundedRectangleCommand(Rect(bytes, NativePaintCommandLayout.FillRect), Radii(bytes, NativePaintCommandLayout.FillRadii), Color(bytes, NativePaintCommandLayout.FillColor)),
            1 => new DrawBoxShadowCommand(Rect(bytes, NativePaintCommandLayout.ShadowRect), Radii(bytes, NativePaintCommandLayout.ShadowRadii), new BoxShadow(
                Float(bytes, NativePaintCommandLayout.ShadowOffsetX), Float(bytes, NativePaintCommandLayout.ShadowOffsetX + 4), Float(bytes, NativePaintCommandLayout.ShadowOffsetX + 8), Float(bytes, NativePaintCommandLayout.ShadowOffsetX + 12), Color(bytes, NativePaintCommandLayout.ShadowColor), Int(bytes, NativePaintCommandLayout.ShadowInset) != 0)),
            2 => new FillRoundedRectangleGradientCommand(
                Rect(bytes, NativePaintCommandLayout.GradientRect),
                Radii(bytes, NativePaintCommandLayout.GradientRadii),
                Rect(bytes, NativePaintCommandLayout.GradientCoordinateRect),
                Gradient(command), Float(bytes, NativePaintCommandLayout.GradientOpacity)),
            3 => new StrokeRoundedRectangleCommand(Rect(bytes, NativePaintCommandLayout.StrokeRect), Radii(bytes, NativePaintCommandLayout.StrokeRadii), Float(bytes, NativePaintCommandLayout.StrokeWidth), Color(bytes, NativePaintCommandLayout.StrokeColor)),
            4 => new DrawTextCommand(command.Text ?? string.Empty, Rect(bytes, NativePaintCommandLayout.TextRect), Family(bytes, NativePaintCommandLayout.TextFamily), Float(bytes, NativePaintCommandLayout.TextSize),
                (FontWeight)Int(bytes, NativePaintCommandLayout.TextWeight), (TextAlign)Int(bytes, NativePaintCommandLayout.TextAlignment), Color(bytes, NativePaintCommandLayout.TextColor), Int(bytes, NativePaintCommandLayout.TextWrap) != 0),
            5 => new DrawTextGradientCommand(command.Text ?? string.Empty, Rect(bytes, NativePaintCommandLayout.TextRect), Family(bytes, NativePaintCommandLayout.TextFamily), Float(bytes, NativePaintCommandLayout.TextSize),
                (FontWeight)Int(bytes, NativePaintCommandLayout.TextWeight), (TextAlign)Int(bytes, NativePaintCommandLayout.TextAlignment), Gradient(command), Float(bytes, NativePaintCommandLayout.TextGradientOpacity)),
            6 => new PushClipCommand(Rect(bytes, NativePaintCommandLayout.ClipRect), Radii(bytes, NativePaintCommandLayout.ClipRadii)),
            7 => new PopClipCommand(),
            8 => new FillBorderSideCommand(Rect(bytes, NativePaintCommandLayout.BorderOuterRect), Radii(bytes, NativePaintCommandLayout.BorderOuterRadii), Rect(bytes, NativePaintCommandLayout.BorderInnerRect), Radii(bytes, NativePaintCommandLayout.BorderInnerRadii),
                Int(bytes, NativePaintCommandLayout.BorderSide), Int(bytes, NativePaintCommandLayout.BorderStyle), Color(bytes, NativePaintCommandLayout.BorderColor)),
            9 => new DrawTextShadowCommand(command.Text ?? string.Empty, Rect(bytes, NativePaintCommandLayout.TextRect), Family(bytes, NativePaintCommandLayout.TextFamily), Float(bytes, NativePaintCommandLayout.TextSize),
                (FontWeight)Int(bytes, NativePaintCommandLayout.TextWeight), (TextAlign)Int(bytes, NativePaintCommandLayout.TextAlignment), new BoxShadow(
                    Float(bytes, NativePaintCommandLayout.TextShadowOffsetX), Float(bytes, NativePaintCommandLayout.TextShadowOffsetX + 4),
                    Float(bytes, NativePaintCommandLayout.TextShadowOffsetX + 8), Float(bytes, NativePaintCommandLayout.TextShadowOffsetX + 12),
                    Color(bytes, NativePaintCommandLayout.TextShadowColor), Int(bytes, NativePaintCommandLayout.TextShadowInset) != 0),
                Int(bytes, NativePaintCommandLayout.TextShadowWrap) != 0),
            10 => new DrawBackgroundImageCommand(
                command.ImageSource ?? string.Empty,
                Rect(bytes, NativePaintCommandLayout.BackgroundImageRect),
                Length(bytes, NativePaintCommandLayout.BackgroundImagePositionX),
                Length(bytes, NativePaintCommandLayout.BackgroundImagePositionY),
                (BackgroundImageSizeKind)Int(bytes, NativePaintCommandLayout.BackgroundImageSizeKind),
                Length(bytes, NativePaintCommandLayout.BackgroundImageSizeX),
                Length(bytes, NativePaintCommandLayout.BackgroundImageSizeY),
                (BackgroundRepeat)Int(bytes, NativePaintCommandLayout.BackgroundImageRepeatX),
                (BackgroundRepeat)Int(bytes, NativePaintCommandLayout.BackgroundImageRepeatY),
                Float(bytes, NativePaintCommandLayout.BackgroundImageOpacity),
                Rect(bytes, NativePaintCommandLayout.BackgroundImageSourceRect),
                Rect(bytes, NativePaintCommandLayout.BackgroundImageDestinationRect),
                Int(bytes, NativePaintCommandLayout.BackgroundImageHasResolvedGeometry) != 0),
            11 => new ApplyBackdropFilterCommand(
                Rect(bytes, NativePaintCommandLayout.BackdropRect),
                Radii(bytes, NativePaintCommandLayout.BackdropRadii),
                command.BackdropFilters?.Select(filter => new BackdropFilter(
                    (BackdropFilterKind)filter.Kind,
                    new CssLength(filter.LengthAmount.Value, (CssUnit)filter.LengthAmount.Unit,
                        filter.LengthAmount.Percent, filter.LengthAmount.Em, filter.LengthAmount.Rem,
                        filter.LengthAmount.Vw, filter.LengthAmount.Vh),
                    filter.ScalarAmount)).ToArray() ?? []),
            12 => new PushTransformCommand(new TransformMatrix(
                Float(bytes, NativePaintCommandLayout.TransformMatrix), Float(bytes, NativePaintCommandLayout.TransformMatrix + 4),
                Float(bytes, NativePaintCommandLayout.TransformMatrix + 8), Float(bytes, NativePaintCommandLayout.TransformMatrix + 12),
                Float(bytes, NativePaintCommandLayout.TransformMatrix + 16), Float(bytes, NativePaintCommandLayout.TransformMatrix + 20))),
            13 => new PopTransformCommand(),
            14 => new PushScrollOffsetCommand(Long(bytes, NativePaintCommandLayout.ScrollContainerId)),
            15 => new PopScrollOffsetCommand(),
            16 => new DrawScrollbarChromeCommand(Long(bytes, NativePaintCommandLayout.ScrollContainerId)),
            _ => throw new InvalidOperationException($"Unknown native paint command tag {command.Tag}."),
        };
    }

    private static long Long(byte[] bytes, int offset) => BitConverter.ToInt64(bytes, offset);

    private NativeTextMetrics Measure(string text, NativeComputedStyle style, float maxWidth)
    {
        // Native layout/display code passes zero for an unconstrained measurement; the managed
        // contract represents that with PositiveInfinity instead of a literal zero-width line.
        var metrics = _textMeasurer.MeasureText(text,
            StyleRuntimeBridge.ToComputedStyle(style, new AetherUI.Dom.ElementNode("span")),
            maxWidth > 0 ? maxWidth : float.PositiveInfinity);
        return new NativeTextMetrics(metrics.Width, metrics.Height, metrics.Baseline);
    }

    private static float Float(byte[] bytes, int offset) => BitConverter.ToSingle(bytes, offset);
    private static int Int(byte[] bytes, int offset) => BitConverter.ToInt32(bytes, offset);
    private static CssLength Length(byte[] bytes, int offset) => new(
        Float(bytes, offset),
        (CssUnit)Int(bytes, offset + 4),
        Float(bytes, offset + 8), Float(bytes, offset + 12), Float(bytes, offset + 16),
        Float(bytes, offset + 20), Float(bytes, offset + 24));
    private static RectF Rect(byte[] bytes, int offset) => new(Float(bytes, offset), Float(bytes, offset + 4), Float(bytes, offset + 8), Float(bytes, offset + 12));
    private static ColorRgba Color(byte[] bytes, int offset) => new(Float(bytes, offset), Float(bytes, offset + 4), Float(bytes, offset + 8), Float(bytes, offset + 12));
    private static CornerRadii Radii(byte[] bytes, int offset) => new(
        Float(bytes, offset), Float(bytes, offset + 4), Float(bytes, offset + 8), Float(bytes, offset + 12),
        Float(bytes, offset + 16), Float(bytes, offset + 20), Float(bytes, offset + 24), Float(bytes, offset + 28));
    private static string Family(byte[] bytes, int offset)
    {
        var length = Array.IndexOf(bytes, (byte)0, offset, 64);
        return Encoding.UTF8.GetString(bytes, offset, (length < 0 ? offset + 64 : length) - offset);
    }

    private static LinearGradient Gradient(NativePaintCommand command)
    {
        var gradient = command.Gradient ?? throw new InvalidOperationException("Native gradient command has no copied gradient.");
        return new LinearGradient(gradient.DirectionX, gradient.DirectionY,
            gradient.Stops.Select(stop => new GradientStop(new ColorRgba(stop.Color.R, stop.Color.G, stop.Color.B, stop.Color.A), stop.Offset)).ToArray());
    }
}
