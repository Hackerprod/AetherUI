using System.Net;
using AetherUI.Diagnostics;

namespace AetherUI.Dom;

public sealed class HtmlParser
{
    private static readonly HashSet<string> VoidElements = new(StringComparer.OrdinalIgnoreCase)
    {
        "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr",
    };

    private readonly string _source;
    private int _position;

    private HtmlParser(string source)
    {
        _source = source;
    }

    public static DocumentNode Parse(string source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return new HtmlParser(source).ParseDocument();
    }

    private DocumentNode ParseDocument()
    {
        var document = new DocumentNode();
        var stack = new Stack<Node>();
        stack.Push(document);

        while (!End)
        {
            if (Current != '<')
            {
                ParseText(stack.Peek());
                continue;
            }

            if (StartsWith("<!--"))
            {
                ParseComment(stack.Peek());
                continue;
            }

            if (StartsWith("<!", StringComparison.Ordinal))
            {
                SkipDeclaration();
                continue;
            }

            if (StartsWith("</", StringComparison.Ordinal))
            {
                ParseEndTag(stack);
                continue;
            }

            if (StartsWith("<?", StringComparison.Ordinal))
            {
                SkipProcessingInstruction();
                continue;
            }

            ParseStartTag(stack);
        }

        return document;
    }

    private void ParseText(Node parent)
    {
        var start = _position;
        var next = _source.IndexOf('<', _position);
        _position = next < 0 ? _source.Length : next;
        if (_position <= start)
        {
            return;
        }

        var decoded = WebUtility.HtmlDecode(_source[start.._position]);
        if (decoded.Length > 0)
        {
            parent.AppendChild(new TextNode(decoded));
        }
    }

    private void ParseComment(Node parent)
    {
        _position += 4;
        var end = _source.IndexOf("-->", _position, StringComparison.Ordinal);
        if (end < 0)
        {
            parent.AppendChild(new CommentNode(_source[_position..]));
            _position = _source.Length;
            return;
        }

        parent.AppendChild(new CommentNode(_source[_position..end]));
        _position = end + 3;
    }

    private void SkipDeclaration()
    {
        var end = _source.IndexOf('>', _position + 2);
        _position = end < 0 ? _source.Length : end + 1;
    }

    private void SkipProcessingInstruction()
    {
        var end = _source.IndexOf("?>", _position + 2, StringComparison.Ordinal);
        _position = end < 0 ? _source.Length : end + 2;
    }

    private void ParseEndTag(Stack<Node> stack)
    {
        _position += 2;
        SkipWhitespace();
        var name = ReadName().ToLowerInvariant();
        SkipUntilTagEnd();

        if (name.Length == 0)
        {
            return;
        }

        while (stack.Count > 1)
        {
            var node = stack.Pop();
            if (node is ElementNode element && string.Equals(element.TagName, name, StringComparison.OrdinalIgnoreCase))
            {
                break;
            }
        }
    }

    private void ParseStartTag(Stack<Node> stack)
    {
        var tagStart = _position;
        _position++;
        SkipWhitespace();
        var tagName = ReadName();
        if (tagName.Length == 0)
        {
            // A literal '<' that does not begin a valid tag.
            stack.Peek().AppendChild(new TextNode("<"));
            _position = Math.Min(tagStart + 1, _source.Length);
            return;
        }

        var element = new ElementNode(tagName);
        var selfClosing = false;

        while (!End)
        {
            SkipWhitespace();
            if (End)
            {
                break;
            }

            if (Current == '>')
            {
                _position++;
                break;
            }

            if (Current == '/' && Peek(1) == '>')
            {
                selfClosing = true;
                _position += 2;
                break;
            }

            var attributeName = ReadName();
            if (attributeName.Length == 0)
            {
                _position++;
                continue;
            }

            SkipWhitespace();
            var value = string.Empty;
            if (!End && Current == '=')
            {
                _position++;
                SkipWhitespace();
                value = ReadAttributeValue();
            }

            element.SetAttribute(attributeName, WebUtility.HtmlDecode(value));
        }

        stack.Peek().AppendChild(element);

        if (selfClosing || VoidElements.Contains(element.TagName))
        {
            return;
        }

        stack.Push(element);
        if (element.TagName is "style" or "script")
        {
            ParseRawText(element);
        }
    }

    private void ParseRawText(ElementNode element)
    {
        var close = FindRawTextClose(element.TagName, _position);
        if (close < 0)
        {
            if (_position < _source.Length)
            {
                element.AppendChild(new TextNode(_source[_position..]));
            }

            _position = _source.Length;
            return;
        }

        if (close > _position)
        {
            element.AppendChild(new TextNode(_source[_position..close]));
        }

        _position = close;
    }


    private int FindRawTextClose(string tagName, int start)
    {
        var needle = $"</{tagName}";
        var search = start;
        while (search < _source.Length)
        {
            var candidate = _source.IndexOf(needle, search, StringComparison.OrdinalIgnoreCase);
            if (candidate < 0)
            {
                return -1;
            }

            var after = candidate + needle.Length;
            if (after >= _source.Length || _source[after] == '>' || char.IsWhiteSpace(_source[after]))
            {
                return candidate;
            }

            search = candidate + needle.Length;
        }

        return -1;
    }

    private string ReadAttributeValue()
    {
        if (End)
        {
            return string.Empty;
        }

        if (Current is '\'' or '"')
        {
            var quote = Current;
            _position++;
            var start = _position;
            while (!End && Current != quote)
            {
                _position++;
            }

            var value = _source[start.._position];
            if (!End)
            {
                _position++;
            }

            return value;
        }

        var unquotedStart = _position;
        while (!End && !char.IsWhiteSpace(Current) && Current is not '>' and not '/')
        {
            _position++;
        }

        return _source[unquotedStart.._position];
    }

    private string ReadName()
    {
        var start = _position;
        while (!End && IsNameCharacter(Current))
        {
            _position++;
        }

        return _source[start.._position];
    }

    private void SkipWhitespace()
    {
        while (!End && char.IsWhiteSpace(Current))
        {
            _position++;
        }
    }

    private void SkipUntilTagEnd()
    {
        while (!End && Current != '>')
        {
            _position++;
        }

        if (!End)
        {
            _position++;
        }
    }

    private bool StartsWith(string value, StringComparison comparison = StringComparison.OrdinalIgnoreCase) =>
        _source.AsSpan(_position).StartsWith(value.AsSpan(), comparison);

    private char Current => _source[_position];
    private bool End => _position >= _source.Length;
    private char Peek(int offset) => _position + offset < _source.Length ? _source[_position + offset] : '\0';

    private static bool IsNameCharacter(char value) =>
        char.IsLetterOrDigit(value) || value is '-' or '_' or ':' or '.';
}
