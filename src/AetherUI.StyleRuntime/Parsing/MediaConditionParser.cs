namespace AetherUI.StyleRuntime;

internal static class MediaConditionParser
{
    public static MediaCondition Parse(string source, DiagnosticBag diagnostics, int offset)
    {
        var alternatives = TextSplitter.SplitTopLevel(source, ',');
        var result = MediaCondition.Never;
        foreach (var alternative in alternatives)
        {
            var parsed = ParseAlternative(alternative, diagnostics, offset);
            result = MediaCondition.Or(result, parsed);
        }

        return result;
    }

    private static MediaCondition ParseAlternative(string source, DiagnosticBag diagnostics, int offset)
    {
        var trimmed = source.Trim();
        var negate = false;
        if (trimmed.StartsWith("not ", StringComparison.OrdinalIgnoreCase))
        {
            negate = true;
            trimmed = trimmed[4..].TrimStart();
        }

        if (trimmed.StartsWith("only ", StringComparison.OrdinalIgnoreCase))
        {
            trimmed = trimmed[5..].TrimStart();
        }

        var parts = SplitAnd(trimmed);
        var result = MediaCondition.Always;
        foreach (var part in parts)
        {
            var term = ParseTerm(part.Trim(), diagnostics, offset);
            result = MediaCondition.And(result, term);
        }

        return negate ? MediaCondition.Not(result) : result;
    }

    private static MediaCondition ParseTerm(string source, DiagnosticBag diagnostics, int offset)
    {
        if (source.Equals("all", StringComparison.OrdinalIgnoreCase) ||
            source.Equals("screen", StringComparison.OrdinalIgnoreCase))
        {
            return MediaCondition.Always;
        }

        if (source.Equals("print", StringComparison.OrdinalIgnoreCase))
        {
            return MediaCondition.Never;
        }

        if (!source.StartsWith('(') || !source.EndsWith(')'))
        {
            diagnostics.Warning("AUIMED001", $"Unsupported media term '{source}'.", offset);
            return MediaCondition.Never;
        }

        var body = source[1..^1].Trim();
        var colon = TextSplitter.FindTopLevel(body, ':');
        if (colon < 0)
        {
            diagnostics.Warning("AUIMED002", $"Media feature '{body}' requires a value.", offset);
            return MediaCondition.Never;
        }

        var name = body[..colon].Trim().ToLowerInvariant();
        var value = body[(colon + 1)..].Trim();
        if (name is "min-width" or "max-width" or "width" or "min-height" or "max-height" or "height")
        {
            if (!StyleValueParsers.TryNonNegativeLength(value, out var length))
            {
                diagnostics.Warning("AUIMED003", $"Invalid media length '{value}'.", offset);
                return MediaCondition.Never;
            }

            return name switch
            {
                "min-width" => MediaCondition.MinimumWidth(length),
                "max-width" => MediaCondition.MaximumWidth(length),
                "width" => MediaCondition.WidthEquals(length),
                "min-height" => MediaCondition.MinimumHeight(length),
                "max-height" => MediaCondition.MaximumHeight(length),
                _ => MediaCondition.HeightEquals(length),
            };
        }

        if (name == "prefers-color-scheme")
        {
            return value.ToLowerInvariant() switch
            {
                "dark" => MediaCondition.ColorScheme(StyleColorScheme.Dark),
                "light" => MediaCondition.ColorScheme(StyleColorScheme.Light),
                _ => InvalidFeature(name, value, diagnostics, offset),
            };
        }

        if (name == "pointer")
        {
            return value.ToLowerInvariant() switch
            {
                "none" => MediaCondition.Pointer(StylePointerAccuracy.None),
                "coarse" => MediaCondition.Pointer(StylePointerAccuracy.Coarse),
                "fine" => MediaCondition.Pointer(StylePointerAccuracy.Fine),
                _ => InvalidFeature(name, value, diagnostics, offset),
            };
        }

        if (name == "prefers-reduced-motion")
        {
            return value.ToLowerInvariant() switch
            {
                "reduce" => MediaCondition.Motion(StyleMotionPreference.Reduce),
                "no-preference" => MediaCondition.Motion(StyleMotionPreference.NoPreference),
                _ => InvalidFeature(name, value, diagnostics, offset),
            };
        }

        diagnostics.Warning("AUIMED004", $"Unsupported media feature '{name}'.", offset);
        return MediaCondition.Never;
    }

    private static IReadOnlyList<string> SplitAnd(string source)
    {
        var result = new List<string>();
        var start = 0;
        var depth = 0;
        var quote = '\0';
        for (var index = 0; index < source.Length; index++)
        {
            var current = source[index];
            if (quote != '\0')
            {
                if (current == quote && (index == 0 || source[index - 1] != '\\'))
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

            if (current == '(')
            {
                depth++;
                continue;
            }

            if (current == ')')
            {
                depth = Math.Max(0, depth - 1);
                continue;
            }

            if (depth == 0 && index + 3 <= source.Length &&
                source.AsSpan(index, 3).Equals("and".AsSpan(), StringComparison.OrdinalIgnoreCase) &&
                (index == 0 || char.IsWhiteSpace(source[index - 1])) &&
                (index + 3 == source.Length || char.IsWhiteSpace(source[index + 3])))
            {
                result.Add(source[start..index].Trim());
                index += 2;
                start = index + 1;
            }
        }

        result.Add(source[start..].Trim());
        return result.Where(static item => item.Length > 0).ToArray();
    }

    private static MediaCondition InvalidFeature(
        string name,
        string value,
        DiagnosticBag diagnostics,
        int offset)
    {
        diagnostics.Warning("AUIMED005", $"Invalid value '{value}' for media feature '{name}'.", offset);
        return MediaCondition.Never;
    }
}
