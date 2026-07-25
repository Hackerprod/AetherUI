using System.Text;

namespace AetherUI.StyleRuntime;

internal static class TextSplitter
{
    public static IReadOnlyList<string> SplitTopLevel(string source, char separator)
    {
        var result = new List<string>();
        var start = 0;
        var parentheses = 0;
        var brackets = 0;
        var braces = 0;
        var quote = '\0';
        var escaped = false;

        for (var index = 0; index < source.Length; index++)
        {
            var current = source[index];
            if (quote != '\0')
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (current == '\\')
                {
                    escaped = true;
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
                continue;
            }

            switch (current)
            {
                case '(':
                    parentheses++;
                    break;
                case ')':
                    parentheses = Math.Max(0, parentheses - 1);
                    break;
                case '[':
                    brackets++;
                    break;
                case ']':
                    brackets = Math.Max(0, brackets - 1);
                    break;
                case '{':
                    braces++;
                    break;
                case '}':
                    braces = Math.Max(0, braces - 1);
                    break;
                default:
                    if (current == separator && parentheses == 0 && brackets == 0 && braces == 0)
                    {
                        result.Add(source[start..index].Trim());
                        start = index + 1;
                    }

                    break;
            }
        }

        result.Add(source[start..].Trim());
        return result;
    }

    public static IReadOnlyList<string> SplitWhitespaceTopLevel(string source)
    {
        var result = new List<string>();
        var token = new StringBuilder();
        var parentheses = 0;
        var brackets = 0;
        var quote = '\0';
        var escaped = false;

        void Flush()
        {
            if (token.Length == 0)
            {
                return;
            }

            result.Add(token.ToString());
            token.Clear();
        }

        foreach (var current in source)
        {
            if (quote != '\0')
            {
                token.Append(current);
                if (escaped)
                {
                    escaped = false;
                }
                else if (current == '\\')
                {
                    escaped = true;
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
                token.Append(current);
                continue;
            }

            if (current == '(')
            {
                parentheses++;
                token.Append(current);
                continue;
            }

            if (current == ')')
            {
                parentheses = Math.Max(0, parentheses - 1);
                token.Append(current);
                continue;
            }

            if (current == '[')
            {
                brackets++;
                token.Append(current);
                continue;
            }

            if (current == ']')
            {
                brackets = Math.Max(0, brackets - 1);
                token.Append(current);
                continue;
            }

            if (char.IsWhiteSpace(current) && parentheses == 0 && brackets == 0)
            {
                Flush();
                continue;
            }

            token.Append(current);
        }

        Flush();
        return result;
    }

    public static int FindTopLevel(string source, char value)
    {
        var parentheses = 0;
        var brackets = 0;
        var quote = '\0';
        var escaped = false;
        for (var index = 0; index < source.Length; index++)
        {
            var current = source[index];
            if (quote != '\0')
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (current == '\\')
                {
                    escaped = true;
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
                continue;
            }

            switch (current)
            {
                case '(':
                    parentheses++;
                    continue;
                case ')':
                    parentheses = Math.Max(0, parentheses - 1);
                    continue;
                case '[':
                    brackets++;
                    continue;
                case ']':
                    brackets = Math.Max(0, brackets - 1);
                    continue;
            }

            if (current == value && parentheses == 0 && brackets == 0)
            {
                return index;
            }
        }

        return -1;
    }

    public static string Unquote(string text)
    {
        text = text.Trim();
        if (text.Length >= 2 && text[0] == text[^1] && (text[0] is '\'' or '"'))
        {
            return Unescape(text[1..^1]);
        }

        return text;
    }

    public static string Unescape(string source)
    {
        if (!source.Contains('\\'))
        {
            return source;
        }

        var result = new StringBuilder(source.Length);
        var escaped = false;
        foreach (var current in source)
        {
            if (escaped)
            {
                result.Append(current);
                escaped = false;
            }
            else if (current == '\\')
            {
                escaped = true;
            }
            else
            {
                result.Append(current);
            }
        }

        if (escaped)
        {
            result.Append('\\');
        }

        return result.ToString();
    }

    public static string RemoveComments(string source)
    {
        var output = new StringBuilder(source.Length);
        var index = 0;
        var quote = '\0';
        var escaped = false;
        while (index < source.Length)
        {
            var current = source[index];
            if (quote != '\0')
            {
                output.Append(current);
                if (escaped)
                {
                    escaped = false;
                }
                else if (current == '\\')
                {
                    escaped = true;
                }
                else if (current == quote)
                {
                    quote = '\0';
                }

                index++;
                continue;
            }

            if (current is '\'' or '"')
            {
                quote = current;
                output.Append(current);
                index++;
                continue;
            }

            if (current == '/' && index + 1 < source.Length && source[index + 1] == '*')
            {
                output.Append(' ');
                output.Append(' ');
                index += 2;
                while (index < source.Length && !(source[index] == '*' && index + 1 < source.Length && source[index + 1] == '/'))
                {
                    output.Append(source[index] == '\n' ? '\n' : ' ');
                    index++;
                }

                if (index + 1 < source.Length)
                {
                    output.Append(' ');
                    output.Append(' ');
                    index += 2;
                }

                continue;
            }

            output.Append(current);
            index++;
        }

        return output.ToString();
    }
}
