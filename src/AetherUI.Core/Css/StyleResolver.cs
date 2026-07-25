using System.Globalization;
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
        switch (property)
        {
            case "display":
                style.Display = value.ToLowerInvariant() switch
                {
                    "none" => DisplayKind.None,
                    "block" => DisplayKind.Block,
                    "inline-block" => DisplayKind.InlineBlock,
                    "flex" => DisplayKind.Flex,
                    "br" => DisplayKind.LineBreak,
                    _ => DisplayKind.Inline,
                };
                break;
            case "width": SetLength(value, length => style.Width = length); break;
            case "height": SetLength(value, length => style.Height = length); break;
            case "min-width": SetLength(value, length => style.MinWidth = length); break;
            case "min-height": SetLength(value, length => style.MinHeight = length); break;
            case "max-width": SetLength(value, length => style.MaxWidth = length); break;
            case "max-height": SetLength(value, length => style.MaxHeight = length); break;
            case "margin": style.Margin = ParseEdges(value, style.Margin); break;
            case "margin-left": SetEdge(value, style.Margin, edge => style.Margin = style.Margin with { Left = edge }); break;
            case "margin-top": SetEdge(value, style.Margin, edge => style.Margin = style.Margin with { Top = edge }); break;
            case "margin-right": SetEdge(value, style.Margin, edge => style.Margin = style.Margin with { Right = edge }); break;
            case "margin-bottom": SetEdge(value, style.Margin, edge => style.Margin = style.Margin with { Bottom = edge }); break;
            case "padding": style.Padding = ParseEdges(value, style.Padding); break;
            case "padding-left": SetEdge(value, style.Padding, edge => style.Padding = style.Padding with { Left = edge }); break;
            case "padding-top": SetEdge(value, style.Padding, edge => style.Padding = style.Padding with { Top = edge }); break;
            case "padding-right": SetEdge(value, style.Padding, edge => style.Padding = style.Padding with { Right = edge }); break;
            case "padding-bottom": SetEdge(value, style.Padding, edge => style.Padding = style.Padding with { Bottom = edge }); break;
            case "border": ApplyBorder(style, value); break;
            case "border-width": style.BorderWidth = ParseEdges(value, style.BorderWidth); break;
            case "border-left-width": SetEdge(value, style.BorderWidth, edge => style.BorderWidth = style.BorderWidth with { Left = edge }); break;
            case "border-top-width": SetEdge(value, style.BorderWidth, edge => style.BorderWidth = style.BorderWidth with { Top = edge }); break;
            case "border-right-width": SetEdge(value, style.BorderWidth, edge => style.BorderWidth = style.BorderWidth with { Right = edge }); break;
            case "border-bottom-width": SetEdge(value, style.BorderWidth, edge => style.BorderWidth = style.BorderWidth with { Bottom = edge }); break;
            case "border-color": if (ColorRgba.TryParse(value, out var borderColor)) style.BorderColor = borderColor; break;
            case "border-radius": if (CssLength.TryParse(value, out var radius)) style.BorderRadius = ResolveLength(radius, style.FontSize, style.FontSize, rootFontSize, viewportWidth, viewportHeight); break;
            case "background":
            case "background-color": if (ColorRgba.TryParse(value, out var background)) style.BackgroundColor = background; break;
            case "color": if (ColorRgba.TryParse(value, out var color)) style.Color = color; break;
            case "opacity": if (TryFloat(value, out var opacity)) style.Opacity = Math.Clamp(opacity, 0, 1); break;
            case "font-family": style.FontFamily = value.Trim('\'', '"').Split(',')[0].Trim(); break;
            case "font-size":
                if (CssLength.TryParse(value, out var fontSize))
                {
                    var inheritedSize = parent?.FontSize ?? 16;
                    var resolved = fontSize.Resolve(inheritedSize, inheritedSize, rootFontSize, viewportWidth, viewportHeight);
                    if (float.IsFinite(resolved) && resolved > 0) style.FontSize = resolved;
                }
                break;
            case "font-weight": style.FontWeight = ParseFontWeight(value); break;
            case "line-height":
                if (TryFloat(value, out var multiplier)) style.LineHeight = Math.Max(.5f, multiplier);
                else if (CssLength.TryParse(value, out var lineHeight))
                {
                    var resolved = lineHeight.Resolve(style.FontSize, style.FontSize, rootFontSize, viewportWidth, viewportHeight);
                    if (float.IsFinite(resolved) && resolved > 0) style.LineHeight = resolved / style.FontSize;
                }
                break;
            case "text-align": style.TextAlign = value.ToLowerInvariant() switch { "center" => TextAlign.Center, "right" or "end" => TextAlign.End, _ => TextAlign.Start }; break;
            case "box-sizing": style.BoxSizing = value.Equals("border-box", StringComparison.OrdinalIgnoreCase) ? BoxSizing.BorderBox : BoxSizing.ContentBox; break;
            case "overflow": style.Overflow = value.Equals("hidden", StringComparison.OrdinalIgnoreCase) ? OverflowKind.Hidden : OverflowKind.Visible; break;
            case "visibility": style.Visible = !value.Equals("hidden", StringComparison.OrdinalIgnoreCase); break;
            case "cursor": style.Cursor = value.ToLowerInvariant() switch { "pointer" => CursorKind.Pointer, "text" => CursorKind.Text, _ => CursorKind.Default }; break;
            case "flex-direction": style.FlexDirection = value.Equals("column", StringComparison.OrdinalIgnoreCase) ? FlexDirection.Column : FlexDirection.Row; break;
            case "justify-content": style.JustifyContent = value.ToLowerInvariant() switch { "center" => JustifyContent.Center, "end" or "flex-end" => JustifyContent.End, "space-between" => JustifyContent.SpaceBetween, _ => JustifyContent.Start }; break;
            case "align-items": style.AlignItems = value.ToLowerInvariant() switch { "center" => AlignItems.Center, "end" or "flex-end" => AlignItems.End, "start" or "flex-start" => AlignItems.Start, _ => AlignItems.Stretch }; break;
            case "flex-grow": if (TryFloat(value, out var grow)) style.FlexGrow = Math.Max(0, grow); break;
            case "gap": if (CssLength.TryParse(value, out var gap)) style.Gap = Math.Max(0, ResolveLength(gap, viewportWidth, style.FontSize, rootFontSize, viewportWidth, viewportHeight)); break;
        }
    }

    private static void ApplyBorder(ComputedStyle style, string value)
    {
        foreach (var token in value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
        {
            if (CssLength.TryParse(token, out var width))
            {
                style.BorderWidth = new CssEdges(width);
            }
            else if (ColorRgba.TryParse(token, out var color))
            {
                style.BorderColor = color;
            }
        }
    }

    private static CssEdges ParseEdges(string value, CssEdges fallback)
    {
        var parts = value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var parsed = parts.Select(static part => CssLength.TryParse(part, out var length) ? length : (CssLength?)null).ToArray();
        if (parsed.Any(static length => length is null) || parsed.Length is < 1 or > 4)
        {
            return fallback;
        }

        return parsed.Length switch
        {
            1 => new CssEdges(parsed[0]!.Value),
            2 => new CssEdges(parsed[1]!.Value, parsed[0]!.Value, parsed[1]!.Value, parsed[0]!.Value),
            3 => new CssEdges(parsed[1]!.Value, parsed[0]!.Value, parsed[1]!.Value, parsed[2]!.Value),
            4 => new CssEdges(parsed[3]!.Value, parsed[0]!.Value, parsed[1]!.Value, parsed[2]!.Value),
            _ => fallback,
        };
    }

    private static void SetLength(string value, Action<CssLength> setter)
    {
        if (CssLength.TryParse(value, out var length)) setter(length);
    }

    private static void SetEdge(string value, CssEdges _, Action<CssLength> setter)
    {
        if (CssLength.TryParse(value, out var length)) setter(length);
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
