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
    Table,
    TableRow,
    TableCell,
}

public enum PositionKind : byte
{
    Static,
    Relative,
    Absolute,
    Fixed,
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

public enum AlignSelf : sbyte
{
    Auto = -1,
    Stretch = 0,
    Start = 1,
    Center = 2,
    End = 3,
}

public enum TextAlign : byte
{
    Start,
    Center,
    End,
    Left,
    Right,
    Justify,
}

public enum TextTransform : byte
{
    None,
    Capitalize,
    Uppercase,
    Lowercase,
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

public enum BorderStyle : byte
{
    None,
    Hidden,
    Dotted,
    Dashed,
    Solid,
    Double,
    Groove,
    Ridge,
    Inset,
    Outset,
}

public readonly record struct ColorEdges(ColorRgba Left, ColorRgba Top, ColorRgba Right, ColorRgba Bottom)
{
    public static ColorEdges Transparent { get; } = new(ColorRgba.Transparent, ColorRgba.Transparent, ColorRgba.Transparent, ColorRgba.Transparent);
}

public readonly record struct BorderStyleEdges(BorderStyle Left, BorderStyle Top, BorderStyle Right, BorderStyle Bottom)
{
    public static BorderStyleEdges None { get; } = new(BorderStyle.None, BorderStyle.None, BorderStyle.None, BorderStyle.None);
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

public enum PointerEventsKind : byte
{
    Auto,
    None,
}

public enum FlexBasisKind : byte
{
    Auto,
    Content,
    Length,
}

public readonly record struct FlexBasis(FlexBasisKind Kind, CssLength Length)
{
    public static readonly FlexBasis Auto = new(FlexBasisKind.Auto, CssLength.Auto);
    public static readonly FlexBasis Content = new(FlexBasisKind.Content, CssLength.Auto);
}

/// <summary>Typed CSS 2D transform operation retained until post-layout painting.</summary>
public enum TransformOperationKind : byte
{
    Translate,
    Scale,
    Rotate,
    SkewX,
    SkewY,
    Matrix,
}

public readonly record struct CssTransformOperation(
    TransformOperationKind Kind, CssLength X, CssLength Y, float ScalarX, float ScalarY, float AngleRadians,
    float MatrixA, float MatrixB, float MatrixC, float MatrixD, float MatrixE, float MatrixF);

public readonly record struct CssTransformOrigin(CssLength X, CssLength Y)
{
    public static readonly CssTransformOrigin Center = new(new CssLength(50, CssUnit.Percent), new CssLength(50, CssUnit.Percent));
}

public sealed class ComputedStyle
{
    public DisplayKind Display { get; set; } = DisplayKind.Inline;
    public FlexDirection FlexDirection { get; set; } = FlexDirection.Row;
    public JustifyContent JustifyContent { get; set; } = JustifyContent.Start;
    public AlignItems AlignItems { get; set; } = AlignItems.Stretch;
    public AlignSelf AlignSelf { get; set; } = AlignSelf.Auto;
    public float FlexGrow { get; set; }
    public float FlexShrink { get; set; } = 1f;
    public FlexBasis FlexBasis { get; set; } = FlexBasis.Auto;
    public bool FlexWrap { get; set; }
    public float Gap { get; set; }
    public PositionKind Position { get; set; } = PositionKind.Static;
    public CssEdges Inset { get; set; } = new(CssLength.Auto);

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
    public ColorEdges BorderColors { get; set; } = ColorEdges.Transparent;
    public BorderStyleEdges BorderStyles { get; set; } = BorderStyleEdges.None;

    /// <summary>
    /// From <c>-webkit-background-clip: text</c> (e.g. a gradient logo wordmark): the element's own
    /// background-image paints as the fill of its text instead of a box fill. Mutually exclusive
    /// with <see cref="BackgroundGradient"/> — never both set at once, see StyleRuntimeBridge.
    /// </summary>
    public LinearGradient? TextGradient { get; set; }

    /// <summary>Text shadows in CSS declaration order. The native text-shadow parser rejects inset and spread radii.</summary>
    public IReadOnlyList<BoxShadow> TextShadows { get; set; } = [];

    /// <summary>Outer (non-inset) box-shadows, back-to-front paint order (first CSS shadow paints on top).</summary>
    public IReadOnlyList<BoxShadow> BoxShadows { get; set; } = [];

    /// <summary>
    /// Kept symbolic (not a resolved float) because a percentage radius must resolve against the
    /// element's own border-box size, which is only known at paint time.
    /// </summary>
    public CssLength BorderRadius { get; set; } = CssLength.Zero;
    public CssBorderRadii BorderRadii { get; set; } = CssBorderRadii.Zero;

    public float Opacity { get; set; } = 1;

    /// <summary>
    /// Non-inherited CSS transforms. They affect post-layout painting and hit-testing only;
    /// the Display List later scopes them around the element and its descendants.
    /// </summary>
    public IReadOnlyList<CssTransformOperation> Transforms { get; set; } = [];
    public CssTransformOrigin TransformOrigin { get; set; } = CssTransformOrigin.Center;

    /// <summary>CSS transition bitmask (AetherUI.Core.Native transition-property bits: opacity = 1, transform = 2).</summary>
    public int TransitionProperties { get; set; }

    /// <summary>CSS transition-duration in seconds; 0 means the element never transitions.</summary>
    public float TransitionDuration { get; set; }

    /// <summary>CSS transition-delay in seconds.</summary>
    public float TransitionDelay { get; set; }

    /// <summary>CSS transition-timing-function enum value (0 = linear, see the native timing enum).</summary>
    public int TransitionTiming { get; set; }

    public string FontFamily { get; set; } = "Segoe UI";
    public float FontSize { get; set; } = 16;
    public FontWeight FontWeight { get; set; } = FontWeight.Normal;
    public float LineHeight { get; set; } = 1.25f;
    public TextAlign TextAlign { get; set; } = TextAlign.Start;
    public TextTransform TextTransform { get; set; } = TextTransform.None;

    public OverflowKind OverflowX { get; set; } = OverflowKind.Visible;
    public OverflowKind OverflowY { get; set; } = OverflowKind.Visible;
    public CursorKind Cursor { get; set; } = CursorKind.Default;
    public PointerEventsKind PointerEvents { get; set; } = PointerEventsKind.Auto;
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
        result.TextTransform = parent.TextTransform;
        result.Cursor = parent.Cursor;
        result.PointerEvents = parent.PointerEvents;
        return result;
    }
}
