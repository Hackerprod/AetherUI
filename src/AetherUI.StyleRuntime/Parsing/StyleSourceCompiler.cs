namespace AetherUI.StyleRuntime;

internal static class StyleSourceCompiler
{
    public static CompiledStyleSheet Compile(
        string source,
        string sourceName,
        StyleSheetHandle handle,
        StyleSheetLayer layer,
        StylePropertyRegistry properties,
        StyleRuntimeOptions options,
        Func<int> allocateRuleId,
        Func<int> allocateSourceOrder)
    {
        var cleaned = TextSplitter.RemoveComments(source);
        var diagnostics = new DiagnosticBag(cleaned, sourceName);
        var rules = new List<CompiledRule>();
        CompileRange(
            cleaned,
            0,
            cleaned.Length,
            MediaCondition.Always,
            handle,
            layer,
            properties,
            diagnostics,
            options,
            allocateRuleId,
            allocateSourceOrder,
            rules);
        return new CompiledStyleSheet(handle, layer, sourceName, rules, diagnostics.Items.ToArray());
    }

    public static InlineDeclarationSet CompileInline(
        string source,
        StylePropertyRegistry properties,
        StyleRuntimeOptions options,
        string sourceName)
    {
        var cleaned = TextSplitter.RemoveComments(source);
        var diagnostics = new DiagnosticBag(cleaned, sourceName);
        var declarations = CompileDeclarations(cleaned, 0, properties, diagnostics, options);
        return new InlineDeclarationSet(
            declarations.Properties,
            declarations.Shorthands,
            declarations.Custom,
            diagnostics.Items.ToArray());
    }

    private static void CompileRange(
        string source,
        int start,
        int end,
        MediaCondition inheritedCondition,
        StyleSheetHandle handle,
        StyleSheetLayer layer,
        StylePropertyRegistry properties,
        DiagnosticBag diagnostics,
        StyleRuntimeOptions options,
        Func<int> allocateRuleId,
        Func<int> allocateSourceOrder,
        List<CompiledRule> rules)
    {
        var index = start;
        while (index < end)
        {
            SkipWhitespace(source, ref index, end);
            if (index >= end)
            {
                break;
            }

            if (source[index] == '@')
            {
                CompileAtRule(
                    source,
                    ref index,
                    end,
                    inheritedCondition,
                    handle,
                    layer,
                    properties,
                    diagnostics,
                    options,
                    allocateRuleId,
                    allocateSourceOrder,
                    rules);
                continue;
            }

            var opening = FindNextTopLevel(source, index, end, '{');
            if (opening < 0)
            {
                diagnostics.Warning("AUICSS001", "Trailing style source does not form a rule.", index);
                break;
            }

            var closing = FindMatchingBrace(source, opening, end);
            if (closing < 0)
            {
                diagnostics.Error("AUICSS002", "Unterminated style rule.", opening);
                break;
            }

            var selectorText = source[index..opening].Trim();
            var selectorOffset = index + Math.Max(0, source[index..opening].IndexOf(selectorText, StringComparison.Ordinal));
            var declarationText = source[(opening + 1)..closing];
            var selectors = SelectorParser.ParseList(selectorText, diagnostics, selectorOffset);
            var declarations = CompileDeclarations(declarationText, opening + 1, properties, diagnostics, options);
            if (selectors.Count > 0 &&
                (declarations.Properties.Count > 0 || declarations.Shorthands.Count > 0 || declarations.Custom.Count > 0))
            {
                var sourceOrder = allocateSourceOrder();
                foreach (var selector in selectors)
                {
                    rules.Add(new CompiledRule(
                        allocateRuleId(),
                        handle,
                        layer,
                        selector,
                        inheritedCondition,
                        declarations.Properties,
                        declarations.Shorthands,
                        declarations.Custom,
                        sourceOrder));
                }
            }

            index = closing + 1;
        }
    }

    private static void CompileAtRule(
        string source,
        ref int index,
        int end,
        MediaCondition inheritedCondition,
        StyleSheetHandle handle,
        StyleSheetLayer layer,
        StylePropertyRegistry properties,
        DiagnosticBag diagnostics,
        StyleRuntimeOptions options,
        Func<int> allocateRuleId,
        Func<int> allocateSourceOrder,
        List<CompiledRule> rules)
    {
        var atOffset = index++;
        var nameStart = index;
        while (index < end && (char.IsLetterOrDigit(source[index]) || source[index] is '-' or '_'))
        {
            index++;
        }

        var name = source[nameStart..index].ToLowerInvariant();
        SkipWhitespace(source, ref index, end);
        var delimiter = FindAtRuleDelimiter(source, index, end);
        if (delimiter < 0)
        {
            diagnostics.Warning("AUICSS003", $"Unterminated at-rule '@{name}'.", atOffset);
            index = end;
            return;
        }

        if (source[delimiter] == ';')
        {
            diagnostics.Info("AUICSS004", $"At-rule '@{name}' is ignored by the Aether style profile.", atOffset);
            index = delimiter + 1;
            return;
        }

        var closing = FindMatchingBrace(source, delimiter, end);
        if (closing < 0)
        {
            diagnostics.Error("AUICSS005", $"Unterminated '@{name}' block.", delimiter);
            index = end;
            return;
        }

        var prelude = source[index..delimiter].Trim();
        if (name == "media")
        {
            var media = MediaConditionParser.Parse(prelude, diagnostics, index);
            CompileRange(
                source,
                delimiter + 1,
                closing,
                MediaCondition.And(inheritedCondition, media),
                handle,
                layer,
                properties,
                diagnostics,
                options,
                allocateRuleId,
                allocateSourceOrder,
                rules);
        }
        else
        {
            diagnostics.Info("AUICSS006", $"At-rule '@{name}' is ignored by the Aether style profile.", atOffset);
        }

        index = closing + 1;
    }

    private static DeclarationCompilation CompileDeclarations(
        string source,
        int baseOffset,
        StylePropertyRegistry properties,
        DiagnosticBag diagnostics,
        StyleRuntimeOptions options)
    {
        var propertyDeclarations = new List<CompiledPropertyDeclaration>();
        var shorthands = new List<DeferredShorthandDeclaration>();
        var custom = new List<CustomPropertyDeclaration>();
        var declarationOrder = 0;
        var runningOffset = 0;
        foreach (var segment in TextSplitter.SplitTopLevel(source, ';'))
        {
            var segmentOffset = source.IndexOf(segment, runningOffset, StringComparison.Ordinal);
            if (segmentOffset < 0)
            {
                segmentOffset = runningOffset;
            }

            runningOffset = Math.Max(segmentOffset + segment.Length, runningOffset);
            if (string.IsNullOrWhiteSpace(segment))
            {
                continue;
            }

            var colon = TextSplitter.FindTopLevel(segment, ':');
            if (colon <= 0)
            {
                diagnostics.Warning("AUICSS007", "Declaration requires a property name and value.", baseOffset + segmentOffset);
                continue;
            }

            var name = segment[..colon].Trim().ToLowerInvariant();
            var value = segment[(colon + 1)..].Trim();
            if (name.Length == 0 || value.Length == 0)
            {
                diagnostics.Warning("AUICSS008", "Declaration contains an empty name or value.", baseOffset + segmentOffset);
                continue;
            }

            var important = TryStripImportant(ref value);
            if (name.StartsWith("--", StringComparison.Ordinal))
            {
                custom.Add(new CustomPropertyDeclaration(name, value, important, declarationOrder++));
                continue;
            }

            if (properties.TryGetShorthand(name, out var shorthand))
            {
                if (ContainsVariable(value))
                {
                    shorthands.Add(new DeferredShorthandDeclaration(name, value, important, declarationOrder++));
                    continue;
                }

                var writer = new StyleShorthandWriter();
                if (!shorthand(value, writer))
                {
                    diagnostics.Warning("AUICSS009", $"Invalid value '{value}' for shorthand '{name}'.", baseOffset + segmentOffset);
                    continue;
                }

                foreach (var expanded in writer.Items)
                {
                    CompileLonghand(
                        expanded.Name,
                        expanded.Value,
                        important,
                        declarationOrder++,
                        properties,
                        propertyDeclarations,
                        diagnostics,
                        baseOffset + segmentOffset);
                }

                continue;
            }

            if (!properties.TryGet(name, out _))
            {
                diagnostics.Warning("AUICSS010", $"Property '{name}' is not registered in the Aether style runtime.", baseOffset + segmentOffset);
                continue;
            }

            CompileLonghand(
                name,
                value,
                important,
                declarationOrder++,
                properties,
                propertyDeclarations,
                diagnostics,
                baseOffset + segmentOffset);
        }

        return new DeclarationCompilation(
            propertyDeclarations.ToArray(),
            shorthands.ToArray(),
            custom.ToArray());
    }

    private static void CompileLonghand(
        string name,
        string value,
        bool important,
        int declarationOrder,
        StylePropertyRegistry properties,
        List<CompiledPropertyDeclaration> output,
        DiagnosticBag diagnostics,
        int offset)
    {
        if (!properties.TryGet(name, out var definition))
        {
            diagnostics.Warning("AUICSS011", $"Expanded property '{name}' is not registered.", offset);
            return;
        }

        var normalized = value.Trim();
        CompiledStyleValue compiled;
        if (normalized.Equals("initial", StringComparison.OrdinalIgnoreCase) ||
            normalized.Equals("revert", StringComparison.OrdinalIgnoreCase))
        {
            compiled = CompiledStyleValue.Initial;
        }
        else if (normalized.Equals("inherit", StringComparison.OrdinalIgnoreCase))
        {
            compiled = CompiledStyleValue.Inherit;
        }
        else if (normalized.Equals("unset", StringComparison.OrdinalIgnoreCase))
        {
            compiled = CompiledStyleValue.Unset;
        }
        else if (ContainsVariable(normalized))
        {
            compiled = CompiledStyleValue.Deferred(normalized);
        }
        else
        {
            var parsed = definition.Parse(normalized);
            if (!parsed.Success)
            {
                diagnostics.Warning("AUICSS012", $"Invalid value '{value}' for property '{name}'.", offset);
                return;
            }

            compiled = CompiledStyleValue.Typed(parsed.Value);
        }

        output.Add(new CompiledPropertyDeclaration(definition.Id, compiled, important, declarationOrder));
    }

    private static bool TryStripImportant(ref string value)
    {
        var trimmed = value.TrimEnd();
        const string marker = "!important";
        if (!trimmed.EndsWith(marker, StringComparison.OrdinalIgnoreCase))
        {
            value = trimmed;
            return false;
        }

        var markerStart = trimmed.Length - marker.Length;
        if (markerStart > 0 && trimmed[markerStart - 1] == '\\')
        {
            value = trimmed;
            return false;
        }

        value = trimmed[..markerStart].TrimEnd();
        return true;
    }

    private static bool ContainsVariable(string source) =>
        source.Contains("var(", StringComparison.OrdinalIgnoreCase);

    private static int FindAtRuleDelimiter(string source, int start, int end)
    {
        var parentheses = 0;
        var brackets = 0;
        var quote = '\0';
        var escaped = false;
        for (var index = start; index < end; index++)
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
            else if (parentheses == 0 && brackets == 0 && (current is ';' or '{'))
            {
                return index;
            }
        }

        return -1;
    }

    private static int FindNextTopLevel(string source, int start, int end, char target)
    {
        var parentheses = 0;
        var brackets = 0;
        var quote = '\0';
        var escaped = false;
        for (var index = start; index < end; index++)
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
            else if (parentheses == 0 && brackets == 0 && current == target)
            {
                return index;
            }
        }

        return -1;
    }

    private static int FindMatchingBrace(string source, int opening, int end)
    {
        var depth = 1;
        var quote = '\0';
        var escaped = false;
        for (var index = opening + 1; index < end; index++)
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
            else if (current == '{')
            {
                depth++;
            }
            else if (current == '}' && --depth == 0)
            {
                return index;
            }
        }

        return -1;
    }

    private static void SkipWhitespace(string source, ref int index, int end)
    {
        while (index < end && char.IsWhiteSpace(source[index]))
        {
            index++;
        }
    }

    private sealed record DeclarationCompilation(
        IReadOnlyList<CompiledPropertyDeclaration> Properties,
        IReadOnlyList<DeferredShorthandDeclaration> Shorthands,
        IReadOnlyList<CustomPropertyDeclaration> Custom);
}
