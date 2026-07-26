using AetherUI.Css;
using AetherUI.Primitives;

namespace AetherUI.Rendering;

public abstract record PaintCommand;

public sealed record FillRoundedRectangleCommand(RectF Rectangle, float Radius, ColorRgba Color) : PaintCommand;
public sealed record DrawBoxShadowCommand(RectF BorderBoxRectangle, float Radius, BoxShadow Shadow) : PaintCommand;
public sealed record FillRoundedRectangleGradientCommand(RectF Rectangle, float Radius, LinearGradient Gradient, float Opacity) : PaintCommand;
public sealed record StrokeRoundedRectangleCommand(RectF Rectangle, float Radius, float StrokeWidth, ColorRgba Color) : PaintCommand;
public sealed record DrawTextCommand(
    string Text,
    RectF Rectangle,
    string FontFamily,
    float FontSize,
    FontWeight FontWeight,
    TextAlign Alignment,
    ColorRgba Color,
    bool Wrap = false) : PaintCommand;
public sealed record DrawTextGradientCommand(
    string Text,
    RectF Rectangle,
    string FontFamily,
    float FontSize,
    FontWeight FontWeight,
    TextAlign Alignment,
    LinearGradient Gradient,
    float Opacity) : PaintCommand;
public sealed record PushClipCommand(RectF Rectangle, float Radius = 0f) : PaintCommand;
public sealed record PopClipCommand : PaintCommand;

public sealed class DisplayList
{
    public DisplayList(IReadOnlyList<PaintCommand> commands)
    {
        Commands = commands ?? throw new ArgumentNullException(nameof(commands));
    }

    public IReadOnlyList<PaintCommand> Commands { get; }
    public int Count => Commands.Count;
}

public interface IRenderBackend : IDisposable
{
    void Resize(int pixelWidth, int pixelHeight, float dpiScale);
    void Render(DisplayList displayList, ColorRgba clearColor);
}
