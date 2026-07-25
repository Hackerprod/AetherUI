using System.Globalization;
using System.Text;
using AetherUI.Dom;
using AetherUI.Primitives;

namespace AetherUI.Css;

public sealed class StyleMap
{
    private readonly Dictionary<ElementNode, ComputedStyle> _styles;

    internal StyleMap(Dictionary<ElementNode, ComputedStyle> styles)
    {
        _styles = styles;
    }

    public ComputedStyle this[ElementNode element] => _styles[element];
    public bool TryGetValue(ElementNode element, out ComputedStyle style) => _styles.TryGetValue(element, out style!);
}

public static class StyleResolver
{
    private const string UserAgentCss = """
        html, body { display: block; }
        body { margin: 8px; color: #202124; background-color: white; font-family: "Segoe UI"; font-size: 16px; line-height: 1.25; }
        head, title, meta, link, style, script { display: none; }
        div, form, p, section, article, header, footer, main, nav, h1, h2, h3, h4, h5, h6 { display: block; }
        span, label, strong, em, a { display: inline; }
        br { display: br; }
        h1 { font-size: 2em; font-weight: 700; margin: .67em 0; }
        h2 { font-size: 1.5em; font-weight: 700; margin: .83em 0; }
        h3 { font-size: 1.17em; font-weight: 700; margin: 1em 0; }
        h4 { font-size: 1em; font-weight: 700; margin: 1.33em 0; }
        p { margin: 1em 0; }
        input, button { display: inline-block; font-family: "Segoe UI"; font-size: 14px; box-sizing: border-box; }
        input { min-width: 180px; height: 34px; padding: 6px 9px; border: 1px solid #9aa0a6; border-radius: 4px; background-color: white; color: #202124; cursor: text; }
        input:focus { border-color: #2563eb; }
        input:invalid { border-color: #dc2626; }
        button { min-width: 84px; height: 34px; padding: 6px 14px; border: 1px solid #1d4ed8; border-radius: 4px; background-color: #2563eb; color: white; cursor: pointer; text-align: center; }
        button:hover { background-color: #1d4ed8; }
        button:active { background-color: #1e40af; }
        button:disabled { opacity: .55; cursor: default; }
        """;

    private static readonly CssStyleSheet UserAgentSheet = CssStyleSheet.Parse(UserAgentCss);

    public static StyleMap Resolve(DocumentNode document, float viewportWidth = 1280, float viewportHeight = 720)
    {
        ArgumentNullException.ThrowIfNull(document);
        var authorCss = string.Join('\n', document.GetElementsByTagName("style").Select(static element => element.TextContent));
        var authorSheet = CssStyleSheet.Parse(authorCss);
        var result = new Dictionary<ElementNode, ComputedStyle>();
        var topLevelElements = document.Children.OfType<ElementNode>().ToArray();
        var htmlRoot = topLevelElements.FirstOrDefault(static element => element.TagName == "html");
        if (htmlRoot is not null)
        {
            ResolveElement(htmlRoot, null, result, authorSheet, viewportWidth, viewportHeight, 16);
        }
        else
        {
            // Aether views commonly contain <style> followed by <body> without an explicit
            // <html> wrapper. Each top-level subtree must therefore be resolved.
            foreach (var element in topLevelElements)
            {
                ResolveElement(element, null, result, authorSheet, viewportWidth, viewportHeight, 16);
            }
        }

        return new StyleMap(result);
    }

    private static void ResolveElement(
        ElementNode element,
        ComputedStyle? parent,
        Dictionary<ElementNode, ComputedStyle> output,
        CssStyleSheet authorSheet,
        float viewportWidth,
        float viewportHeight,
        float rootFontSize)
    {
        var style = ComputedStyle.InheritFrom(parent);
        var winners = new Dictionary<string, Winner>(StringComparer.OrdinalIgnoreCase);
        CollectRules(UserAgentSheet, element, 0, winners);
        CollectRules(authorSheet, element, 1_000_000, winners);

        if (element.GetAttribute("style") is { Length: > 0 } inlineStyle)
        {
            var order = 2_000_000;
            foreach (var declaration in CssParser.ParseDeclarations(inlineStyle))
            {
                Consider(winners, declaration, 1000, order++);
            }
        }

        foreach (var winner in winners.Values.OrderBy(static value => value.Order))
        {
            Apply(style, winner.Declaration.Name, winner.Declaration.Value, parent, viewportWidth, viewportHeight, rootFontSize);
        }

        if (element.TagName == "html")
        {
            rootFontSize = style.FontSize;
        }

        output[element] = style;
        foreach (var child in element.Children.OfType<ElementNode>())
        {
            ResolveElement(child, style, output, authorSheet, viewportWidth, viewportHeight, rootFontSize);
        }
    }

    private static void CollectRules(CssStyleSheet sheet, ElementNode element, int orderBase, Dictionary<string, Winner> winners)
    {
        foreach (var rule in sheet.Rules)
        {
            var matchingSpecificity = rule.Selectors.Where(selector => selector.Matches(element))
                .Select(static selector => selector.Specificity)
                .DefaultIfEmpty(-1)
                .Max();
            if (matchingSpecificity < 0)
            {
                continue;
            }

            var order = orderBase + (rule.SourceOrder * 100);
            foreach (var declaration in rule.Declarations)
            {
                Consider(winners, declaration, matchingSpecificity, order++);
            }
        }
    }

    private static void Consider(Dictionary<string, Winner> winners, CssDeclaration declaration, int specificity, int order)
    {
        var candidate = new Winner(declaration, specificity, order);
        if (!winners.TryGetValue(declaration.Name, out var current) || candidate.Beats(current))
        {
            winners[declaration.Name] = candidate;
        }
    }

    /// <summary>
    /// One entry per supported CSS property: converts the raw declaration value and applies it
    /// to <see cref="ComputedStyle"/>. Returns false when the value could not be converted, so the
    /// dispatcher can report it via <see cref="CssDiagnostics"/> instead of silently dropping it.
    /// </summary>
    private delegate bool PropertyApplier(
        ComputedStyle style,
        string value,
        ComputedStyle? parent,
        float viewportWidth,
        float viewportHeight,
        float rootFontSize);

    private static readonly Dictionary<string, PropertyApplier> PropertyBindings = BuildPropertyBindings();

    private static void Apply(
        ComputedStyle style,
        string property,
        string rawValue,
        ComputedStyle? parent,
        float viewportWidth,
        float viewportHeight,
        float rootFontSize)
    {
        var value = rawValue.Trim();
        if (!PropertyBindings.TryGetValue(property, out var applier))
        {
            CssDiagnostics.ReportUnknownProperty(property, value);
            return;
        }

        if (!applier(style, value, parent, viewportWidth, viewportHeight, rootFontSize))
        {
            CssDiagnostics.ReportUnsupportedValue(property, value);
        }
    }

    private static Dictionary<string, PropertyApplier> BuildPropertyBindings()
    {
        var map = new Dictionary<string, PropertyApplier>(StringComparer.OrdinalIgnoreCase);

        void Keyword(string name, Action<ComputedStyle, string> apply) =>
            map[name] = (style, value, _, _, _, _) => { apply(style, value); return true; };

        void Length(string name, Action<ComputedStyle, CssLength> apply) =>
            map[name] = (style, value, _, _, _, _) => TryParseLength(value, length => apply(style, length));

        void Edges(string name, Func<ComputedStyle, CssEdges> current, Action<ComputedStyle, CssEdges> apply) =>
            map[name] = (style, value, _, _, _, _) => TryParseEdges(value, edges => apply(style, edges));

        Keyword("display", static (style, value) => style.Display = value.ToLowerInvariant() switch
        {
            "none" => DisplayKind.None,
            "block" => DisplayKind.Block,
            "inline-block" => DisplayKind.InlineBlock,
            "flex" => DisplayKind.Flex,
            "br" => DisplayKind.LineBreak,
            _ => DisplayKind.Inline,
        });

        Length("width", static (style, length) => style.Width = length);
        Length("height", static (style, length) => style.Height = length);
        Length("min-width", static (style, length) => style.MinWidth = length);
        Length("min-height", static (style, length) => style.MinHeight = length);
        Length("max-width", static (style, length) => style.MaxWidth = length);
        Length("max-height", static (style, length) => style.MaxHeight = length);

        Edges("margin", static style => style.Margin, static (style, edges) => style.Margin = edges);
        Length("margin-left", static (style, length) => style.Margin = style.Margin with { Left = length });
        Length("margin-top", static (style, length) => style.Margin = style.Margin with { Top = length });
        Length("margin-right", static (style, length) => style.Margin = style.Margin with { Right = length });
        Length("margin-bottom", static (style, length) => style.Margin = style.Margin with { Bottom = length });

        Edges("padding", static style => style.Padding, static (style, edges) => style.Padding = edges);
        Length("padding-left", static (style, length) => style.Padding = style.Padding with { Left = length });
        Length("padding-top", static (style, length) => style.Padding = style.Padding with { Top = length });
        Length("padding-right", static (style, length) => style.Padding = style.Padding with { Right = length });
        Length("padding-bottom", static (style, length) => style.Padding = style.Padding with { Bottom = length });

        map["border"] = static (style, value, _, _, _, _) => { ApplyBorder(style, value); return true; };
        Edges("border-width", static style => style.BorderWidth, static (style, edges) => style.BorderWidth = edges);
        Length("border-left-width", static (style, length) => style.BorderWidth = style.BorderWidth with { Left = length });
        Length("border-top-width", static (style, length) => style.BorderWidth = style.BorderWidth with { Top = length });
        Length("border-right-width", static (style, length) => style.BorderWidth = style.BorderWidth with { Right = length });
        Length("border-bottom-width", static (style, length) => style.BorderWidth = style.BorderWidth with { Bottom = length });

        map["border-color"] = static (style, value, _, _, _, _) =>
        {
            if (!ColorRgba.TryParse(value, out var color)) return false;
            style.BorderColor = color;
            return true;
        };

        map["border-radius"] = static (style, value, _, _, _, _) =>
        {
            if (!CssLength.TryParse(value, out var radius)) return false;
            style.BorderRadius = radius;
            return true;
        };

        PropertyApplier backgroundColor = static (style, value, _, _, _, _) =>
        {
            if (!ColorRgba.TryParse(value, out var background)) return false;
            style.BackgroundColor = background;
            return true;
        };
        map["background"] = backgroundColor;
        map["background-color"] = backgroundColor;

        map["color"] = static (style, value, _, _, _, _) =>
        {
            if (!ColorRgba.TryParse(value, out var color)) return false;
            style.Color = color;
            return true;
        };

        map["opacity"] = static (style, value, _, _, _, _) =>
        {
            if (!TryFloat(value, out var opacity)) return false;
            style.Opacity = Math.Clamp(opacity, 0, 1);
            return true;
        };

        Keyword("font-family", static (style, value) => style.FontFamily = value.Trim('\'', '"').Split(',')[0].Trim());

        map["font-size"] = static (style, value, parent, viewportWidth, viewportHeight, rootFontSize) =>
        {
            if (!CssLength.TryParse(value, out var fontSize)) return false;
            var inheritedSize = parent?.FontSize ?? 16;
            var resolved = fontSize.Resolve(inheritedSize, inheritedSize, rootFontSize, viewportWidth, viewportHeight);
            if (!float.IsFinite(resolved) || resolved <= 0) return false;
            style.FontSize = resolved;
            return true;
        };

        Keyword("font-weight", static (style, value) => style.FontWeight = ParseFontWeight(value));

        map["line-height"] = static (style, value, _, viewportWidth, viewportHeight, rootFontSize) =>
        {
            if (TryFloat(value, out var multiplier))
            {
                style.LineHeight = Math.Max(.5f, multiplier);
                return true;
            }

            if (CssLength.TryParse(value, out var lineHeight))
            {
                var resolved = lineHeight.Resolve(style.FontSize, style.FontSize, rootFontSize, viewportWidth, viewportHeight);
                if (float.IsFinite(resolved) && resolved > 0)
                {
                    style.LineHeight = resolved / style.FontSize;
                    return true;
                }
            }

            return false;
        };

        Keyword("text-align", static (style, value) => style.TextAlign = value.ToLowerInvariant() switch
        {
            "center" => TextAlign.Center,
            "right" or "end" => TextAlign.End,
            _ => TextAlign.Start,
        });
        Keyword("box-sizing", static (style, value) => style.BoxSizing =
            value.Equals("border-box", StringComparison.OrdinalIgnoreCase) ? BoxSizing.BorderBox : BoxSizing.ContentBox);
        Keyword("overflow", static (style, value) => style.Overflow =
            value.Equals("hidden", StringComparison.OrdinalIgnoreCase) ? OverflowKind.Hidden : OverflowKind.Visible);
        Keyword("visibility", static (style, value) => style.Visible = !value.Equals("hidden", StringComparison.OrdinalIgnoreCase));
        Keyword("cursor", static (style, value) => style.Cursor = value.ToLowerInvariant() switch
        {
            "pointer" => CursorKind.Pointer,
            "text" => CursorKind.Text,
            _ => CursorKind.Default,
        });
        Keyword("flex-direction", static (style, value) => style.FlexDirection =
            value.Equals("column", StringComparison.OrdinalIgnoreCase) ? FlexDirection.Column : FlexDirection.Row);
        Keyword("justify-content", static (style, value) => style.JustifyContent = value.ToLowerInvariant() switch
        {
            "center" => JustifyContent.Center,
            "end" or "flex-end" => JustifyContent.End,
            "space-between" => JustifyContent.SpaceBetween,
            _ => JustifyContent.Start,
        });
        Keyword("align-items", static (style, value) => style.AlignItems = value.ToLowerInvariant() switch
        {
            "center" => AlignItems.Center,
            "end" or "flex-end" => AlignItems.End,
            "start" or "flex-start" => AlignItems.Start,
            _ => AlignItems.Stretch,
        });

        map["flex-grow"] = static (style, value, _, _, _, _) =>
        {
            if (!TryFloat(value, out var grow)) return false;
            style.FlexGrow = Math.Max(0, grow);
            return true;
        };

        map["gap"] = static (style, value, _, viewportWidth, viewportHeight, rootFontSize) =>
        {
            if (!CssLength.TryParse(value, out var gap)) return false;
            style.Gap = Math.Max(0, ResolveLength(gap, viewportWidth, style.FontSize, rootFontSize, viewportWidth, viewportHeight));
            return true;
        };

        return map;
    }

    private static readonly HashSet<string> BorderStyleKeywords = new(StringComparer.OrdinalIgnoreCase)
    {
        "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset", "hidden",
    };

    /// <summary>
    /// Splits a shorthand value on whitespace, treating a parenthesized function call
    /// (e.g. <c>rgba(255, 255, 255, 0.1)</c>) as a single token even though it contains spaces.
    /// </summary>
    private static IEnumerable<string> TokenizeRespectingParens(string value)
    {
        var tokens = new List<string>();
        var current = new StringBuilder();
        var depth = 0;
        foreach (var character in value)
        {
            if (character == '(')
            {
                depth++;
                current.Append(character);
            }
            else if (character == ')')
            {
                depth = Math.Max(0, depth - 1);
                current.Append(character);
            }
            else if (char.IsWhiteSpace(character) && depth == 0)
            {
                if (current.Length > 0)
                {
                    tokens.Add(current.ToString());
                    current.Clear();
                }
            }
            else
            {
                current.Append(character);
            }
        }

        if (current.Length > 0)
        {
            tokens.Add(current.ToString());
        }

        return tokens;
    }

    private static void ApplyBorder(ComputedStyle style, string value)
    {
        foreach (var token in TokenizeRespectingParens(value))
        {
            if (token.Equals("none", StringComparison.OrdinalIgnoreCase))
            {
                style.BorderWidth = new CssEdges(CssLength.Zero);
            }
            else if (CssLength.TryParse(token, out var width))
            {
                style.BorderWidth = new CssEdges(width);
            }
            else if (ColorRgba.TryParse(token, out var color))
            {
                style.BorderColor = color;
            }
            else if (!BorderStyleKeywords.Contains(token))
            {
                // Border-style keywords (solid, dashed, ...) are recognized but not modeled;
                // anything else is a genuinely unsupported token worth surfacing.
                CssDiagnostics.ReportUnsupportedValue("border", token);
            }
        }
    }

    private static bool TryParseEdges(string value, Action<CssEdges> setter)
    {
        var parts = value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var parsed = parts.Select(static part => CssLength.TryParse(part, out var length) ? length : (CssLength?)null).ToArray();
        if (parsed.Length is < 1 or > 4 || parsed.Any(static length => length is null))
        {
            return false;
        }

        var edges = parsed.Length switch
        {
            1 => new CssEdges(parsed[0]!.Value),
            2 => new CssEdges(parsed[1]!.Value, parsed[0]!.Value, parsed[1]!.Value, parsed[0]!.Value),
            3 => new CssEdges(parsed[1]!.Value, parsed[0]!.Value, parsed[1]!.Value, parsed[2]!.Value),
            _ => new CssEdges(parsed[3]!.Value, parsed[0]!.Value, parsed[1]!.Value, parsed[2]!.Value),
        };
        setter(edges);
        return true;
    }

    private static bool TryParseLength(string value, Action<CssLength> setter)
    {
        if (!CssLength.TryParse(value, out var length))
        {
            return false;
        }

        setter(length);
        return true;
    }

    private static float ResolveLength(CssLength length, float reference, float fontSize, float rootFontSize, float viewportWidth, float viewportHeight)
    {
        var resolved = length.Resolve(reference, fontSize, rootFontSize, viewportWidth, viewportHeight);
        return float.IsFinite(resolved) ? resolved : 0;
    }

    private static bool TryFloat(string value, out float result) =>
        float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out result) && float.IsFinite(result);

    private static FontWeight ParseFontWeight(string value)
    {
        if (value.Equals("bold", StringComparison.OrdinalIgnoreCase)) return FontWeight.Bold;
        if (value.Equals("normal", StringComparison.OrdinalIgnoreCase)) return FontWeight.Normal;
        if (!ushort.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var numeric)) return FontWeight.Normal;
        numeric = (ushort)Math.Clamp((int)Math.Round(numeric / 100d) * 100, 100, 900);
        return (FontWeight)numeric;
    }

    private sealed record Winner(CssDeclaration Declaration, int Specificity, int Order)
    {
        public bool Beats(Winner current)
        {
            if (Declaration.Important != current.Declaration.Important)
            {
                return Declaration.Important;
            }

            return Specificity > current.Specificity || (Specificity == current.Specificity && Order >= current.Order);
        }
    }
}
