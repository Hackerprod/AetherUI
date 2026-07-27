using AetherUI.Dom;
using AetherUI.Core.Native;
using AetherUI.Primitives;

namespace AetherUI.Css;

/// <summary>
/// Converts the native style runtime's flat <see cref="NativeComputedStyle"/> (parsing, cascade,
/// selectors, inheritance, custom properties, media queries) into this engine's own
/// <see cref="ComputedStyle"/>, which remains the managed projection consumed by host-facing layout
/// and display-list models.
/// </summary>
/// <remarks>
/// The native runtime understands more of CSS than this engine's layout/paint code can act on yet
/// (per-axis flex wrap/basis/order, <c>position</c>, per-side border style/color, multiple box-
/// shadows, transforms). Those values are parsed correctly natively but have no field to land in
/// here — they are intentionally dropped at this boundary, not silently mis-approximated. Growing
/// this bridge (and the matching <see cref="ComputedStyle"/> fields) is how those capabilities get
/// wired up later, one family at a time, without touching parsing/cascade/selectors again.
/// </remarks>
public static class StyleRuntimeBridge
{
    public static ComputedStyle ToComputedStyle(NativeComputedStyle source, ElementNode element)
    {
        var backgroundGradient = ToLinearGradient(source.BackgroundLayers
            .FirstOrDefault(static layer => layer.Kind == 0)?.Gradient);
        var textGradient = ToLinearGradient(source.TextGradient);
        var style = new ComputedStyle
        {
            Display = element.TagName == "br" ? DisplayKind.LineBreak : (DisplayKind)source.Display,
            FlexDirection = (FlexDirection)source.FlexDirection,
            JustifyContent = (JustifyContent)source.JustifyContent,
            AlignItems = (AlignItems)source.AlignItems,
            FlexGrow = source.FlexGrow,
            FlexWrap = source.FlexWrap,
            Gap = source.Gap,
            Position = (PositionKind)source.Position,
            Inset = ToCssEdges(source.Inset),
            Width = ToCssLength(source.Width),
            Height = ToCssLength(source.Height),
            MinWidth = ToCssLength(source.MinWidth),
            MinHeight = ToCssLength(source.MinHeight),
            MaxWidth = ToCssLength(source.MaxWidth),
            MaxHeight = ToCssLength(source.MaxHeight),
            Margin = ToCssEdges(source.Margin),
            Padding = ToCssEdges(source.Padding),
            BorderWidth = ToCssEdges(source.BorderWidth),
            BoxSizing = (BoxSizing)source.BoxSizing,
            Color = ToColorRgba(source.Color),
            BackgroundColor = ToColorRgba(source.BackgroundColor),
            BackgroundGradient = backgroundGradient,
            TextGradient = textGradient,
            TextShadows = source.TextShadows.Select(static shadow => new BoxShadow(
                shadow.X, shadow.Y, shadow.Blur, shadow.Spread, ToColorRgba(shadow.Color), shadow.Inset)).ToArray(),
            BorderColor = ToColorRgba(source.BorderColor),
            BorderColors = ToColorEdges(source.BorderColors),
            BorderStyles = ToBorderStyleEdges(source.BorderStyles),
            BorderRadius = ToCssLength(source.BorderRadii.TopLeft.Horizontal),
            BorderRadii = ToBorderRadii(source.BorderRadii),
            Opacity = source.Opacity,
            BoxShadows = source.BoxShadows
                .Where(static shadow => !shadow.Inset)
                .Select(static shadow => new BoxShadow(
                    shadow.X, shadow.Y, shadow.Blur, shadow.Spread, ToColorRgba(shadow.Color), false))
                .ToArray(),
            FontFamily = source.FontFamily,
            FontSize = source.FontSize,
            FontWeight = (FontWeight)source.FontWeight,
            LineHeight = source.LineHeight,
            TextAlign = (TextAlign)source.TextAlign,
            TextTransform = (TextTransform)source.TextTransform,
            Overflow = (OverflowKind)source.Overflow,
            Cursor = (CursorKind)source.Cursor,
            Visible = source.Visible,
        };
        if (source.BoxShadows.Any(static shadow => shadow.Inset))
        {
            CssDiagnostics.ReportUnknownProperty("box-shadow", "<inset, not yet painted>");
        }
        return style;
    }

    private static ColorRgba ToColorRgba(NativeColor color) => new(color.R, color.G, color.B, color.A);

    private static ColorEdges ToColorEdges(NativeColorEdges edges) => new(
        ToColorRgba(edges.Left), ToColorRgba(edges.Top), ToColorRgba(edges.Right), ToColorRgba(edges.Bottom));

    private static BorderStyleEdges ToBorderStyleEdges(NativeBorderStyleEdges edges) => new(
        (BorderStyle)edges.Left, (BorderStyle)edges.Top, (BorderStyle)edges.Right, (BorderStyle)edges.Bottom);

    private static CssEdges ToCssEdges(NativeEdges edges) => new(
        ToCssLength(edges.Left), ToCssLength(edges.Top), ToCssLength(edges.Right), ToCssLength(edges.Bottom));

    private static CssBorderRadii ToBorderRadii(NativeBorderRadii radii) => new(
        ToCornerRadius(radii.TopLeft), ToCornerRadius(radii.TopRight),
        ToCornerRadius(radii.BottomRight), ToCornerRadius(radii.BottomLeft));

    private static CssCornerRadius ToCornerRadius(NativeCornerRadius radius) => new(
        ToCssLength(radius.Horizontal), ToCssLength(radius.Vertical));

    private static CssLength ToCssLength(NativeLength length) => length.Unit switch
    {
        0 => CssLength.Auto,
        1 => new CssLength(length.Value, CssUnit.Pixels),
        2 => new CssLength(length.Value, CssUnit.Percent),
        3 => new CssLength(length.Value, CssUnit.Em),
        4 => new CssLength(length.Value, CssUnit.Rem),
        5 => new CssLength(length.Value, CssUnit.ViewportWidth),
        6 => new CssLength(length.Value, CssUnit.ViewportHeight),
        7 => CssLength.Linear(length.Value, length.Percent, length.Em, length.Rem, length.Vw, length.Vh),
        _ => CssLength.Auto,
    };

    private static LinearGradient? ToLinearGradient(NativeGradient? gradient) =>
        gradient is null ? null : new LinearGradient(
            gradient.DirectionX, gradient.DirectionY,
            gradient.Stops.Select(stop => new GradientStop(ToColorRgba(stop.Color), stop.Offset)).ToArray());
}
