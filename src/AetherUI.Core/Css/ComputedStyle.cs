using AetherUI.Primitives;

namespace AetherUI.Css;

public enum DisplayKind : byte
{
    None,
    Block,
    Inline,
    InlineBlock,
    Flex,
    LineBreak,
}

public enum FlexDirection : byte
{
    Row,
    Column,
}

public enum JustifyContent : byte
{
    Start,
    Center,
    End,
    SpaceBetween,
}

public enum AlignItems : byte
{
    Stretch,
    Start,
    Center,
    End,
}

public enum TextAlign : byte
{
    Start,
    Center,
    End,
}

public enum FontWeight : ushort
{
    Thin = 100,
    ExtraLight = 200,
    Light = 300,
    Normal = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
    ExtraBold = 800,
    Black = 900,
}

public enum BoxSizing : byte
{
    ContentBox,
    BorderBox,
}

public enum OverflowKind : byte
{
    Visible,
    Hidden,
    /// <summary>Clips like <see cref="Hidden"/> but is a scroll container (scrollable + shows a thumb only when content overflows).</summary>
    Auto,
    /// <summary>Clips like <see cref="Hidden"/> and is always a scroll container regardless of overflow.</summary>
    Scroll,
}

public enum CursorKind : byte
{
    Default,
    Pointer,
    Text,
}

public sealed class ComputedStyle
{
    public DisplayKind Display { get; set; } = DisplayKind.Inline;
    public FlexDirection FlexDirection { get; set; } = FlexDirection.Row;
    public JustifyContent JustifyContent { get; set; } = JustifyContent.Start;
    public AlignItems AlignItems { get; set; } = AlignItems.Stretch;
    public float FlexGrow { get; set; }
    public bool FlexWrap { get; set; }
    public float Gap { get; set; }

    public CssLength Width { get; set; } = CssLength.Auto;
    public CssLength Height { get; set; } = CssLength.Auto;
    public CssLength MinWidth { get; set; } = CssLength.Zero;
    public CssLength MinHeight { get; set; } = CssLength.Zero;
    public CssLength MaxWidth { get; set; } = CssLength.Auto;
    public CssLength MaxHeight { get; set; } = CssLength.Auto;
    public CssEdges Margin { get; set; } = CssEdges.Zero;
    public CssEdges Padding { get; set; } = CssEdges.Zero;
    public CssEdges BorderWidth { get; set; } = CssEdges.Zero;
    public BoxSizing BoxSizing { get; set; } = BoxSizing.ContentBox;

    public ColorRgba Color { get; set; } = ColorRgba.Black;
    public ColorRgba BackgroundColor { get; set; } = ColorRgba.Transparent;
    public LinearGradient? BackgroundGradient { get; set; }
    public ColorRgba BorderColor { get; set; } = ColorRgba.Transparent;

    /// <summary>
    /// From <c>-webkit-background-clip: text</c> (e.g. a gradient logo wordmark): the element's own
    /// background-image paints as the fill of its text instead of a box fill. Mutually exclusive
    /// with <see cref="BackgroundGradient"/> — never both set at once, see StyleRuntimeBridge.
    /// </summary>
    public LinearGradient? TextGradient { get; set; }

    /// <summary>Outer (non-inset) box-shadows, back-to-front paint order (first CSS shadow paints on top).</summary>
    public IReadOnlyList<BoxShadow> BoxShadows { get; set; } = [];

    /// <summary>
    /// Kept symbolic (not a resolved float) because a percentage radius must resolve against the
    /// element's own border-box size, which is only known at paint time.
    /// </summary>
    public CssLength BorderRadius { get; set; } = CssLength.Zero;

    public float Opacity { get; set; } = 1;

    public string FontFamily { get; set; } = "Segoe UI";
    public float FontSize { get; set; } = 16;
    public FontWeight FontWeight { get; set; } = FontWeight.Normal;
    public float LineHeight { get; set; } = 1.25f;
    public TextAlign TextAlign { get; set; } = TextAlign.Start;

    public OverflowKind Overflow { get; set; } = OverflowKind.Visible;
    public CursorKind Cursor { get; set; } = CursorKind.Default;
    public bool Visible { get; set; } = true;

    public ComputedStyle Clone() => (ComputedStyle)MemberwiseClone();

    public static ComputedStyle InheritFrom(ComputedStyle? parent)
    {
        var result = new ComputedStyle();
        if (parent is null)
        {
            return result;
        }

        result.Color = parent.Color;
        result.FontFamily = parent.FontFamily;
        result.FontSize = parent.FontSize;
        result.FontWeight = parent.FontWeight;
        result.LineHeight = parent.LineHeight;
        result.TextAlign = parent.TextAlign;
        result.Cursor = parent.Cursor;
        return result;
    }
}
