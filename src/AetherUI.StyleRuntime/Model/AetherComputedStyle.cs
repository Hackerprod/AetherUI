using System.Collections.ObjectModel;

namespace AetherUI.StyleRuntime;

public sealed record LayoutComputedStyle(
    DisplayMode Display,
    PositionMode Position,
    VisibilityMode Visibility,
    BoxSizingMode BoxSizing,
    OverflowMode OverflowX,
    OverflowMode OverflowY,
    StyleLength Width,
    StyleLength Height,
    StyleLength MinWidth,
    StyleLength MinHeight,
    StyleLength MaxWidth,
    StyleLength MaxHeight,
    StyleEdges<StyleLength> Margin,
    StyleEdges<StyleLength> Padding,
    StyleEdges<StyleLength> Inset,
    int? ZIndex);

public sealed record FlexComputedStyle(
    FlexDirectionMode Direction,
    FlexWrapMode Wrap,
    float Grow,
    float Shrink,
    StyleLength Basis,
    JustifyContentMode JustifyContent,
    AlignItemsMode AlignItems,
    AlignSelfMode AlignSelf,
    AlignContentMode AlignContent,
    StyleLength RowGap,
    StyleLength ColumnGap,
    int Order);

public sealed record TextComputedStyle(
    StyleColor Color,
    string FontFamily,
    StyleLength FontSize,
    ushort FontWeight,
    FontStyleMode FontStyle,
    StyleLength LineHeight,
    TextAlignMode TextAlign,
    WhiteSpaceMode WhiteSpace,
    StyleLength LetterSpacing,
    /// <summary>From <c>-webkit-text-fill-color</c>/<c>text-fill-color</c>; null means "use <see cref="Color"/> as normal" (the CSS default, <c>currentcolor</c>).</summary>
    StyleColor? TextFillColor);

public sealed record PaintComputedStyle(
    StyleColor BackgroundColor,
    StyleLinearGradient? BackgroundImage,
    StyleEdges<StyleLength> BorderWidth,
    StyleEdges<BorderLineStyle> BorderStyle,
    StyleEdges<StyleColor> BorderColor,
    StyleCornerRadii BorderRadius,
    float Opacity,
    IReadOnlyList<StyleShadow> BoxShadows,
    /// <summary>From <c>-webkit-background-clip</c>/<c>background-clip: text</c> — the element's background paints as the text's fill instead of the box's fill.</summary>
    bool BackgroundClipText);

public sealed record InteractionComputedStyle(
    CursorMode Cursor,
    PointerEventsMode PointerEvents);

public sealed record TransformComputedStyle(
    StyleLength OriginX,
    StyleLength OriginY,
    IReadOnlyList<StyleTransformOperation> Operations);

/// <summary>
/// Final native style consumed directly by AetherUI layout, paint, hit testing and composition.
/// It is not a CSSOM object and intentionally exposes no browser-style mutation API.
/// </summary>
public sealed record AetherComputedStyle(
    LayoutComputedStyle Layout,
    FlexComputedStyle Flex,
    TextComputedStyle Text,
    PaintComputedStyle Paint,
    InteractionComputedStyle Interaction,
    TransformComputedStyle Transform,
    IReadOnlyDictionary<string, object?> Extensions)
{
    public static AetherComputedStyle Initial { get; } = new AetherStyleBuilder().Build();

    public bool TryGetExtension<T>(string key, out T value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        if (Extensions.TryGetValue(key, out var raw) && raw is T typed)
        {
            value = typed;
            return true;
        }

        value = default!;
        return false;
    }

    public T GetExtension<T>(string key, T fallback) =>
        TryGetExtension<T>(key, out var value) ? value : fallback;
}

/// <summary>
/// Mutable target used only while the runtime resolves one node. Custom Aether capabilities
/// can write strongly typed extension slots through <c>SetExtension</c>.
/// </summary>
public sealed class AetherStyleBuilder
{
    internal DisplayMode Display { get; set; } = DisplayMode.Inline;
    internal PositionMode Position { get; set; } = PositionMode.Static;
    internal VisibilityMode Visibility { get; set; } = VisibilityMode.Visible;
    internal BoxSizingMode BoxSizing { get; set; } = BoxSizingMode.ContentBox;
    internal OverflowMode OverflowX { get; set; } = OverflowMode.Visible;
    internal OverflowMode OverflowY { get; set; } = OverflowMode.Visible;
    internal StyleLength Width { get; set; } = StyleLength.Auto;
    internal StyleLength Height { get; set; } = StyleLength.Auto;
    internal StyleLength MinWidth { get; set; } = StyleLength.Zero;
    internal StyleLength MinHeight { get; set; } = StyleLength.Zero;
    internal StyleLength MaxWidth { get; set; } = StyleLength.None;
    internal StyleLength MaxHeight { get; set; } = StyleLength.None;
    internal StyleEdges<StyleLength> Margin { get; set; } = new(StyleLength.Zero);
    internal StyleEdges<StyleLength> Padding { get; set; } = new(StyleLength.Zero);
    internal StyleEdges<StyleLength> Inset { get; set; } = new(StyleLength.Auto);
    internal int? ZIndex { get; set; }

    internal FlexDirectionMode FlexDirection { get; set; } = FlexDirectionMode.Row;
    internal FlexWrapMode FlexWrap { get; set; } = FlexWrapMode.NoWrap;
    internal float FlexGrow { get; set; }
    internal float FlexShrink { get; set; } = 1f;
    internal StyleLength FlexBasis { get; set; } = StyleLength.Auto;
    internal JustifyContentMode JustifyContent { get; set; } = JustifyContentMode.Start;
    internal AlignItemsMode AlignItems { get; set; } = AlignItemsMode.Stretch;
    internal AlignSelfMode AlignSelf { get; set; } = AlignSelfMode.Auto;
    internal AlignContentMode AlignContent { get; set; } = AlignContentMode.Stretch;
    internal StyleLength RowGap { get; set; } = StyleLength.Zero;
    internal StyleLength ColumnGap { get; set; } = StyleLength.Zero;
    internal int Order { get; set; }

    internal StyleColor Color { get; set; } = StyleColor.Black;
    internal string FontFamily { get; set; } = "Segoe UI";
    internal StyleLength FontSize { get; set; } = StyleLength.PixelsValue(16f);
    internal ushort FontWeight { get; set; } = 400;
    internal FontStyleMode FontStyle { get; set; } = FontStyleMode.Normal;
    internal StyleLength LineHeight { get; set; } = StyleLength.PixelsValue(20f);
    internal TextAlignMode TextAlign { get; set; } = TextAlignMode.Start;
    internal WhiteSpaceMode WhiteSpace { get; set; } = WhiteSpaceMode.Normal;
    internal StyleLength LetterSpacing { get; set; } = StyleLength.Zero;
    internal StyleColor? TextFillColor { get; set; }

    internal StyleColor BackgroundColor { get; set; } = StyleColor.Transparent;
    internal StyleLinearGradient? BackgroundImage { get; set; }
    internal bool BackgroundClipText { get; set; }
    internal StyleEdges<StyleLength> BorderWidth { get; set; } = new(StyleLength.Zero);
    internal StyleEdges<BorderLineStyle> BorderStyle { get; set; } = new(BorderLineStyle.None);
    internal StyleEdges<StyleColor> BorderColor { get; set; } = new(StyleColor.CurrentColor);
    internal StyleCornerRadii BorderRadius { get; set; } = StyleCornerRadii.Zero;
    internal float Opacity { get; set; } = 1f;
    internal IReadOnlyList<StyleShadow> BoxShadows { get; set; } = Array.Empty<StyleShadow>();

    internal CursorMode Cursor { get; set; } = CursorMode.Default;
    internal PointerEventsMode PointerEvents { get; set; } = PointerEventsMode.Auto;

    internal StyleLength TransformOriginX { get; set; } = StyleLength.PercentValue(50f);
    internal StyleLength TransformOriginY { get; set; } = StyleLength.PercentValue(50f);
    internal IReadOnlyList<StyleTransformOperation> TransformOperations { get; set; } =
        Array.Empty<StyleTransformOperation>();

    private readonly Dictionary<string, object?> _extensions = new(StringComparer.Ordinal);

    public void SetExtension<T>(string key, T value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        _extensions[key] = value;
    }

    public bool TryGetExtension<T>(string key, out T value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        if (_extensions.TryGetValue(key, out var raw) && raw is T typed)
        {
            value = typed;
            return true;
        }

        value = default!;
        return false;
    }

    internal AetherComputedStyle Build()
    {
        var extensions = _extensions.Count == 0
            ? EmptyReadOnlyDictionary.Instance
            : new ReadOnlyDictionary<string, object?>(new Dictionary<string, object?>(_extensions, StringComparer.Ordinal));

        return new AetherComputedStyle(
            new LayoutComputedStyle(
                Display,
                Position,
                Visibility,
                BoxSizing,
                OverflowX,
                OverflowY,
                Width,
                Height,
                MinWidth,
                MinHeight,
                MaxWidth,
                MaxHeight,
                Margin,
                Padding,
                Inset,
                ZIndex),
            new FlexComputedStyle(
                FlexDirection,
                FlexWrap,
                FlexGrow,
                FlexShrink,
                FlexBasis,
                JustifyContent,
                AlignItems,
                AlignSelf,
                AlignContent,
                RowGap,
                ColumnGap,
                Order),
            new TextComputedStyle(
                Color,
                FontFamily,
                FontSize,
                FontWeight,
                FontStyle,
                LineHeight,
                TextAlign,
                WhiteSpace,
                LetterSpacing,
                TextFillColor),
            new PaintComputedStyle(
                BackgroundColor,
                BackgroundImage,
                BorderWidth,
                BorderStyle,
                BorderColor,
                BorderRadius,
                Opacity,
                BoxShadows,
                BackgroundClipText),
            new InteractionComputedStyle(Cursor, PointerEvents),
            new TransformComputedStyle(TransformOriginX, TransformOriginY, TransformOperations),
            extensions);
    }

    private sealed class EmptyReadOnlyDictionary : ReadOnlyDictionary<string, object?>
    {
        public static EmptyReadOnlyDictionary Instance { get; } = new();

        private EmptyReadOnlyDictionary()
            : base(new Dictionary<string, object?>(StringComparer.Ordinal))
        {
        }
    }
}
