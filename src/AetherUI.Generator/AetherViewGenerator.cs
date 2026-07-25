using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;

namespace AetherUI.Generator;

[Generator(LanguageNames.CSharp)]
public sealed class AetherViewGenerator : IIncrementalGenerator
{
    private static readonly DiagnosticDescriptor ReadFailure = new(
        "AUI001",
        "No se pudo leer la vista",
        "No se pudo leer el archivo de vista '{0}'",
        "AetherUI.Generator",
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor ParseFailure = new(
        "AUI002",
        "Vista .cshtml no válida",
        "{0}",
        "AetherUI.Generator",
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var rootNamespace = context.AnalyzerConfigOptionsProvider.Select(static (provider, _) =>
        {
            return provider.GlobalOptions.TryGetValue("build_property.RootNamespace", out var value) && !string.IsNullOrWhiteSpace(value)
                ? value
                : "AetherUI.Generated";
        });

        var views = context.AdditionalTextsProvider
            .Where(static file => file.Path.EndsWith(".cshtml", StringComparison.OrdinalIgnoreCase))
            .Select(static (file, cancellationToken) =>
            {
                var text = file.GetText(cancellationToken);
                return new ViewInput(file.Path, text);
            });

        context.RegisterSourceOutput(views.Combine(rootNamespace), static (productionContext, pair) =>
        {
            var input = pair.Left;
            if (input.Text is null)
            {
                productionContext.ReportDiagnostic(Diagnostic.Create(ReadFailure, Location.None, input.Path));
                return;
            }

            var compiler = new ViewCompiler(input.Path, input.Text, pair.Right);
            var result = compiler.Compile();
            foreach (var diagnostic in result.Diagnostics)
            {
                var lineSpan = input.Text.Lines.GetLinePositionSpan(diagnostic.Span);
                var location = Location.Create(input.Path, diagnostic.Span, lineSpan);
                productionContext.ReportDiagnostic(Diagnostic.Create(ParseFailure, location, diagnostic.Message));
            }

            if (result.Source is null)
            {
                return;
            }

            productionContext.AddSource(result.HintName, SourceText.From(result.Source, Encoding.UTF8));
        });
    }

    private sealed class ViewInput
    {
        public ViewInput(string path, SourceText? text)
        {
            Path = path;
            Text = text;
        }

        public string Path { get; }
        public SourceText? Text { get; }
    }
}

internal sealed class ViewCompiler
{
    private readonly string _path;
    private readonly SourceText _sourceText;
    private readonly string _source;
    private readonly string _rootNamespace;
    private readonly List<ViewDiagnostic> _diagnostics = new();
    private readonly List<string> _usings = new();
    private readonly List<CodeBlock> _codeBlocks = new();
    private string? _namespace;
    private string? _className;
    private string _baseType = "global::AetherUI.Runtime.UiComponent";

    public ViewCompiler(string path, SourceText sourceText, string rootNamespace)
    {
        _path = path;
        _sourceText = sourceText;
        _source = sourceText.ToString();
        _rootNamespace = rootNamespace;
    }

    public ViewCompilationResult Compile()
    {
        var markupBuffer = _source.ToCharArray();
        ParseLineDirectives(markupBuffer);
        ParseCodeBlocks(markupBuffer);

        if (_diagnostics.Count > 0)
        {
            return new ViewCompilationResult(null, BuildHintName(), _diagnostics.ToImmutableArray());
        }

        _namespace ??= _rootNamespace;
        _className ??= SanitizeIdentifier(Path.GetFileNameWithoutExtension(_path), "GeneratedView");
        if (!IsValidQualifiedName(_namespace))
        {
            AddDiagnostic(0, Math.Min(_source.Length, 1), $"El namespace '{_namespace}' no es válido.");
        }

        if (!SyntaxFacts.IsValidIdentifier(_className))
        {
            AddDiagnostic(0, Math.Min(_source.Length, 1), $"El nombre de clase '{_className}' no es válido.");
        }

        var markup = new string(markupBuffer);
        var markupCompiler = new MarkupCompiler(markup);
        var operations = markupCompiler.Compile();
        foreach (var diagnostic in markupCompiler.Diagnostics)
        {
            _diagnostics.Add(diagnostic);
        }

        if (_diagnostics.Count > 0)
        {
            return new ViewCompilationResult(null, BuildHintName(), _diagnostics.ToImmutableArray());
        }

        var source = GenerateSource(operations);
        return new ViewCompilationResult(source, BuildHintName(), _diagnostics.ToImmutableArray());
    }

    private void ParseLineDirectives(char[] buffer)
    {
        var position = 0;
        while (position < _source.Length)
        {
            var line = _sourceText.Lines.GetLineFromPosition(position);
            var lineStart = line.Start;
            var lineEnd = line.End;
            var text = _source.Substring(lineStart, lineEnd - lineStart);
            var trimmed = text.TrimStart();
            var leading = text.Length - trimmed.Length;
            if (trimmed.StartsWith("@namespace ", StringComparison.Ordinal))
            {
                AssignSingleDirective("namespace", trimmed.Substring(11).Trim().TrimEnd(';'), ref _namespace, lineStart + leading, lineEnd - lineStart);
                Blank(buffer, lineStart, line.EndIncludingLineBreak);
            }
            else if (trimmed.StartsWith("@class ", StringComparison.Ordinal))
            {
                AssignSingleDirective("class", trimmed.Substring(7).Trim().TrimEnd(';'), ref _className, lineStart + leading, lineEnd - lineStart);
                Blank(buffer, lineStart, line.EndIncludingLineBreak);
            }
            else if (trimmed.StartsWith("@inherits ", StringComparison.Ordinal))
            {
                var value = trimmed.Substring(10).Trim().TrimEnd(';');
                if (string.IsNullOrWhiteSpace(value))
                {
                    AddDiagnostic(lineStart, lineEnd - lineStart, "@inherits requiere un tipo base.");
                }
                else
                {
                    _baseType = value;
                }

                Blank(buffer, lineStart, line.EndIncludingLineBreak);
            }
            else if (trimmed.StartsWith("@using ", StringComparison.Ordinal))
            {
                var value = trimmed.Substring(7).Trim().TrimEnd(';');
                if (string.IsNullOrWhiteSpace(value))
                {
                    AddDiagnostic(lineStart, lineEnd - lineStart, "@using requiere un namespace o alias.");
                }
                else
                {
                    _usings.Add(value);
                }

                Blank(buffer, lineStart, line.EndIncludingLineBreak);
            }

            position = line.EndIncludingLineBreak;
            if (position <= lineStart)
            {
                break;
            }
        }
    }

    private void ParseCodeBlocks(char[] buffer)
    {
        var index = 0;
        while (index < _source.Length)
        {
            index = FindDirective(index, "@code");
            if (index < 0)
            {
                return;
            }

            var cursor = index + 5;
            while (cursor < _source.Length && char.IsWhiteSpace(_source[cursor]))
            {
                cursor++;
            }

            if (cursor >= _source.Length || _source[cursor] != '{')
            {
                AddDiagnostic(index, Math.Min(5, _source.Length - index), "@code debe estar seguido por un bloque entre llaves.");
                index += 5;
                continue;
            }

            var close = CSharpScanner.FindMatchingDelimiter(_source, cursor, '{', '}');
            if (close < 0)
            {
                AddDiagnostic(cursor, 1, "El bloque @code no tiene una llave de cierre.");
                return;
            }

            var contentStart = cursor + 1;
            var content = _source.Substring(contentStart, close - contentStart);
            var line = _sourceText.Lines.GetLineFromPosition(contentStart).LineNumber + 1;
            _codeBlocks.Add(new CodeBlock(content, line));
            Blank(buffer, index, close + 1);
            index = close + 1;
        }
    }

    private int FindDirective(int start, string directive)
    {
        var search = Math.Max(0, start);
        while (search <= _source.Length - directive.Length)
        {
            var index = _source.IndexOf(directive, search, StringComparison.Ordinal);
            if (index < 0)
            {
                return -1;
            }

            var lineStart = index;
            while (lineStart > 0 && _source[lineStart - 1] is not '\n' and not '\r')
            {
                lineStart--;
            }

            var onlyIndentationBefore = true;
            for (var cursor = lineStart; cursor < index; cursor++)
            {
                if (!char.IsWhiteSpace(_source[cursor]))
                {
                    onlyIndentationBefore = false;
                    break;
                }
            }

            var after = index + directive.Length;
            var afterIsBoundary = after >= _source.Length || char.IsWhiteSpace(_source[after]) || _source[after] == '{';
            if (onlyIndentationBefore && afterIsBoundary)
            {
                return index;
            }

            search = index + directive.Length;
        }

        return -1;
    }

    private string GenerateSource(IReadOnlyList<RenderOperation> operations)
    {
        var builder = new StringBuilder(Math.Max(4096, _source.Length * 2));
        builder.AppendLine("// <auto-generated />");
        builder.AppendLine("#nullable enable");
        builder.AppendLine("using global::System;");
        builder.AppendLine("using global::AetherUI.Events;");
        builder.AppendLine("using global::AetherUI.Runtime;");
        var emittedUsings = new HashSet<string>(StringComparer.Ordinal)
        {
            "System",
            "AetherUI.Events",
            "AetherUI.Runtime",
        };
        foreach (var usingValue in _usings)
        {
            if (emittedUsings.Add(usingValue))
            {
                builder.Append("using ").Append(usingValue).AppendLine(";");
            }
        }

        builder.AppendLine();
        builder.Append("namespace ").Append(_namespace).AppendLine();
        builder.AppendLine("{");
        builder.Append("    public sealed partial class ").Append(_className).Append(" : ").Append(_baseType).AppendLine();
        builder.AppendLine("    {");

        foreach (var codeBlock in _codeBlocks)
        {
            builder.Append("#line ").Append(codeBlock.StartLine).Append(' ').Append(Quote(_path)).AppendLine();
            builder.AppendLine(codeBlock.Content);
            builder.AppendLine("#line default");
            builder.AppendLine("#line hidden");
        }

        builder.AppendLine("        protected override void BuildRenderTree(global::AetherUI.Runtime.MarkupWriter writer)");
        builder.AppendLine("        {");
        foreach (var operation in operations)
        {
            switch (operation.Kind)
            {
                case RenderOperationKind.Literal:
                    if (operation.Value.Length > 0)
                    {
                        builder.Append("            writer.WriteLiteral(").Append(Quote(operation.Value)).AppendLine(");");
                    }
                    break;
                case RenderOperationKind.Expression:
                    builder.Append("            writer.Write(").Append(operation.Value).AppendLine(");");
                    break;
                case RenderOperationKind.Attribute:
                    builder.Append("            writer.WriteAttribute(").Append(Quote(operation.Name!)).Append(", ").Append(operation.Value).AppendLine(");");
                    break;
                case RenderOperationKind.Event:
                    builder.Append("            writer.WriteEventAttribute(").Append(Quote(operation.Name!)).Append(", ").Append(operation.Value).AppendLine(");");
                    break;
                default:
                    throw new InvalidOperationException("Operación de render desconocida.");
            }
        }

        builder.AppendLine("        }");
        builder.AppendLine("    }");
        builder.AppendLine("}");
        return builder.ToString();
    }

    private void AssignSingleDirective(string name, string value, ref string? target, int start, int length)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            AddDiagnostic(start, length, $"@{name} requiere un valor.");
            return;
        }

        if (target is not null)
        {
            AddDiagnostic(start, length, $"La directiva @{name} solo puede declararse una vez.");
            return;
        }

        target = value;
    }

    private static bool IsValidQualifiedName(string value)
    {
        var parts = value.Split('.');
        if (parts.Length == 0)
        {
            return false;
        }

        foreach (var part in parts)
        {
            if (!SyntaxFacts.IsValidIdentifier(part))
            {
                return false;
            }
        }

        return true;
    }

    private string BuildHintName()
    {
        var file = SanitizeIdentifier(Path.GetFileNameWithoutExtension(_path), "View");
        using var sha = SHA256.Create();
        var bytes = sha.ComputeHash(Encoding.UTF8.GetBytes(_path));
        var hash = BitConverter.ToString(bytes, 0, 6).Replace("-", string.Empty);
        return file + "." + hash + ".AetherUI.g.cs";
    }

    private static string SanitizeIdentifier(string value, string fallback)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return fallback;
        }

        var builder = new StringBuilder(value.Length + 1);
        if (!SyntaxFacts.IsIdentifierStartCharacter(value[0]))
        {
            builder.Append('_');
        }

        foreach (var character in value)
        {
            builder.Append(SyntaxFacts.IsIdentifierPartCharacter(character) ? character : '_');
        }

        return builder.Length == 0 ? fallback : builder.ToString();
    }

    private static string Quote(string value)
    {
        var builder = new StringBuilder(value.Length + 16);
        builder.Append('"');
        foreach (var character in value)
        {
            switch (character)
            {
                case '\\': builder.Append("\\\\"); break;
                case '"': builder.Append("\\\""); break;
                case '\r': builder.Append("\\r"); break;
                case '\n': builder.Append("\\n"); break;
                case '\t': builder.Append("\\t"); break;
                case '\0': builder.Append("\\0"); break;
                default:
                    if (char.IsControl(character))
                    {
                        builder.Append("\\u").Append(((int)character).ToString("x4"));
                    }
                    else
                    {
                        builder.Append(character);
                    }
                    break;
            }
        }

        return builder.Append('"').ToString();
    }

    private static void Blank(char[] buffer, int start, int endExclusive)
    {
        endExclusive = Math.Min(endExclusive, buffer.Length);
        for (var index = Math.Max(0, start); index < endExclusive; index++)
        {
            if (buffer[index] != '\r' && buffer[index] != '\n')
            {
                buffer[index] = ' ';
            }
        }
    }

    private void AddDiagnostic(int start, int length, string message)
    {
        var safeStart = Math.Max(0, Math.Min(start, _source.Length));
        var safeLength = Math.Max(0, Math.Min(length, _source.Length - safeStart));
        _diagnostics.Add(new ViewDiagnostic(new TextSpan(safeStart, safeLength), message));
    }

    private sealed class CodeBlock
    {
        public CodeBlock(string content, int startLine)
        {
            Content = content;
            StartLine = startLine;
        }

        public string Content { get; }
        public int StartLine { get; }
    }
}

internal sealed class MarkupCompiler
{
    private readonly string _markup;
    private readonly List<RenderOperation> _operations = new();
    private readonly List<ViewDiagnostic> _diagnostics = new();
    private readonly StringBuilder _literal = new();

    public MarkupCompiler(string markup)
    {
        _markup = markup;
    }

    public IReadOnlyList<ViewDiagnostic> Diagnostics => _diagnostics;

    public IReadOnlyList<RenderOperation> Compile()
    {
        var index = 0;
        var insideTag = false;
        var quote = '\0';
        while (index < _markup.Length)
        {
            if (insideTag && quote == '\0' && char.IsWhiteSpace(_markup[index]) && TryParseDynamicAttribute(index, out var attribute))
            {
                FlushLiteral();
                _operations.Add(attribute.Operation);
                index = attribute.End;
                continue;
            }

            var character = _markup[index];
            if (character == '@')
            {
                if (index + 1 < _markup.Length && _markup[index + 1] == '@')
                {
                    _literal.Append('@');
                    index += 2;
                    continue;
                }

                if (TryParseExpression(index, out var expression, out var end))
                {
                    FlushLiteral();
                    _operations.Add(new RenderOperation(RenderOperationKind.Expression, null, expression));
                    index = end;
                    continue;
                }
            }

            _literal.Append(character);
            if (insideTag)
            {
                if (quote == '\0' && (character == '"' || character == '\''))
                {
                    quote = character;
                }
                else if (quote != '\0' && character == quote)
                {
                    quote = '\0';
                }
                else if (quote == '\0' && character == '>')
                {
                    insideTag = false;
                }
            }
            else if (character == '<' && !StartsWith(index, "<!--"))
            {
                insideTag = true;
            }

            index++;
        }

        FlushLiteral();
        return _operations;
    }

    private bool TryParseDynamicAttribute(int start, out ParsedAttribute parsed)
    {
        parsed = default;
        var cursor = start;
        while (cursor < _markup.Length && char.IsWhiteSpace(_markup[cursor]))
        {
            cursor++;
        }

        var nameStart = cursor;
        while (cursor < _markup.Length && IsAttributeNameCharacter(_markup[cursor]))
        {
            cursor++;
        }

        if (cursor == nameStart)
        {
            return false;
        }

        var name = _markup.Substring(nameStart, cursor - nameStart);
        while (cursor < _markup.Length && char.IsWhiteSpace(_markup[cursor]))
        {
            cursor++;
        }

        if (cursor >= _markup.Length || _markup[cursor] != '=')
        {
            return false;
        }

        cursor++;
        while (cursor < _markup.Length && char.IsWhiteSpace(_markup[cursor]))
        {
            cursor++;
        }

        if (cursor >= _markup.Length || (_markup[cursor] != '"' && _markup[cursor] != '\''))
        {
            return false;
        }

        var quote = _markup[cursor++];
        if (cursor >= _markup.Length || _markup[cursor] != '@' || !TryParseExpression(cursor, out var expression, out var expressionEnd))
        {
            return false;
        }

        cursor = expressionEnd;
        if (cursor >= _markup.Length || _markup[cursor] != quote)
        {
            return false;
        }

        var kind = name.StartsWith("on", StringComparison.OrdinalIgnoreCase) && name.Length > 2
            ? RenderOperationKind.Event
            : RenderOperationKind.Attribute;
        parsed = new ParsedAttribute(new RenderOperation(kind, name, expression), cursor + 1);
        return true;
    }

    private bool TryParseExpression(int at, out string expression, out int end)
    {
        expression = string.Empty;
        end = at;
        if (at + 1 >= _markup.Length || _markup[at] != '@')
        {
            return false;
        }

        var cursor = at + 1;
        if (_markup[cursor] == '(')
        {
            var close = CSharpScanner.FindMatchingDelimiter(_markup, cursor, '(', ')');
            if (close < 0)
            {
                _diagnostics.Add(new ViewDiagnostic(new TextSpan(at, Math.Min(2, _markup.Length - at)), "La expresión Razor no tiene paréntesis de cierre."));
                end = _markup.Length;
                return false;
            }

            expression = _markup.Substring(cursor + 1, close - cursor - 1).Trim();
            if (expression.Length == 0)
            {
                _diagnostics.Add(new ViewDiagnostic(new TextSpan(at, close - at + 1), "La expresión Razor está vacía."));
                end = close + 1;
                return false;
            }

            end = close + 1;
            return true;
        }

        if (!SyntaxFacts.IsIdentifierStartCharacter(_markup[cursor]))
        {
            return false;
        }

        var start = cursor;
        cursor++;
        while (cursor < _markup.Length)
        {
            var character = _markup[cursor];
            if (SyntaxFacts.IsIdentifierPartCharacter(character) || character == '.' || character == '?' || character == '!')
            {
                cursor++;
                continue;
            }

            break;
        }

        expression = _markup.Substring(start, cursor - start).TrimEnd('.', '?');
        end = start + expression.Length;
        return expression.Length > 0;
    }

    private bool StartsWith(int index, string value)
    {
        if (index + value.Length > _markup.Length)
        {
            return false;
        }

        return string.Compare(_markup, index, value, 0, value.Length, StringComparison.Ordinal) == 0;
    }

    private void FlushLiteral()
    {
        if (_literal.Length == 0)
        {
            return;
        }

        var value = _literal.ToString();
        _literal.Clear();
        if (_operations.Count > 0 && _operations[_operations.Count - 1].Kind == RenderOperationKind.Literal)
        {
            var previous = _operations[_operations.Count - 1];
            _operations[_operations.Count - 1] = new RenderOperation(RenderOperationKind.Literal, null, previous.Value + value);
        }
        else
        {
            _operations.Add(new RenderOperation(RenderOperationKind.Literal, null, value));
        }
    }

    private static bool IsAttributeNameCharacter(char character) =>
        char.IsLetterOrDigit(character) || character is '-' or '_' or ':';

    private readonly struct ParsedAttribute
    {
        public ParsedAttribute(RenderOperation operation, int end)
        {
            Operation = operation;
            End = end;
        }

        public RenderOperation Operation { get; }
        public int End { get; }
    }
}

internal static class CSharpScanner
{
    public static int FindMatchingDelimiter(string text, int openIndex, char open, char close)
    {
        var depth = 0;
        for (var index = openIndex; index < text.Length; index++)
        {
            var character = text[index];
            if (character == '/' && index + 1 < text.Length && text[index + 1] == '/')
            {
                index = SkipLineComment(text, index + 2);
                continue;
            }

            if (character == '/' && index + 1 < text.Length && text[index + 1] == '*')
            {
                index = SkipBlockComment(text, index + 2);
                continue;
            }

            if (character == '\'' )
            {
                index = SkipQuoted(text, index, '\'', false);
                continue;
            }

            if (character == '"')
            {
                var quoteCount = CountRun(text, index, '"');
                if (quoteCount >= 3)
                {
                    index = SkipRawString(text, index, quoteCount);
                }
                else
                {
                    var verbatim = (index > 0 && text[index - 1] == '@') ||
                        (index > 1 && text[index - 2] == '@' && text[index - 1] == '$');
                    index = SkipQuoted(text, index, '"', verbatim);
                }

                continue;
            }

            if (character == open)
            {
                depth++;
            }
            else if (character == close)
            {
                depth--;
                if (depth == 0)
                {
                    return index;
                }
            }
        }

        return -1;
    }

    private static int SkipLineComment(string text, int index)
    {
        while (index < text.Length && text[index] != '\n')
        {
            index++;
        }

        return Math.Min(index, text.Length - 1);
    }

    private static int SkipBlockComment(string text, int index)
    {
        while (index + 1 < text.Length)
        {
            if (text[index] == '*' && text[index + 1] == '/')
            {
                return index + 1;
            }

            index++;
        }

        return text.Length - 1;
    }

    private static int SkipQuoted(string text, int index, char quote, bool verbatim)
    {
        for (var cursor = index + 1; cursor < text.Length; cursor++)
        {
            if (verbatim && quote == '"' && text[cursor] == '"' && cursor + 1 < text.Length && text[cursor + 1] == '"')
            {
                cursor++;
                continue;
            }

            if (!verbatim && text[cursor] == '\\')
            {
                cursor++;
                continue;
            }

            if (text[cursor] == quote)
            {
                return cursor;
            }
        }

        return text.Length - 1;
    }

    private static int SkipRawString(string text, int index, int quoteCount)
    {
        for (var cursor = index + quoteCount; cursor < text.Length; cursor++)
        {
            if (text[cursor] == '"' && CountRun(text, cursor, '"') >= quoteCount)
            {
                return cursor + quoteCount - 1;
            }
        }

        return text.Length - 1;
    }

    private static int CountRun(string text, int index, char value)
    {
        var count = 0;
        while (index + count < text.Length && text[index + count] == value)
        {
            count++;
        }

        return count;
    }
}

internal enum RenderOperationKind
{
    Literal,
    Expression,
    Attribute,
    Event,
}

internal sealed class RenderOperation
{
    public RenderOperation(RenderOperationKind kind, string? name, string value)
    {
        Kind = kind;
        Name = name;
        Value = value;
    }

    public RenderOperationKind Kind { get; }
    public string? Name { get; }
    public string Value { get; }
}

internal sealed class ViewDiagnostic
{
    public ViewDiagnostic(TextSpan span, string message)
    {
        Span = span;
        Message = message;
    }

    public TextSpan Span { get; }
    public string Message { get; }
}

internal sealed class ViewCompilationResult
{
    public ViewCompilationResult(string? source, string hintName, ImmutableArray<ViewDiagnostic> diagnostics)
    {
        Source = source;
        HintName = hintName;
        Diagnostics = diagnostics;
    }

    public string? Source { get; }
    public string HintName { get; }
    public ImmutableArray<ViewDiagnostic> Diagnostics { get; }
}
