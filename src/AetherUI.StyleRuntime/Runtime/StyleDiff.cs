namespace AetherUI.StyleRuntime;

public readonly record struct StyleUpdate(
    IAetherStyleNode Node,
    AetherComputedStyle? Previous,
    AetherComputedStyle Current,
    StyleImpact Impact);

internal static class StyleDiff
{
    public static StyleImpact Compare(
        AetherComputedStyle? previous,
        AetherComputedStyle current,
        IReadOnlyList<StylePropertyDefinition> definitions)
    {
        if (previous is null)
        {
            return StyleImpact.All;
        }

        var impact = StyleImpact.None;
        foreach (var definition in definitions)
        {
            var before = definition.Read(previous);
            var after = definition.Read(current);
            if (!ValueEquals(before, after))
            {
                impact |= definition.Impact;
            }
        }

        return impact;
    }

    private static bool ValueEquals(object? left, object? right)
    {
        if (ReferenceEquals(left, right))
        {
            return true;
        }

        if (left is null || right is null)
        {
            return false;
        }

        if (left is IReadOnlyList<StyleShadow> leftShadows &&
            right is IReadOnlyList<StyleShadow> rightShadows)
        {
            return leftShadows.SequenceEqual(rightShadows);
        }

        if (left is IReadOnlyList<StyleTransformOperation> leftTransforms &&
            right is IReadOnlyList<StyleTransformOperation> rightTransforms)
        {
            return leftTransforms.SequenceEqual(rightTransforms);
        }

        return Equals(left, right);
    }
}
