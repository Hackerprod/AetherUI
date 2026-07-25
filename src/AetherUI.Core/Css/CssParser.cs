namespace AetherUI.Css;

public sealed record CssDeclaration(string Name, string Value, bool Important);

public sealed record CssRule(IReadOnlyList<CssSelector> Selectors, IReadOnlyList<CssDeclaration> Declarations, int SourceOrder);

public sealed class CssStyleSheet
{
    public CssStyleSheet(IReadOnlyList<CssRule> rules)
    {
        Rules = rules;
    }

    public IReadOnlyList<CssRule> Rules { get; }

    public static CssStyleSheet Parse(string? source)
    {
        if (string.IsNullOrWhiteSpace(source))
        {
            return new CssStyleSheet([]);
        }

        return new CssParser(RemoveComments(source)).ParseStyleSheet();
    }

    private static string RemoveComments(string source)
    {
        var result = new System.Text.StringBuilder(source.Length);
        var position = 0;
        while (position < source.Length)
        {
            var start = source.IndexOf("/*", position, StringComparison.Ordinal);
            if (start < 0)
            {
                result.Append(source, position, source.Length - position);
                break;
            }

            result.Append(source, position, start - position);
            var end = source.IndexOf("*/", start + 2, StringComparison.Ordinal);
            position = end < 0 ? source.Length : end + 2;
        }

        return result.ToString();
    }
}

internal sealed class CssParser
{
    private readonly string _source;
    private int _position;
    private int _sourceOrder;

    public CssParser(string source)
    {
        _source = source;
    }

    public CssStyleSheet ParseStyleSheet()
    {
        var rules = new List<CssRule>();
        while (_position < _source.Length)
        {
            SkipWhitespace();
            if (_position >= _source.Length)
            {
                break;
            }

            if (_source[_position] == '@')
            {
                SkipAtRule();
                continue;
            }

            var openingBrace = FindNextTopLevel('{', _position);
            if (openingBrace < 0)
            {
                break;
            }

            var selectorText = _source[_position..openingBrace].Trim();
            var closingBrace = FindMatchingBrace(openingBrace);
            if (closingBrace < 0)
            {
                break;
            }

            var declarationText = _source[(openingBrace + 1)..closingBrace];
            _position = closingBrace + 1;

            var selectors = SplitTopLevel(selectorText, ',')
                .Select(static text => CssSelector.TryParse(text, out var selector) ? selector : null)
                .Where(static selector => selector is not null)
                .Cast<CssSelector>()
                .ToArray();
            if (selectors.Length == 0)
            {
                continue;
            }

            var declarations = ParseDeclarations(declarationText);
            if (declarations.Count > 0)
            {
                rules.Add(new CssRule(selectors, declarations, _sourceOrder++));
            }
        }

        return new CssStyleSheet(rules);
    }

    public static IReadOnlyList<CssDeclaration> ParseDeclarations(string source)
    {
        var declarations = new List<CssDeclaration>();
        foreach (var entry in SplitTopLevel(source, ';'))
        {
            var colon = FindTopLevelColon(entry);
            if (colon <= 0)
            {
                continue;
            }

            var name = entry[..colon].Trim().ToLowerInvariant();
            var value = entry[(colon + 1)..].Trim();
            if (name.Length == 0 || value.Length == 0)
            {
                continue;
            }

            var important = value.EndsWith("!important", StringComparison.OrdinalIgnoreCase);
            if (important)
            {
                value = value[..^10].TrimEnd();
            }

            declarations.Add(new CssDeclaration(name, value, important));
        }

        return declarations;
    }

    internal static IReadOnlyList<string> SplitTopLevel(string source, char separator)
    {
        var result = new List<string>();
        var start = 0;
        var parentheses = 0;
        var brackets = 0;
        char quote = '\0';

        for (var i = 0; i < source.Length; i++)
        {
            var current = source[i];
            if (quote != '\0')
            {
                if (current == '\\')
                {
                    i++;
                }
                else if (current == quote)
                {
                    quote = '\0';
                }

                continue;
            }

            if (current is '\'' or '"')
            {
                quote = current;
            }
            else if (current == '(')
            {
                parentheses++;
            }
            else if (current == ')')
            {
                parentheses = Math.Max(0, parentheses - 1);
            }
            else if (current == '[')
            {
                brackets++;
            }
            else if (current == ']')
            {
                brackets = Math.Max(0, brackets - 1);
            }
            else if (current == separator && parentheses == 0 && brackets == 0)
            {
                result.Add(source[start..i]);
                start = i + 1;
            }
        }

        result.Add(source[start..]);
        return result;
    }

    private static int FindTopLevelColon(string source)
    {
        var parentheses = 0;
        char quote = '\0';
        for (var i = 0; i < source.Length; i++)
        {
            var current = source[i];
            if (quote != '\0')
            {
                if (current == '\\')
                {
                    i++;
                }
                else if (current == quote)
                {
                    quote = '\0';
                }

                continue;
            }

            if (current is '\'' or '"')
            {
                quote = current;
            }
            else if (current == '(')
            {
                parentheses++;
            }
            else if (current == ')')
            {
                parentheses = Math.Max(0, parentheses - 1);
            }
            else if (current == ':' && parentheses == 0)
            {
                return i;
            }
        }

        return -1;
    }

    private void SkipAtRule()
    {
        var semicolon = FindNextTopLevel(';', _position);
        var brace = FindNextTopLevel('{', _position);
        if (brace >= 0 && (semicolon < 0 || brace < semicolon))
        {
            var close = FindMatchingBrace(brace);
            _position = close < 0 ? _source.Length : close + 1;
        }
        else
        {
            _position = semicolon < 0 ? _source.Length : semicolon + 1;
        }
    }

    private int FindMatchingBrace(int openingBrace)
    {
        var depth = 0;
        char quote = '\0';
        for (var i = openingBrace; i < _source.Length; i++)
        {
            var current = _source[i];
            if (quote != '\0')
            {
                if (current == '\\')
                {
                    i++;
                }
                else if (current == quote)
                {
                    quote = '\0';
                }

                continue;
            }

            if (current is '\'' or '"')
            {
                quote = current;
            }
            else if (current == '{')
            {
                depth++;
            }
            else if (current == '}' && --depth == 0)
            {
                return i;
            }
        }

        return -1;
    }

    private int FindNextTopLevel(char target, int start)
    {
        char quote = '\0';
        var parentheses = 0;
        for (var i = start; i < _source.Length; i++)
        {
            var current = _source[i];
            if (quote != '\0')
            {
                if (current == '\\')
                {
                    i++;
                }
                else if (current == quote)
                {
                    quote = '\0';
                }

                continue;
            }

            if (current is '\'' or '"')
            {
                quote = current;
            }
            else if (current == '(')
            {
                parentheses++;
            }
            else if (current == ')')
            {
                parentheses = Math.Max(0, parentheses - 1);
            }
            else if (current == target && parentheses == 0)
            {
                return i;
            }
        }

        return -1;
    }

    private void SkipWhitespace()
    {
        while (_position < _source.Length && char.IsWhiteSpace(_source[_position]))
        {
            _position++;
        }
    }
}
