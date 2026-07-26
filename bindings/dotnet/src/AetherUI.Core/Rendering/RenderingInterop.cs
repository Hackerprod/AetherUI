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

    public RenderingInterop(ITextMeasurer textMeasurer)
    {
        _textMeasurer = textMeasurer ?? throw new ArgumentNullException(nameof(textMeasurer));
        _native = new NativeDisplayListInterop(Measure);
    }

    public DisplayList Build(NativeLayoutResult layout, IntPtr focusedElement) =>
        new(_native.Build(layout, focusedElement).Select(Decode).ToArray());

    public void Dispose() => _native.Dispose();

    private PaintCommand Decode(NativePaintCommand command)
    {
        var bytes = command.Bytes;
        return command.Tag switch
        {
            0 => new FillRoundedRectangleCommand(Rect(bytes, 8), Float(bytes, 24), Color(bytes, 28)),
            1 => new DrawBoxShadowCommand(Rect(bytes, 8), Float(bytes, 24), new BoxShadow(
                Float(bytes, 28), Float(bytes, 32), Float(bytes, 36), Float(bytes, 40), Color(bytes, 44), Int(bytes, 60) != 0)),
            2 => new FillRoundedRectangleGradientCommand(Rect(bytes, 8), Float(bytes, 24), Gradient(command), Float(bytes, 56)),
            3 => new StrokeRoundedRectangleCommand(Rect(bytes, 8), Float(bytes, 24), Float(bytes, 28), Color(bytes, 32)),
            4 => new DrawTextCommand(command.Text ?? string.Empty, Rect(bytes, 16), Family(bytes, 32), Float(bytes, 96),
                (FontWeight)Int(bytes, 100), (TextAlign)Int(bytes, 104), Color(bytes, 108), Int(bytes, 124) != 0),
            5 => new DrawTextGradientCommand(command.Text ?? string.Empty, Rect(bytes, 16), Family(bytes, 32), Float(bytes, 96),
                (FontWeight)Int(bytes, 100), (TextAlign)Int(bytes, 104), Gradient(command), Float(bytes, 136)),
            6 => new PushClipCommand(Rect(bytes, 8), Float(bytes, 24)),
            7 => new PopClipCommand(),
            _ => throw new InvalidOperationException($"Unknown native paint command tag {command.Tag}."),
        };
    }

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
    private static RectF Rect(byte[] bytes, int offset) => new(Float(bytes, offset), Float(bytes, offset + 4), Float(bytes, offset + 8), Float(bytes, offset + 12));
    private static ColorRgba Color(byte[] bytes, int offset) => new(Float(bytes, offset), Float(bytes, offset + 4), Float(bytes, offset + 8), Float(bytes, offset + 12));
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
