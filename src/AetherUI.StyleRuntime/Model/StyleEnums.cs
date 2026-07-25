namespace AetherUI.StyleRuntime;

[Flags]
public enum StylePseudoState : uint
{
    None = 0,
    Hover = 1u << 0,
    Active = 1u << 1,
    Focus = 1u << 2,
    FocusWithin = 1u << 3,
    Disabled = 1u << 4,
    Checked = 1u << 5,
    Invalid = 1u << 6,
    ReadOnly = 1u << 7,
    Selected = 1u << 8,
    Expanded = 1u << 9,
}

[Flags]
public enum StyleImpact : uint
{
    None = 0,
    RebuildVisual = 1u << 0,
    Layout = 1u << 1,
    TextLayout = 1u << 2,
    Paint = 1u << 3,
    Composite = 1u << 4,
    HitTest = 1u << 5,
    Cursor = 1u << 6,
    Inherited = 1u << 7,
    All = RebuildVisual | Layout | TextLayout | Paint | Composite | HitTest | Cursor | Inherited,
}

public enum StyleSheetLayer : byte
{
    EngineDefaults = 0,
    Theme = 1,
    Author = 2,
}

public enum DisplayMode : byte
{
    None,
    Block,
    Inline,
    InlineBlock,
    Flex,
}

public enum PositionMode : byte
{
    Static,
    Relative,
    Absolute,
    Fixed,
}

public enum BoxSizingMode : byte
{
    ContentBox,
    BorderBox,
}

public enum OverflowMode : byte
{
    Visible,
    Hidden,
    Clip,
    Auto,
    Scroll,
}

public enum VisibilityMode : byte
{
    Visible,
    Hidden,
    Collapse,
}

public enum FlexDirectionMode : byte
{
    Row,
    RowReverse,
    Column,
    ColumnReverse,
}

public enum FlexWrapMode : byte
{
    NoWrap,
    Wrap,
    WrapReverse,
}

public enum JustifyContentMode : byte
{
    Start,
    End,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
}

public enum AlignItemsMode : byte
{
    Stretch,
    Start,
    End,
    Center,
    Baseline,
}

public enum AlignSelfMode : byte
{
    Auto,
    Stretch,
    Start,
    End,
    Center,
    Baseline,
}

public enum AlignContentMode : byte
{
    Stretch,
    Start,
    End,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
}

public enum TextAlignMode : byte
{
    Start,
    End,
    Left,
    Right,
    Center,
    Justify,
}

public enum FontStyleMode : byte
{
    Normal,
    Italic,
    Oblique,
}

public enum WhiteSpaceMode : byte
{
    Normal,
    NoWrap,
    Pre,
    PreWrap,
    PreLine,
}

public enum CursorMode : byte
{
    Default,
    Pointer,
    Text,
    Move,
    Crosshair,
    Wait,
    Help,
    NotAllowed,
    Grab,
    Grabbing,
    ResizeHorizontal,
    ResizeVertical,
}

public enum PointerEventsMode : byte
{
    Auto,
    None,
}

public enum BorderLineStyle : byte
{
    None,
    Solid,
    Dashed,
    Dotted,
    Double,
}

public enum StyleLengthKind : byte
{
    Linear,
    Auto,
    None,
    MinContent,
    MaxContent,
    FitContent,
}

public enum StyleTransformKind : byte
{
    Translate,
    Scale,
    Rotate,
    Skew,
}

public enum StyleMutationKind : byte
{
    Class,
    Id,
    Attribute,
    State,
    InlineStyle,
    Children,
    Parent,
    Environment,
    Full,
}

public readonly record struct StyleMutation(StyleMutationKind Kind, string? Name = null)
{
    public static StyleMutation Class(string? name = null) => new(StyleMutationKind.Class, name);
    public static StyleMutation Id() => new(StyleMutationKind.Id);
    public static StyleMutation Attribute(string? name = null) => new(StyleMutationKind.Attribute, name);
    public static StyleMutation State() => new(StyleMutationKind.State);
    public static StyleMutation InlineStyle() => new(StyleMutationKind.InlineStyle);
    public static StyleMutation Children() => new(StyleMutationKind.Children);
    public static StyleMutation Parent() => new(StyleMutationKind.Parent);
    public static StyleMutation Environment() => new(StyleMutationKind.Environment);
    public static StyleMutation Full() => new(StyleMutationKind.Full);
}
