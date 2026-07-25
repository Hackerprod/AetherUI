namespace AetherUI.StyleRuntime;

internal sealed class SelectorMatcher
{
    public bool Matches(CompiledSelector selector, IAetherStyleNode node) =>
        MatchStep(selector, 0, node);

    private bool MatchStep(CompiledSelector selector, int stepIndex, IAetherStyleNode node)
    {
        var step = selector.Steps[stepIndex];
        if (!MatchesCompound(step.Compound, node))
        {
            return false;
        }

        if (step.RelationToNext == SelectorCombinator.None)
        {
            return true;
        }

        var nextIndex = stepIndex + 1;
        return step.RelationToNext switch
        {
            SelectorCombinator.Child =>
                node.StyleParent is not null && MatchStep(selector, nextIndex, node.StyleParent),
            SelectorCombinator.Descendant => MatchAncestor(selector, nextIndex, node.StyleParent),
            SelectorCombinator.AdjacentSibling =>
                TryGetPreviousSibling(node, out var previous) && MatchStep(selector, nextIndex, previous),
            SelectorCombinator.GeneralSibling => MatchPreviousSiblings(selector, nextIndex, node),
            _ => false,
        };
    }

    private bool MatchesCompound(CompoundSelector compound, IAetherStyleNode node)
    {
        if (compound.TypeName is not null &&
            !string.Equals(compound.TypeName, node.StyleType, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        if (compound.Id is not null && !string.Equals(compound.Id, node.StyleId, StringComparison.Ordinal))
        {
            return false;
        }

        foreach (var className in compound.Classes)
        {
            if (!node.HasStyleClass(className))
            {
                return false;
            }
        }

        foreach (var attribute in compound.Attributes)
        {
            if (!MatchesAttribute(attribute, node))
            {
                return false;
            }
        }

        foreach (var pseudo in compound.Pseudos)
        {
            if (!MatchesPseudo(pseudo, node))
            {
                return false;
            }
        }

        return true;
    }

    private bool MatchesAttribute(AttributeSelector selector, IAetherStyleNode node)
    {
        if (!node.TryGetStyleAttribute(selector.Name, out var actual))
        {
            return false;
        }

        if (selector.Operator == AttributeMatchOperator.Exists)
        {
            return true;
        }

        actual ??= string.Empty;
        var expected = selector.Value ?? string.Empty;
        var comparison = selector.CaseInsensitive ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        return selector.Operator switch
        {
            AttributeMatchOperator.Equals => actual.Equals(expected, comparison),
            AttributeMatchOperator.Includes => actual.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
                .Any(item => item.Equals(expected, comparison)),
            AttributeMatchOperator.DashMatch => actual.Equals(expected, comparison) ||
                actual.StartsWith(expected + "-", comparison),
            AttributeMatchOperator.Prefix => actual.StartsWith(expected, comparison),
            AttributeMatchOperator.Suffix => actual.EndsWith(expected, comparison),
            AttributeMatchOperator.Contains => actual.Contains(expected, comparison),
            _ => false,
        };
    }

    private bool MatchesPseudo(PseudoSelector pseudo, IAetherStyleNode node) => pseudo.Kind switch
    {
        PseudoSelectorKind.RequiredState => (node.StyleStates & pseudo.State) == pseudo.State,
        PseudoSelectorKind.ForbiddenState => (node.StyleStates & pseudo.State) == 0,
        PseudoSelectorKind.Root => node.StyleParent is null,
        PseudoSelectorKind.Empty => node.StyleChildren.Count == 0,
        PseudoSelectorKind.FirstChild => GetSiblingIndex(node) == 0,
        PseudoSelectorKind.LastChild => IsLastChild(node),
        PseudoSelectorKind.OnlyChild => node.StyleParent is not null && node.StyleParent.StyleChildren.Count == 1,
        PseudoSelectorKind.NthChild => pseudo.Nth.Matches(GetSiblingIndex(node) + 1),
        PseudoSelectorKind.Is or PseudoSelectorKind.Where => pseudo.Nested.Any(selector => Matches(selector, node)),
        PseudoSelectorKind.Not => pseudo.Nested.All(selector => !Matches(selector, node)),
        PseudoSelectorKind.Has => HasMatchingDescendant(node, pseudo.Nested),
        _ => false,
    };

    private bool MatchAncestor(CompiledSelector selector, int stepIndex, IAetherStyleNode? ancestor)
    {
        while (ancestor is not null)
        {
            if (MatchStep(selector, stepIndex, ancestor))
            {
                return true;
            }

            ancestor = ancestor.StyleParent;
        }

        return false;
    }

    private bool MatchPreviousSiblings(CompiledSelector selector, int stepIndex, IAetherStyleNode node)
    {
        var parent = node.StyleParent;
        if (parent is null)
        {
            return false;
        }

        foreach (var sibling in parent.StyleChildren)
        {
            if (sibling.StyleNodeId == node.StyleNodeId)
            {
                return false;
            }

            if (MatchStep(selector, stepIndex, sibling))
            {
                return true;
            }
        }

        return false;
    }

    private bool HasMatchingDescendant(IAetherStyleNode node, IReadOnlyList<CompiledSelector> selectors)
    {
        foreach (var child in node.StyleChildren)
        {
            if (selectors.Any(selector => Matches(selector, child)) || HasMatchingDescendant(child, selectors))
            {
                return true;
            }
        }

        return false;
    }

    private static bool TryGetPreviousSibling(IAetherStyleNode node, out IAetherStyleNode sibling)
    {
        var parent = node.StyleParent;
        if (parent is null)
        {
            sibling = null!;
            return false;
        }

        IAetherStyleNode? previous = null;
        foreach (var child in parent.StyleChildren)
        {
            if (child.StyleNodeId == node.StyleNodeId)
            {
                if (previous is not null)
                {
                    sibling = previous;
                    return true;
                }

                break;
            }

            previous = child;
        }

        sibling = null!;
        return false;
    }

    private static int GetSiblingIndex(IAetherStyleNode node)
    {
        var parent = node.StyleParent;
        if (parent is null)
        {
            return 0;
        }

        for (var index = 0; index < parent.StyleChildren.Count; index++)
        {
            if (parent.StyleChildren[index].StyleNodeId == node.StyleNodeId)
            {
                return index;
            }
        }

        return -1;
    }

    private static bool IsLastChild(IAetherStyleNode node)
    {
        var parent = node.StyleParent;
        return parent is null || parent.StyleChildren.Count > 0 &&
            parent.StyleChildren[^1].StyleNodeId == node.StyleNodeId;
    }
}
