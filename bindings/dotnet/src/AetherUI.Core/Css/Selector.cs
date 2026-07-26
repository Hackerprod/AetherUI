using AetherUI.Dom;

namespace AetherUI.Css;

public enum SelectorCombinator : byte
{
    None,
    Descendant,
    Child,
}

public sealed record AttributeSelector(string Name, string? Value);

public sealed class SimpleSelector
{
    public string? TagName { get; init; }
    public string? Id { get; init; }
    public IReadOnlyList<string> Classes { get; init; } = [];
    public IReadOnlyList<AttributeSelector> Attributes { get; init; } = [];
    public IReadOnlyList<string> PseudoClasses { get; init; } = [];
}

public sealed record SelectorStep(SimpleSelector Simple, SelectorCombinator ToLeft);

public sealed class CssSelector
{
    public CssSelector(IReadOnlyList<SelectorStep> steps, int specificity, string source)
    {
        Steps = steps;
        Specificity = specificity;
        Source = source;
    }

    public IReadOnlyList<SelectorStep> Steps { get; }
    public int Specificity { get; }
    public string Source { get; }

    public bool Matches(ElementNode element) => MatchesStep(element, 0);

    public static bool TryParse(string? source, out CssSelector selector)
    {
        selector = null!;
        if (string.IsNullOrWhiteSpace(source))
        {
            return false;
        }

        var text = source.Trim();
        var segments = new List<string>();
        var combinators = new List<SelectorCombinator>();
        var position = 0;

        while (position < text.Length)
        {
            while (position < text.Length && char.IsWhiteSpace(text[position]))
            {
                position++;
            }

            if (position >= text.Length)
            {
                break;
            }

            var start = position;
            var bracketDepth = 0;
            char quote = '\0';
            while (position < text.Length)
            {
                var current = text[position];
                if (quote != '\0')
                {
                    if (current == '\\')
                    {
                        position += 2;
                        continue;
                    }

                    if (current == quote)
                    {
                        quote = '\0';
                    }

                    position++;
                    continue;
                }

                if (current is '\'' or '"')
                {
                    quote = current;
                }
                else if (current == '[')
                {
                    bracketDepth++;
                }
                else if (current == ']')
                {
                    bracketDepth = Math.Max(0, bracketDepth - 1);
                }
                else if (bracketDepth == 0 && (current == '>' || char.IsWhiteSpace(current)))
                {
                    break;
                }

                position++;
            }

            var segment = text[start..position].Trim();
            if (segment.Length == 0)
            {
                return false;
            }

            segments.Add(segment);
            var hadWhitespace = false;
            while (position < text.Length && char.IsWhiteSpace(text[position]))
            {
                hadWhitespace = true;
                position++;
            }

            if (position < text.Length && text[position] == '>')
            {
                combinators.Add(SelectorCombinator.Child);
                position++;
            }
            else if (position < text.Length && hadWhitespace)
            {
                combinators.Add(SelectorCombinator.Descendant);
            }
        }

        if (segments.Count == 0 || combinators.Count != segments.Count - 1)
        {
            return false;
        }

        var parsed = new List<SimpleSelector>(segments.Count);
        var specificity = 0;
        foreach (var segment in segments)
        {
            if (!TryParseSimple(segment, out var simple, out var simpleSpecificity))
            {
                return false;
            }

            parsed.Add(simple);
            specificity += simpleSpecificity;
        }

        var steps = new List<SelectorStep>(segments.Count);
        for (var i = parsed.Count - 1; i >= 0; i--)
        {
            var relation = i > 0 ? combinators[i - 1] : SelectorCombinator.None;
            steps.Add(new SelectorStep(parsed[i], relation));
        }

        selector = new CssSelector(steps, specificity, text);
        return true;
    }

    private bool MatchesStep(ElementNode element, int stepIndex)
    {
        var step = Steps[stepIndex];
        if (!MatchesSimple(step.Simple, element))
        {
            return false;
        }

        if (stepIndex == Steps.Count - 1)
        {
            return true;
        }

        if (step.ToLeft == SelectorCombinator.Child)
        {
            return element.Parent is ElementNode parent && MatchesStep(parent, stepIndex + 1);
        }

        for (var current = element.Parent as ElementNode; current is not null; current = current.Parent as ElementNode)
        {
            if (MatchesStep(current, stepIndex + 1))
            {
                return true;
            }
        }

        return false;
    }

    private static bool MatchesSimple(SimpleSelector selector, ElementNode element)
    {
        if (selector.TagName is { Length: > 0 } tagName && tagName != "*" &&
            !string.Equals(tagName, element.TagName, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        if (selector.Id is not null && !string.Equals(selector.Id, element.Id, StringComparison.Ordinal))
        {
            return false;
        }

        if (selector.Classes.Any(className => !element.HasClass(className)))
        {
            return false;
        }

        foreach (var attribute in selector.Attributes)
        {
            if (!element.HasAttribute(attribute.Name))
            {
                return false;
            }

            if (attribute.Value is not null &&
                !string.Equals(element.GetAttribute(attribute.Name), attribute.Value, StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
        }

        foreach (var pseudoClass in selector.PseudoClasses)
        {
            if (!MatchesPseudoClass(pseudoClass, element))
            {
                return false;
            }
        }

        return true;
    }

    private static bool MatchesPseudoClass(string pseudoClass, ElementNode element) => pseudoClass switch
    {
        "hover" => element.State.HasFlag(ElementState.Hover),
        "active" => element.State.HasFlag(ElementState.Active),
        "focus" => element.State.HasFlag(ElementState.Focus),
        "disabled" => element.IsDisabled,
        "required" => element.IsRequired,
        "invalid" => element.State.HasFlag(ElementState.Invalid),
        "first-child" => element.Parent?.Children.OfType<ElementNode>().FirstOrDefault() == element,
        "last-child" => element.Parent?.Children.OfType<ElementNode>().LastOrDefault() == element,
        _ => false,
    };

    private static bool TryParseSimple(string source, out SimpleSelector selector, out int specificity)
    {
        selector = null!;
        specificity = 0;
        var position = 0;
        string? tagName = null;
        string? id = null;
        var classes = new List<string>();
        var attributes = new List<AttributeSelector>();
        var pseudoClasses = new List<string>();

        if (position < source.Length && (source[position] == '*' || IsIdentifierStart(source[position])))
        {
            tagName = ReadIdentifier(source, ref position);
            if (tagName.Length > 0 && tagName != "*")
            {
                specificity += 1;
            }
        }

        while (position < source.Length)
        {
            var prefix = source[position++];
            switch (prefix)
            {
                case '#':
                    id = ReadIdentifier(source, ref position);
                    if (id.Length == 0)
                    {
                        return false;
                    }

                    specificity += 100;
                    break;
                case '.':
                    var className = ReadIdentifier(source, ref position);
                    if (className.Length == 0)
                    {
                        return false;
                    }

                    classes.Add(className);
                    specificity += 10;
                    break;
                case ':':
                    var pseudo = ReadIdentifier(source, ref position).ToLowerInvariant();
                    if (pseudo.Length == 0)
                    {
                        return false;
                    }

                    pseudoClasses.Add(pseudo);
                    specificity += 10;
                    break;
                case '[':
                    var closing = FindAttributeClose(source, position);
                    if (closing < 0)
                    {
                        return false;
                    }

                    var attributeText = source[position..closing].Trim();
                    position = closing + 1;
                    var equals = attributeText.IndexOf('=');
                    string attributeName;
                    string? attributeValue = null;
                    if (equals >= 0)
                    {
                        attributeName = attributeText[..equals].Trim().ToLowerInvariant();
                        attributeValue = attributeText[(equals + 1)..].Trim().Trim('\'', '"');
                    }
                    else
                    {
                        attributeName = attributeText.ToLowerInvariant();
                    }

                    if (attributeName.Length == 0)
                    {
                        return false;
                    }

                    attributes.Add(new AttributeSelector(attributeName, attributeValue));
                    specificity += 10;
                    break;
                default:
                    return false;
            }
        }

        selector = new SimpleSelector
        {
            TagName = tagName,
            Id = id,
            Classes = classes,
            Attributes = attributes,
            PseudoClasses = pseudoClasses,
        };
        return true;
    }

    private static string ReadIdentifier(string source, ref int position)
    {
        if (position < source.Length && source[position] == '*')
        {
            position++;
            return "*";
        }

        var start = position;
        while (position < source.Length && IsIdentifierPart(source[position]))
        {
            position++;
        }

        return source[start..position];
    }

    private static int FindAttributeClose(string source, int start)
    {
        char quote = '\0';
        for (var i = start; i < source.Length; i++)
        {
            var current = source[i];
            if (quote != '\0')
            {
                if (current == quote)
                {
                    quote = '\0';
                }
            }
            else if (current is '\'' or '"')
            {
                quote = current;
            }
            else if (current == ']')
            {
                return i;
            }
        }

        return -1;
    }

    private static bool IsIdentifierStart(char value) => char.IsLetter(value) || value is '_' or '-' or '*';
    private static bool IsIdentifierPart(char value) => char.IsLetterOrDigit(value) || value is '_' or '-';
}
