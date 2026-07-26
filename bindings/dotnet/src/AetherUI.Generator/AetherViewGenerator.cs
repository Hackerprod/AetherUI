using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
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

        var viewFiles = context.AdditionalTextsProvider
            .Where(static file => file.Path.EndsWith(".cshtml", StringComparison.OrdinalIgnoreCase)
                || file.Path.EndsWith(".view.html", StringComparison.OrdinalIgnoreCase)
                || file.Path.EndsWith(".view.css", StringComparison.OrdinalIgnoreCase))
            .Select(static (file, cancellationToken) =>
            {
                var text = file.GetText(cancellationToken);
                return new ViewInput(file.Path, text);
            })
            .Collect();

        context.RegisterSourceOutput(viewFiles.Combine(rootNamespace).Combine(context.CompilationProvider), static (productionContext, pair) =>
        {
            var inputs = pair.Left.Left;
            var rootNamespace = pair.Left.Right;
            var compilation = pair.Right;
            var styleSheets = new Dictionary<string, SourceText?>(StringComparer.OrdinalIgnoreCase);
            foreach (var input in inputs)
            {
                if (input.Path.EndsWith(".view.css", StringComparison.OrdinalIgnoreCase))
                {
                    styleSheets[input.Path] = input.Text;
                }
            }

            foreach (var input in inputs)
            {
                var isLegacy = input.Path.EndsWith(".cshtml", StringComparison.OrdinalIgnoreCase);
                var isView = input.Path.EndsWith(".view.html", StringComparison.OrdinalIgnoreCase);
                if (!isLegacy && !isView)
                {
                    continue;
                }

                if (input.Text is null)
                {
                    productionContext.ReportDiagnostic(Diagnostic.Create(ReadFailure, Location.None, input.Path));
                    continue;
                }

                SourceText? styleSheet = null;
                if (isView)
                {
                    var stylePath = input.Path.Substring(0, input.Path.Length - ".html".Length) + ".css";
                    styleSheets.TryGetValue(stylePath, out styleSheet);
                }

                var viewNamespace = isView
                    ? FindCompanionNamespace(compilation, input.Path) ?? rootNamespace
                    : rootNamespace;
                var compiler = new ViewCompiler(input.Path, input.Text, viewNamespace,
                    isView ? ViewSyntax.Modern : ViewSyntax.Legacy, styleSheet);
                var result = compiler.Compile();
                foreach (var diagnostic in result.Diagnostics)
                {
                    var lineSpan = input.Text.Lines.GetLinePositionSpan(diagnostic.Span);
                    var location = Location.Create(input.Path, diagnostic.Span, lineSpan);
                    productionContext.ReportDiagnostic(Diagnostic.Create(ParseFailure, location, diagnostic.Message));
                }

                if (result.Source is not null)
                {
                    productionContext.AddSource(result.HintName, SourceText.From(result.Source, Encoding.UTF8));
                }
            }
        });
    }

    private static string? FindCompanionNamespace(Compilation compilation, string viewPath)
    {
        var className = Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(viewPath));
        var companionPath = viewPath.Substring(0, viewPath.Length - ".view.html".Length) + ".cs";
        string? fallbackNamespace = null;
        foreach (var syntaxTree in compilation.SyntaxTrees)
        {
            var semanticModel = compilation.GetSemanticModel(syntaxTree);
            var declaration = syntaxTree.GetRoot().DescendantNodes().OfType<ClassDeclarationSyntax>()
                .FirstOrDefault(candidate => string.Equals(candidate.Identifier.ValueText, className, StringComparison.Ordinal));
            if (declaration is not null && semanticModel.GetDeclaredSymbol(declaration) is INamedTypeSymbol symbol)
            {
                var candidateNamespace = symbol.ContainingNamespace.IsGlobalNamespace ? string.Empty : symbol.ContainingNamespace.ToDisplayString();
                if (string.Equals(syntaxTree.FilePath, companionPath, StringComparison.OrdinalIgnoreCase))
                {
                    return candidateNamespace;
                }

                if (fallbackNamespace is not null && !string.Equals(fallbackNamespace, candidateNamespace, StringComparison.Ordinal))
                {
                    return null;
                }

                fallbackNamespace = candidateNamespace;
            }
        }

        return fallbackNamespace;
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
    private readonly ViewSyntax _syntax;
    private readonly string? _styleSheet;
    private readonly List<ViewDiagnostic> _diagnostics = new();
    private readonly List<string> _usings = new();
    private readonly List<CodeBlock> _codeBlocks = new();
    private string? _namespace;
    private string? _className;
    private string _baseType = "global::AetherUI.Runtime.UiComponent";

    public ViewCompiler(string path, SourceText sourceText, string rootNamespace)
        : this(path, sourceText, rootNamespace, ViewSyntax.Legacy, null)
    {
    }

    public ViewCompiler(string path, SourceText sourceText, string rootNamespace, ViewSyntax syntax, SourceText? styleSheet)
    {
        _path = path;
        _sourceText = sourceText;
        _source = sourceText.ToString();
        _rootNamespace = rootNamespace;
        _syntax = syntax;
        _styleSheet = styleSheet?.ToString();
    }

    public ViewCompilationResult Compile()
    {
        var markupBuffer = _source.ToCharArray();
        if (_syntax == ViewSyntax.Legacy)
        {
            ParseLineDirectives(markupBuffer);
            ParseCodeBlocks(markupBuffer);
        }

        if (_diagnostics.Count > 0)
        {
            return new ViewCompilationResult(null, BuildHintName(), _diagnostics.ToImmutableArray());
        }

        _namespace ??= _rootNamespace;
        _className ??= _syntax == ViewSyntax.Modern
            ? SanitizeIdentifier(Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(_path)), "GeneratedView")
            : SanitizeIdentifier(Path.GetFileNameWithoutExtension(_path), "GeneratedView");
        if (!IsValidQualifiedName(_namespace))
        {
            AddDiagnostic(0, Math.Min(_source.Length, 1), $"El namespace '{_namespace}' no es válido.");
        }

        if (!SyntaxFacts.IsValidIdentifier(_className))
        {
            AddDiagnostic(0, Math.Min(_source.Length, 1), $"El nombre de clase '{_className}' no es válido.");
        }

        var markup = new string(markupBuffer);
        IViewMarkupCompiler markupCompiler = _syntax == ViewSyntax.Modern
            ? new ModernMarkupCompiler(markup)
            : new MarkupCompiler(markup, ViewSyntax.Legacy);
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
        if (_syntax == ViewSyntax.Modern && !string.IsNullOrEmpty(_styleSheet))
        {
            builder.Append("            writer.WriteLiteral(").Append(Quote("<style>" + _styleSheet + "</style>")).AppendLine(");");
        }
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
                case RenderOperationKind.IfStart:
                    builder.Append("            if (").Append(operation.Value).AppendLine(")");
                    builder.AppendLine("            {");
                    break;
                case RenderOperationKind.ForStart:
                    builder.Append("            foreach (var ").Append(operation.Name).Append(" in ").Append(operation.Value).AppendLine(")");
                    builder.AppendLine("            {");
                    break;
                case RenderOperationKind.BlockEnd:
                    builder.AppendLine("            }");
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
        var baseName = _syntax == ViewSyntax.Modern
            ? Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(_path))
            : Path.GetFileNameWithoutExtension(_path);
        var file = SanitizeIdentifier(baseName, "View");
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

internal interface IViewMarkupCompiler
{
    IReadOnlyList<ViewDiagnostic> Diagnostics { get; }
    IReadOnlyList<RenderOperation> Compile();
}

internal sealed class MarkupCompiler : IViewMarkupCompiler
{
    private readonly string _markup;
    private readonly ViewSyntax _syntax;
    private readonly List<RenderOperation> _operations = new();
    private readonly List<ViewDiagnostic> _diagnostics = new();
    private readonly StringBuilder _literal = new();

    public MarkupCompiler(string markup, ViewSyntax syntax = ViewSyntax.Legacy)
    {
        _markup = markup;
        _syntax = syntax;
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
            if (_syntax == ViewSyntax.Modern && character == '{' && index + 1 < _markup.Length && _markup[index + 1] == '{'
                && TryParseMustache(index, out var modernExpression, out var modernEnd))
            {
                FlushLiteral();
                _operations.Add(new RenderOperation(RenderOperationKind.Expression, null, modernExpression));
                index = modernEnd;
                continue;
            }

            if (_syntax == ViewSyntax.Legacy && character == '@')
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
        var valueStart = cursor;
        while (cursor < _markup.Length && _markup[cursor] != quote)
        {
            cursor++;
        }
        if (cursor >= _markup.Length)
        {
            return false;
        }

        var rawValue = _markup.Substring(valueStart, cursor - valueStart).Trim();
        var isEvent = name.StartsWith("on", StringComparison.OrdinalIgnoreCase) && name.Length > 2;
        if (_syntax == ViewSyntax.Modern)
        {
            if (isEvent)
            {
                if (rawValue.Length == 0)
                {
                    return false;
                }

                var handler = rawValue.IndexOf('(') >= 0 ? "_ => " + rawValue : rawValue;
                parsed = new ParsedAttribute(new RenderOperation(RenderOperationKind.Event, name, handler), cursor + 1);
                return true;
            }

            if (!TryParseMustacheValue(rawValue, out var modernExpression))
            {
                return false;
            }

            parsed = new ParsedAttribute(new RenderOperation(RenderOperationKind.Attribute, name, modernExpression), cursor + 1);
            return true;
        }

        if (valueStart >= _markup.Length || _markup[valueStart] != '@' || !TryParseExpression(valueStart, out var expression, out var expressionEnd)
            || expressionEnd != cursor)
        {
            return false;
        }

        var kind = isEvent ? RenderOperationKind.Event : RenderOperationKind.Attribute;
        parsed = new ParsedAttribute(new RenderOperation(kind, name, expression), cursor + 1);
        return true;
    }

    private bool TryParseMustache(int start, out string expression, out int end)
    {
        expression = string.Empty;
        end = start;
        if (start + 3 >= _markup.Length || _markup[start] != '{' || _markup[start + 1] != '{')
        {
            return false;
        }

        var close = CSharpScanner.FindMatchingDelimiter(_markup, start, '{', '}');
        if (close < 0 || close <= start + 2 || _markup[close - 1] != '}')
        {
            _diagnostics.Add(new ViewDiagnostic(new TextSpan(start, Math.Min(2, _markup.Length - start)), "La interpolación {{ }} no tiene cierre."));
            end = _markup.Length;
            return false;
        }

        expression = _markup.Substring(start + 2, close - start - 3).Trim();
        if (expression.Length == 0)
        {
            _diagnostics.Add(new ViewDiagnostic(new TextSpan(start, close - start + 1), "La interpolación {{ }} está vacía."));
            end = close + 1;
            return false;
        }

        end = close + 1;
        return true;
    }

    internal static bool TryParseMustacheValue(string value, out string expression)
    {
        expression = string.Empty;
        var trimmed = value.Trim();
        if (trimmed.Length < 5 || !trimmed.StartsWith("{{", StringComparison.Ordinal) || !trimmed.EndsWith("}}", StringComparison.Ordinal))
        {
            return false;
        }

        expression = trimmed.Substring(2, trimmed.Length - 4).Trim();
        return expression.Length > 0;
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

internal sealed class ModernMarkupCompiler : IViewMarkupCompiler
{
    private readonly string _markup;
    private readonly List<RenderOperation> _operations = new();
    private readonly List<ViewDiagnostic> _diagnostics = new();
    private readonly Stack<RenderOperationKind> _blocks = new();

    public ModernMarkupCompiler(string markup)
    {
        _markup = markup;
    }

    public IReadOnlyList<ViewDiagnostic> Diagnostics => _diagnostics;

    public IReadOnlyList<RenderOperation> Compile()
    {
        var segmentStart = 0;
        var cursor = 0;
        while (cursor < _markup.Length)
        {
            var nextOpen = _markup.IndexOf("<template", cursor, StringComparison.OrdinalIgnoreCase);
            var nextClose = _markup.IndexOf("</template", cursor, StringComparison.OrdinalIgnoreCase);
            var tokenStart = NextToken(nextOpen, nextClose);
            if (tokenStart < 0)
            {
                AppendFragment(segmentStart, _markup.Length);
                break;
            }

            var tagEnd = FindTagEnd(tokenStart);
            if (tagEnd < 0)
            {
                AppendFragment(segmentStart, _markup.Length);
                break;
            }

            if (tokenStart == nextOpen && TryParseControlStart(tokenStart, tagEnd, out var control))
            {
                AppendFragment(segmentStart, tokenStart);
                _operations.Add(control);
                _blocks.Push(control.Kind);
                segmentStart = tagEnd + 1;
            }
            else if (tokenStart == nextClose && _blocks.Count > 0)
            {
                AppendFragment(segmentStart, tokenStart);
                _operations.Add(new RenderOperation(RenderOperationKind.BlockEnd, null, string.Empty));
                _blocks.Pop();
                segmentStart = tagEnd + 1;
            }

            cursor = tagEnd + 1;
        }

        if (_blocks.Count > 0)
        {
            _diagnostics.Add(new ViewDiagnostic(new TextSpan(_markup.Length, 0), "Un <template a:if> o <template a:for> no tiene cierre."));
        }

        return _operations;
    }

    private void AppendFragment(int start, int end)
    {
        if (end <= start)
        {
            return;
        }

        var compiler = new MarkupCompiler(_markup.Substring(start, end - start), ViewSyntax.Modern);
        foreach (var operation in compiler.Compile())
        {
            _operations.Add(operation);
        }
        foreach (var diagnostic in compiler.Diagnostics)
        {
            _diagnostics.Add(new ViewDiagnostic(new TextSpan(start + diagnostic.Span.Start, diagnostic.Span.Length), diagnostic.Message));
        }
    }

    private bool TryParseControlStart(int start, int end, out RenderOperation control)
    {
        control = default!;
        var tag = _markup.Substring(start, end - start + 1);
        var hasIf = TryGetAttribute(tag, "a:if", out var ifValue);
        var hasFor = TryGetAttribute(tag, "a:for", out var forValue);
        if (!hasIf && !hasFor)
        {
            return false;
        }

        if (hasIf && hasFor)
        {
            _diagnostics.Add(new ViewDiagnostic(new TextSpan(start, end - start + 1), "Un template solo puede declarar a:if o a:for."));
            return false;
        }

        if (hasIf)
        {
            if (!MarkupCompiler.TryParseMustacheValue(ifValue, out var expression))
            {
                _diagnostics.Add(new ViewDiagnostic(new TextSpan(start, end - start + 1), "a:if requiere una condición {{ expresión }}."));
                return false;
            }

            control = new RenderOperation(RenderOperationKind.IfStart, null, expression);
            return true;
        }

        var separator = forValue.IndexOf(" in ", StringComparison.Ordinal);
        if (separator <= 0 || separator + 4 >= forValue.Length)
        {
            _diagnostics.Add(new ViewDiagnostic(new TextSpan(start, end - start + 1), "a:for requiere la forma 'item in Items'."));
            return false;
        }

        var variable = forValue.Substring(0, separator).Trim();
        var enumerable = forValue.Substring(separator + 4).Trim();
        if (!SyntaxFacts.IsValidIdentifier(variable) || enumerable.Length == 0)
        {
            _diagnostics.Add(new ViewDiagnostic(new TextSpan(start, end - start + 1), "a:for requiere un identificador y una colección válidos."));
            return false;
        }

        control = new RenderOperation(RenderOperationKind.ForStart, variable, enumerable);
        return true;
    }

    private int FindTagEnd(int start)
    {
        var quote = '\0';
        for (var index = start; index < _markup.Length; index++)
        {
            var character = _markup[index];
            if (quote == '\0' && (character == '\"' || character == '\''))
            {
                quote = character;
            }
            else if (quote != '\0' && character == quote)
            {
                quote = '\0';
            }
            else if (quote == '\0' && character == '>')
            {
                return index;
            }
        }

        return -1;
    }

    private static bool TryGetAttribute(string tag, string name, out string value)
    {
        value = string.Empty;
        var index = tag.IndexOf(name, StringComparison.OrdinalIgnoreCase);
        if (index < 0)
        {
            return false;
        }

        var cursor = index + name.Length;
        while (cursor < tag.Length && char.IsWhiteSpace(tag[cursor])) cursor++;
        if (cursor >= tag.Length || tag[cursor++] != '=') return false;
        while (cursor < tag.Length && char.IsWhiteSpace(tag[cursor])) cursor++;
        if (cursor >= tag.Length || (tag[cursor] != '\"' && tag[cursor] != '\'')) return false;
        var quote = tag[cursor++];
        var end = tag.IndexOf(quote, cursor);
        if (end < 0) return false;
        value = tag.Substring(cursor, end - cursor);
        return true;
    }

    private static int NextToken(int first, int second)
    {
        if (first < 0) return second;
        if (second < 0) return first;
        return Math.Min(first, second);
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
    IfStart,
    ForStart,
    BlockEnd,
}

internal enum ViewSyntax
{
    Legacy,
    Modern,
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
