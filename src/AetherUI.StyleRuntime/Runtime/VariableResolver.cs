using System.Text;

namespace AetherUI.StyleRuntime;

internal sealed class CustomPropertyScope
{
    internal const string InvalidValue = "\u0000aether-invalid-custom-property\u0000";

    private readonly IReadOnlyDictionary<string, string> _local;
    private readonly CustomPropertyScope? _parent;

    public CustomPropertyScope(CustomPropertyScope? parent, IReadOnlyDictionary<string, string> local)
    {
        _parent = parent;
        _local = local;
    }

    public bool TryGet(string name, out string value)
    {
        if (_local.TryGetValue(name, out value!))
        {
            return true;
        }

        if (_parent is not null)
        {
            return _parent.TryGet(name, out value);
        }

        value = string.Empty;
        return false;
    }
}

internal sealed class VariableResolver
{
    private readonly StyleRuntimeOptions _options;

    public VariableResolver(StyleRuntimeOptions options)
    {
        _options = options;
    }

    public bool TryResolve(string source, CustomPropertyScope scope, out string value)
    {
        var stack = new HashSet<string>(StringComparer.Ordinal);
        return TryResolveCore(source, scope, stack, 0, out value);
    }

    private bool TryResolveCore(
        string source,
        CustomPropertyScope scope,
        HashSet<string> stack,
        int depth,
        out string value)
    {
        if (depth > _options.MaximumVariableDepth)
        {
            value = string.Empty;
            return false;
        }

        var output = new StringBuilder(Math.Min(source.Length + 16, _options.MaximumVariableExpansionLength));
        var index = 0;
        while (index < source.Length)
        {
            var variableStart = source.IndexOf("var(", index, StringComparison.OrdinalIgnoreCase);
            if (variableStart < 0)
            {
                output.Append(source, index, source.Length - index);
                break;
            }

            output.Append(source, index, variableStart - index);
            var bodyStart = variableStart + 4;
            if (!TryFindClosingParenthesis(source, bodyStart, out var closing))
            {
                value = string.Empty;
                return false;
            }

            var body = source[bodyStart..closing];
            var comma = TextSplitter.FindTopLevel(body, ',');
            var name = (comma < 0 ? body : body[..comma]).Trim();
            var fallback = comma < 0 ? null : body[(comma + 1)..].Trim();
            if (!name.StartsWith("--", StringComparison.Ordinal) || name.Length < 3)
            {
                value = string.Empty;
                return false;
            }

            string? replacement = null;
            if (scope.TryGet(name, out var raw) &&
                !string.Equals(raw, CustomPropertyScope.InvalidValue, StringComparison.Ordinal) &&
                stack.Add(name))
            {
                if (TryResolveCore(raw, scope, stack, depth + 1, out var resolved))
                {
                    replacement = resolved;
                }

                stack.Remove(name);
            }

            if (replacement is null && fallback is not null &&
                TryResolveCore(fallback, scope, stack, depth + 1, out var resolvedFallback))
            {
                replacement = resolvedFallback;
            }

            if (replacement is null)
            {
                value = string.Empty;
                return false;
            }

            output.Append(replacement);
            if (output.Length > _options.MaximumVariableExpansionLength)
            {
                value = string.Empty;
                return false;
            }

            index = closing + 1;
        }

        if (output.Length > _options.MaximumVariableExpansionLength)
        {
            value = string.Empty;
            return false;
        }

        value = output.ToString();
        return true;
    }

    private static bool TryFindClosingParenthesis(string source, int start, out int closing)
    {
        var depth = 1;
        var quote = '\0';
        var escaped = false;
        for (var index = start; index < source.Length; index++)
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
            }
            else if (current == '(')
            {
                depth++;
            }
            else if (current == ')' && --depth == 0)
            {
                closing = index;
                return true;
            }
        }

        closing = -1;
        return false;
    }
}
