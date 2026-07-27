using AetherUI.Css;
using AetherUI.Primitives;

namespace AetherUI.Rendering;

public abstract record PaintCommand;

public readonly record struct CornerRadii(
    float TopLeftX, float TopLeftY, float TopRightX, float TopRightY,
    float BottomRightX, float BottomRightY, float BottomLeftX, float BottomLeftY)
{
    public static readonly CornerRadii Zero = default;
    public static CornerRadii Uniform(float radius) => new(radius, radius, radius, radius, radius, radius, radius, radius);
    public bool IsZero => TopLeftX <= .5f && TopLeftY <= .5f && TopRightX <= .5f && TopRightY <= .5f &&
                          BottomRightX <= .5f && BottomRightY <= .5f && BottomLeftX <= .5f && BottomLeftY <= .5f;
}

public sealed record FillRoundedRectangleCommand(RectF Rectangle, CornerRadii Radii, ColorRgba Color) : PaintCommand;
public sealed record DrawBoxShadowCommand(RectF BorderBoxRectangle, CornerRadii Radii, BoxShadow Shadow) : PaintCommand;
public sealed record FillRoundedRectangleGradientCommand(RectF Rectangle, CornerRadii Radii, LinearGradient Gradient, float Opacity) : PaintCommand;
public sealed record StrokeRoundedRectangleCommand(RectF Rectangle, CornerRadii Radii, float StrokeWidth, ColorRgba Color) : PaintCommand;
public sealed record FillBorderSideCommand(
    RectF OuterRectangle, CornerRadii OuterRadii,
    RectF InnerRectangle, CornerRadii InnerRadii,
    int Side, int Style, ColorRgba Color) : PaintCommand;
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
public sealed record DrawTextShadowCommand(
    string Text,
    RectF Rectangle,
    string FontFamily,
    float FontSize,
    FontWeight FontWeight,
    TextAlign Alignment,
    BoxShadow Shadow,
    bool Wrap = false) : PaintCommand;
public enum BackgroundImageSizeKind { Auto, Explicit, Cover, Contain }
public enum BackgroundRepeat { Repeat, NoRepeat, Space, Round }
public enum BackdropFilterKind { Blur, Brightness, Contrast, Saturate }
public readonly record struct BackdropFilter(BackdropFilterKind Kind, CssLength LengthAmount, float ScalarAmount);
public sealed record ApplyBackdropFilterCommand(
    RectF Rectangle, CornerRadii Radii, IReadOnlyList<BackdropFilter> Filters) : PaintCommand;
public sealed record DrawBackgroundImageCommand(
    string Source,
    RectF PaintRectangle,
    CssLength PositionX,
    CssLength PositionY,
    BackgroundImageSizeKind SizeKind,
    CssLength SizeX,
    CssLength SizeY,
    BackgroundRepeat RepeatX,
    BackgroundRepeat RepeatY,
    float Opacity,
    RectF SourceRectangle,
    RectF DestinationRectangle,
    bool HasResolvedGeometry,
    AetherUI.Runtime.ImageResourceResult? Resource = null) : PaintCommand;
public sealed record PushClipCommand(RectF Rectangle, CornerRadii Radii) : PaintCommand;
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
