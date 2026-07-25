namespace AetherUI.StyleRuntime;

[Flags]
internal enum SelectorDependency : ushort
{
    None = 0,
    SelfClass = 1 << 0,
    SelfId = 1 << 1,
    SelfAttribute = 1 << 2,
    SelfState = 1 << 3,
    Ancestor = 1 << 4,
    Sibling = 1 << 5,
    Structure = 1 << 6,
    Descendant = 1 << 7,
}

internal enum SelectorCombinator : byte
{
    None,
    Descendant,
    Child,
    AdjacentSibling,
    GeneralSibling,
}

internal enum AttributeMatchOperator : byte
{
    Exists,
    Equals,
    Includes,
    DashMatch,
    Prefix,
    Suffix,
    Contains,
}

internal enum PseudoSelectorKind : byte
{
    RequiredState,
    ForbiddenState,
    Root,
    Empty,
    FirstChild,
    LastChild,
    OnlyChild,
    NthChild,
    Is,
    Where,
    Not,
    Has,
}

internal readonly record struct Specificity(int IdCount, int ClassCount, int TypeCount) : IComparable<Specificity>
{
    public static Specificity Zero => default;

    public int CompareTo(Specificity other)
    {
        var result = IdCount.CompareTo(other.IdCount);
        if (result != 0)
        {
            return result;
        }

        result = ClassCount.CompareTo(other.ClassCount);
        return result != 0 ? result : TypeCount.CompareTo(other.TypeCount);
    }

    public static Specificity operator +(Specificity left, Specificity right) =>
        new(left.IdCount + right.IdCount, left.ClassCount + right.ClassCount, left.TypeCount + right.TypeCount);

    public static Specificity Max(IEnumerable<Specificity> values)
    {
        var maximum = Zero;
        foreach (var value in values)
        {
            if (value.CompareTo(maximum) > 0)
            {
                maximum = value;
            }
        }

        return maximum;
    }
}

internal readonly record struct AttributeSelector(
    string Name,
    AttributeMatchOperator Operator,
    string? Value,
    bool CaseInsensitive);

internal readonly record struct NthExpression(int Step, int Offset)
{
    public bool Matches(int oneBasedIndex)
    {
        if (Step == 0)
        {
            return oneBasedIndex == Offset;
        }

        var difference = oneBasedIndex - Offset;
        return difference == 0 || difference % Step == 0 && difference / Step >= 0;
    }
}

internal sealed record PseudoSelector(
    PseudoSelectorKind Kind,
    StylePseudoState State,
    NthExpression Nth,
    IReadOnlyList<CompiledSelector> Nested);

internal sealed record CompoundSelector(
    string? TypeName,
    string? Id,
    IReadOnlyList<string> Classes,
    IReadOnlyList<AttributeSelector> Attributes,
    IReadOnlyList<PseudoSelector> Pseudos);

/// <summary>Right-to-left selector step. RelationToNext describes how to reach the next step on the left.</summary>
internal sealed record SelectorStep(CompoundSelector Compound, SelectorCombinator RelationToNext);

internal readonly record struct SelectorAnchor(string? Id, string? ClassName, string? TypeName)
{
    public bool IsUniversal => Id is null && ClassName is null && TypeName is null;
}

internal sealed record CompiledSelector(
    IReadOnlyList<SelectorStep> Steps,
    Specificity Specificity,
    SelectorDependency Dependencies,
    SelectorAnchor Anchor,
    string Source);
