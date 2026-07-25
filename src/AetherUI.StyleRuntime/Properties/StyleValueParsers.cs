using System.Globalization;

namespace AetherUI.StyleRuntime;

internal static class StyleValueParsers
{
    private static readonly IReadOnlyDictionary<string, StyleColor> NamedColors =
        new Dictionary<string, StyleColor>(StringComparer.OrdinalIgnoreCase)
        {
            ["transparent"] = StyleColor.Transparent,
            ["black"] = new(0, 0, 0, 255),
            ["silver"] = new(192, 192, 192, 255),
            ["gray"] = new(128, 128, 128, 255),
            ["white"] = new(255, 255, 255, 255),
            ["maroon"] = new(128, 0, 0, 255),
            ["red"] = new(255, 0, 0, 255),
            ["purple"] = new(128, 0, 128, 255),
            ["fuchsia"] = new(255, 0, 255, 255),
            ["green"] = new(0, 128, 0, 255),
            ["lime"] = new(0, 255, 0, 255),
            ["olive"] = new(128, 128, 0, 255),
            ["yellow"] = new(255, 255, 0, 255),
            ["navy"] = new(0, 0, 128, 255),
            ["blue"] = new(0, 0, 255, 255),
            ["teal"] = new(0, 128, 128, 255),
            ["aqua"] = new(0, 255, 255, 255),
            ["orange"] = new(255, 165, 0, 255),
            ["aliceblue"] = new(240, 248, 255, 255),
            ["rebeccapurple"] = new(102, 51, 153, 255),
            ["whitesmoke"] = new(245, 245, 245, 255),
            ["dimgray"] = new(105, 105, 105, 255),
            ["lightgray"] = new(211, 211, 211, 255),
            ["darkgray"] = new(169, 169, 169, 255),
        };

    public static bool TryKeyword<T>(string source, IReadOnlyDictionary<string, T> values, out T value)
    {
        ArgumentNullException.ThrowIfNull(source);
        return values.TryGetValue(source.Trim(), out value!);
    }

    public static bool TryNumber(string source, out float value) =>
        TryFiniteFloat(source.Trim(), out value);

    public static bool TryNonNegativeNumber(string source, out float value)
    {
        if (TryNumber(source, out value) && value >= 0f)
        {
            return true;
        }

        value = default;
        return false;
    }

    public static bool TryInteger(string source, out int value) =>
        int.TryParse(source.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out value);

    public static bool TryNullableInteger(string source, out int? value)
    {
        var trimmed = source.Trim();
        if (trimmed.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            value = null;
            return true;
        }

        if (TryInteger(trimmed, out var parsed))
        {
            value = parsed;
            return true;
        }

        value = default;
        return false;
    }

    public static bool TryOpacity(string source, out float value)
    {
        var trimmed = source.Trim();
        if (trimmed.EndsWith('%') && TryFiniteFloat(trimmed[..^1], out var percent))
        {
            value = Math.Clamp(percent / 100f, 0f, 1f);
            return true;
        }

        if (TryFiniteFloat(trimmed, out var number))
        {
            value = Math.Clamp(number, 0f, 1f);
            return true;
        }

        value = default;
        return false;
    }

    public static bool TryLength(string source, out StyleLength value) =>
        TryLengthCore(source, allowAuto: false, allowNone: false, allowContentKeywords: false, out value);

    public static bool TryLengthOrAuto(string source, out StyleLength value) =>
        TryLengthCore(source, allowAuto: true, allowNone: false, allowContentKeywords: false, out value);

    public static bool TrySize(string source, out StyleLength value)
    {
        if (!TryLengthCore(source, allowAuto: true, allowNone: false, allowContentKeywords: true, out value))
        {
            return false;
        }

        return value.Kind != StyleLengthKind.Linear || !IsDefinitelyNegative(value);
    }

    public static bool TryMaxSize(string source, out StyleLength value)
    {
        if (!TryLengthCore(source, allowAuto: false, allowNone: true, allowContentKeywords: true, out value))
        {
            return false;
        }

        return value.Kind != StyleLengthKind.Linear || !IsDefinitelyNegative(value);
    }

    public static bool TryNonNegativeLength(string source, out StyleLength value)
    {
        if (!TryLength(source, out value))
        {
            return false;
        }

        return !IsDefinitelyNegative(value);
    }

    public static bool TryNonNegativeLengthOrAuto(string source, out StyleLength value)
    {
        if (!TryLengthOrAuto(source, out value))
        {
            return false;
        }

        return value.Kind == StyleLengthKind.Auto || !IsDefinitelyNegative(value);
    }

    public static bool TryFontSize(string source, out StyleLength value)
    {
        var trimmed = source.Trim();
        var keyword = trimmed.ToLowerInvariant() switch
        {
            "xx-small" => 9f,
            "x-small" => 10f,
            "small" => 13f,
            "medium" => 16f,
            "large" => 18f,
            "x-large" => 24f,
            "xx-large" => 32f,
            _ => float.NaN,
        };

        if (float.IsFinite(keyword))
        {
            value = StyleLength.PixelsValue(keyword);
            return true;
        }

        return TryNonNegativeLength(source, out value);
    }

    public static bool TryLineHeight(string source, out StyleLength value)
    {
        var trimmed = source.Trim();
        if (trimmed.Equals("normal", StringComparison.OrdinalIgnoreCase))
        {
            value = StyleLength.EmValue(1.2f);
            return true;
        }

        if (TryFiniteFloat(trimmed, out var multiplier) && multiplier >= 0f)
        {
            value = StyleLength.EmValue(multiplier);
            return true;
        }

        return TryNonNegativeLength(source, out value);
    }

    public static bool TryFontWeight(string source, out ushort value)
    {
        var trimmed = source.Trim();
        if (trimmed.Equals("normal", StringComparison.OrdinalIgnoreCase))
        {
            value = 400;
            return true;
        }

        if (trimmed.Equals("bold", StringComparison.OrdinalIgnoreCase))
        {
            value = 700;
            return true;
        }

        if (ushort.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed) &&
            parsed is >= 1 and <= 1000)
        {
            value = parsed;
            return true;
        }

        value = default;
        return false;
    }

    public static bool TryFontFamily(string source, out string value)
    {
        var first = TextSplitter.SplitTopLevel(source, ',').FirstOrDefault();
        if (string.IsNullOrWhiteSpace(first))
        {
            value = string.Empty;
            return false;
        }

        value = TextSplitter.Unquote(first);
        return value.Length > 0;
    }

    public static bool TryColor(string source, out StyleColor color)
    {
        var trimmed = source.Trim();
        if (trimmed.Equals("currentcolor", StringComparison.OrdinalIgnoreCase))
        {
            color = StyleColor.CurrentColor;
            return true;
        }

        if (NamedColors.TryGetValue(trimmed, out color))
        {
            return true;
        }

        if (trimmed.StartsWith('#') && TryHexColor(trimmed.AsSpan(1), out color))
        {
            return true;
        }

        if (TryFunctionalColor(trimmed, out color))
        {
            return true;
        }

        color = default;
        return false;
    }

    /// <summary>
    /// Parses a 2+ stop <c>linear-gradient()</c>. The direction accepts <c>&lt;N&gt;deg</c> or a
    /// <c>to &lt;side&gt;[ &lt;side&gt;]</c> keyword combination; stops accept an optional trailing
    /// percentage offset and are otherwise spread evenly, matching common real-world usage.
    /// </summary>
    public static bool TryLinearGradient(string source, out StyleLinearGradient gradient)
    {
        gradient = default;
        var trimmed = source.Trim();
        if (!trimmed.StartsWith("linear-gradient(", StringComparison.OrdinalIgnoreCase) || !trimmed.EndsWith(')'))
        {
            return false;
        }

        // "linear-gradient(" is 16 characters; the prior off-by-one here (17) silently ate the
        // first character of the inner content — harmless for a numeric angle like "45deg" (still
        // parses as "5deg", just the wrong angle) but fatal for "to right" (becomes "o right",
        // which matches neither the angle nor the "to <side>" branch below and falls through).
        var inner = trimmed.Substring(16, trimmed.Length - 17);
        var parts = TextSplitter.SplitTopLevel(inner, ',');
        if (parts.Count < 2)
        {
            return false;
        }

        var directionX = 0f;
        var directionY = 1f; // CSS default direction when omitted: "to bottom".
        var stopStart = 0;

        var first = parts[0].Trim();
        if (first.EndsWith("deg", StringComparison.OrdinalIgnoreCase) &&
            float.TryParse(first.AsSpan(0, first.Length - 3), NumberStyles.Float, CultureInfo.InvariantCulture, out var degrees))
        {
            var radians = degrees * MathF.PI / 180f;
            directionX = MathF.Sin(radians);
            directionY = -MathF.Cos(radians);
            stopStart = 1;
        }
        else if (first.StartsWith("to ", StringComparison.OrdinalIgnoreCase))
        {
            var x = 0f;
            var y = 0f;
            foreach (var keyword in first.Substring(3).Split(' ', StringSplitOptions.RemoveEmptyEntries))
            {
                switch (keyword.Trim().ToLowerInvariant())
                {
                    case "top": y = -1f; break;
                    case "bottom": y = 1f; break;
                    case "left": x = -1f; break;
                    case "right": x = 1f; break;
                }
            }

            if (x != 0f || y != 0f)
            {
                var length = MathF.Sqrt(x * x + y * y);
                directionX = x / length;
                directionY = y / length;
                stopStart = 1;
            }
        }

        var stopCount = parts.Count - stopStart;
        if (stopCount < 2)
        {
            return false;
        }

        var stops = new List<StyleGradientStop>(stopCount);
        for (var i = 0; i < stopCount; i++)
        {
            var stopText = parts[stopStart + i].Trim();
            var colorText = stopText;
            float? offset = null;
            var spaceIndex = stopText.LastIndexOf(' ');
            if (spaceIndex > 0 && stopText.EndsWith('%'))
            {
                var percentText = stopText.Substring(spaceIndex + 1, stopText.Length - spaceIndex - 2);
                if (float.TryParse(percentText, NumberStyles.Float, CultureInfo.InvariantCulture, out var percent))
                {
                    colorText = stopText.Substring(0, spaceIndex).Trim();
                    offset = percent / 100f;
                }
            }

            if (!TryColor(colorText, out var color))
            {
                return false;
            }

            stops.Add(new StyleGradientStop(color, offset ?? (stopCount == 1 ? 0f : (float)i / (stopCount - 1))));
        }

        gradient = new StyleLinearGradient(directionX, directionY, stops);
        return true;
    }

    public static bool TryBorderStyle(string source, out BorderLineStyle value)
    {
        value = source.Trim().ToLowerInvariant() switch
        {
            "none" => BorderLineStyle.None,
            "solid" => BorderLineStyle.Solid,
            "dashed" => BorderLineStyle.Dashed,
            "dotted" => BorderLineStyle.Dotted,
            "double" => BorderLineStyle.Double,
            _ => (BorderLineStyle)byte.MaxValue,
        };

        return value != (BorderLineStyle)byte.MaxValue;
    }

    public static bool TryBorderWidth(string source, out StyleLength value)
    {
        var trimmed = source.Trim();
        value = trimmed.ToLowerInvariant() switch
        {
            "thin" => StyleLength.PixelsValue(1f),
            "medium" => StyleLength.PixelsValue(3f),
            "thick" => StyleLength.PixelsValue(5f),
            _ => default,
        };

        if (trimmed.Equals("thin", StringComparison.OrdinalIgnoreCase) ||
            trimmed.Equals("medium", StringComparison.OrdinalIgnoreCase) ||
            trimmed.Equals("thick", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return TryNonNegativeLength(trimmed, out value);
    }

    public static bool TryCornerRadii(string source, out StyleCornerRadii value)
    {
        var slash = TextSplitter.FindTopLevel(source, '/');
        var horizontalText = slash < 0 ? source : source[..slash];
        var verticalText = slash < 0 ? horizontalText : source[(slash + 1)..];
        if (!TryLengthList(horizontalText, TryNonNegativeLength, 1, 4, out var horizontal) ||
            !TryLengthList(verticalText, TryNonNegativeLength, 1, 4, out var vertical))
        {
            value = default;
            return false;
        }

        var h = ExpandFour(horizontal);
        var v = ExpandFour(vertical);
        value = new StyleCornerRadii(
            new StyleCornerRadius(h.Top, v.Top),
            new StyleCornerRadius(h.Right, v.Right),
            new StyleCornerRadius(h.Bottom, v.Bottom),
            new StyleCornerRadius(h.Left, v.Left));
        return true;
    }

    public static bool TryBoxShadows(string source, out IReadOnlyList<StyleShadow> shadows)
    {
        var trimmed = source.Trim();
        if (trimmed.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            shadows = Array.Empty<StyleShadow>();
            return true;
        }

        var result = new List<StyleShadow>();
        foreach (var segment in TextSplitter.SplitTopLevel(trimmed, ','))
        {
            if (!TryShadow(segment, out var shadow))
            {
                shadows = Array.Empty<StyleShadow>();
                return false;
            }

            result.Add(shadow);
        }

        shadows = result.ToArray();
        return result.Count > 0;
    }

    public static bool TryTransform(string source, out IReadOnlyList<StyleTransformOperation> operations)
    {
        var trimmed = source.Trim();
        if (trimmed.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            operations = Array.Empty<StyleTransformOperation>();
            return true;
        }

        var result = new List<StyleTransformOperation>();
        var index = 0;
        while (index < trimmed.Length)
        {
            SkipWhitespace(trimmed, ref index);
            var nameStart = index;
            while (index < trimmed.Length && (char.IsLetter(trimmed[index]) || trimmed[index] == '-'))
            {
                index++;
            }

            if (nameStart == index)
            {
                operations = Array.Empty<StyleTransformOperation>();
                return false;
            }

            var name = trimmed[nameStart..index].ToLowerInvariant();
            SkipWhitespace(trimmed, ref index);
            if (index >= trimmed.Length || trimmed[index] != '(' || !TryReadFunctionBody(trimmed, ref index, out var body))
            {
                operations = Array.Empty<StyleTransformOperation>();
                return false;
            }

            if (!TryTransformFunction(name, body, out var operation))
            {
                operations = Array.Empty<StyleTransformOperation>();
                return false;
            }

            result.Add(operation);
        }

        operations = result.ToArray();
        return result.Count > 0;
    }

    public static bool TryTransformOrigin(string source, out (StyleLength X, StyleLength Y) value)
    {
        var parts = TextSplitter.SplitWhitespaceTopLevel(source);
        if (parts.Count is < 1 or > 2)
        {
            value = default;
            return false;
        }

        if (!TryPositionComponent(parts[0], horizontal: true, out var first))
        {
            value = default;
            return false;
        }

        var second = StyleLength.PercentValue(50f);
        if (parts.Count == 2 && !TryPositionComponent(parts[1], horizontal: false, out second))
        {
            value = default;
            return false;
        }

        value = (first, second);
        return true;
    }

    public static bool TryExpandBox(
        string source,
        StyleValueParser<StyleLength> parser,
        out StyleEdges<StyleLength> values)
    {
        if (!TryLengthList(source, parser, 1, 4, out var parsed))
        {
            values = default;
            return false;
        }

        values = ExpandFour(parsed);
        return true;
    }

    public static bool TryExpandColors(string source, out StyleEdges<StyleColor> values)
    {
        var parts = TextSplitter.SplitWhitespaceTopLevel(source);
        if (parts.Count is < 1 or > 4)
        {
            values = default;
            return false;
        }

        var parsed = new List<StyleColor>(parts.Count);
        foreach (var part in parts)
        {
            if (!TryColor(part, out var color))
            {
                values = default;
                return false;
            }

            parsed.Add(color);
        }

        values = ExpandFour(parsed);
        return true;
    }

    public static bool TryExpandBorderStyles(string source, out StyleEdges<BorderLineStyle> values)
    {
        var parts = TextSplitter.SplitWhitespaceTopLevel(source);
        if (parts.Count is < 1 or > 4)
        {
            values = default;
            return false;
        }

        var parsed = new List<BorderLineStyle>(parts.Count);
        foreach (var part in parts)
        {
            if (!TryBorderStyle(part, out var borderStyle))
            {
                values = default;
                return false;
            }

            parsed.Add(borderStyle);
        }

        values = ExpandFour(parsed);
        return true;
    }

    public static bool TryExpandBorderWidths(string source, out StyleEdges<StyleLength> values)
    {
        var parts = TextSplitter.SplitWhitespaceTopLevel(source);
        if (parts.Count is < 1 or > 4)
        {
            values = default;
            return false;
        }

        var parsed = new List<StyleLength>(parts.Count);
        foreach (var part in parts)
        {
            if (!TryBorderWidth(part, out var width))
            {
                values = default;
                return false;
            }

            parsed.Add(width);
        }

        values = ExpandFour(parsed);
        return true;
    }

    private static bool TryLengthCore(
        string source,
        bool allowAuto,
        bool allowNone,
        bool allowContentKeywords,
        out StyleLength value)
    {
        var trimmed = source.Trim();
        if (allowAuto && trimmed.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            value = StyleLength.Auto;
            return true;
        }

        if (allowNone && trimmed.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            value = StyleLength.None;
            return true;
        }

        if (allowContentKeywords)
        {
            if (trimmed.Equals("min-content", StringComparison.OrdinalIgnoreCase))
            {
                value = StyleLength.MinContent;
                return true;
            }

            if (trimmed.Equals("max-content", StringComparison.OrdinalIgnoreCase))
            {
                value = StyleLength.MaxContent;
                return true;
            }

            if (trimmed.Equals("fit-content", StringComparison.OrdinalIgnoreCase))
            {
                value = StyleLength.FitContent;
                return true;
            }
        }

        var expression = trimmed;
        if (trimmed.StartsWith("calc(", StringComparison.OrdinalIgnoreCase) && trimmed.EndsWith(')'))
        {
            expression = trimmed[5..^1];
        }

        var parser = new LinearLengthParser(expression);
        if (parser.TryParse(out value))
        {
            return true;
        }

        value = default;
        return false;
    }

    private static bool TryFunctionalColor(string source, out StyleColor color)
    {
        var opening = source.IndexOf('(');
        if (opening <= 0 || !source.EndsWith(')'))
        {
            color = default;
            return false;
        }

        var name = source[..opening].Trim().ToLowerInvariant();
        var body = source[(opening + 1)..^1].Trim();
        return name switch
        {
            "rgb" or "rgba" => TryRgb(body, out color),
            "hsl" or "hsla" => TryHsl(body, out color),
            _ => Fail(out color),
        };
    }

    private static bool TryRgb(string body, out StyleColor color)
    {
        var slashParts = TextSplitter.SplitTopLevel(body, '/');
        if (slashParts.Count > 2)
        {
            color = default;
            return false;
        }

        var channels = body.Contains(',')
            ? TextSplitter.SplitTopLevel(slashParts[0], ',')
            : TextSplitter.SplitWhitespaceTopLevel(slashParts[0]);

        string? alphaText = slashParts.Count == 2 ? slashParts[1] : null;
        if (body.Contains(',') && channels.Count == 4)
        {
            alphaText = channels[3];
            channels = channels.Take(3).ToArray();
        }

        if (channels.Count != 3 ||
            !TryRgbChannel(channels[0], out var red) ||
            !TryRgbChannel(channels[1], out var green) ||
            !TryRgbChannel(channels[2], out var blue) ||
            !TryAlpha(alphaText, out var alpha))
        {
            color = default;
            return false;
        }

        color = new StyleColor(red, green, blue, alpha);
        return true;
    }

    private static bool TryHsl(string body, out StyleColor color)
    {
        var slashParts = TextSplitter.SplitTopLevel(body, '/');
        if (slashParts.Count > 2)
        {
            color = default;
            return false;
        }

        var channels = body.Contains(',')
            ? TextSplitter.SplitTopLevel(slashParts[0], ',')
            : TextSplitter.SplitWhitespaceTopLevel(slashParts[0]);

        string? alphaText = slashParts.Count == 2 ? slashParts[1] : null;
        if (body.Contains(',') && channels.Count == 4)
        {
            alphaText = channels[3];
            channels = channels.Take(3).ToArray();
        }

        if (channels.Count != 3 ||
            !TryAngle(channels[0], out var hue) ||
            !TryPercentage01(channels[1], out var saturation) ||
            !TryPercentage01(channels[2], out var lightness) ||
            !TryAlpha(alphaText, out var alpha))
        {
            color = default;
            return false;
        }

        var c = (1f - Math.Abs((2f * lightness) - 1f)) * saturation;
        var h = ((hue % (2f * MathF.PI)) + (2f * MathF.PI)) % (2f * MathF.PI);
        var hPrime = h / (MathF.PI / 3f);
        var x = c * (1f - Math.Abs((hPrime % 2f) - 1f));
        var (r1, g1, b1) = hPrime switch
        {
            < 1f => (c, x, 0f),
            < 2f => (x, c, 0f),
            < 3f => (0f, c, x),
            < 4f => (0f, x, c),
            < 5f => (x, 0f, c),
            _ => (c, 0f, x),
        };
        var m = lightness - (c / 2f);
        color = new StyleColor(ToByte01(r1 + m), ToByte01(g1 + m), ToByte01(b1 + m), alpha);
        return true;
    }

    private static bool TryHexColor(ReadOnlySpan<char> source, out StyleColor color)
    {
        if (source.Length is 3 or 4)
        {
            if (!TryHexNibble(source[0], out var r) || !TryHexNibble(source[1], out var g) ||
                !TryHexNibble(source[2], out var b))
            {
                color = default;
                return false;
            }

            var a = 15;
            if (source.Length == 4 && !TryHexNibble(source[3], out a))
            {
                color = default;
                return false;
            }

            color = new StyleColor((byte)(r * 17), (byte)(g * 17), (byte)(b * 17), (byte)(a * 17));
            return true;
        }

        if (source.Length is 6 or 8)
        {
            if (!byte.TryParse(source[..2], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var r) ||
                !byte.TryParse(source.Slice(2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var g) ||
                !byte.TryParse(source.Slice(4, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var b))
            {
                color = default;
                return false;
            }

            var a = byte.MaxValue;
            if (source.Length == 8 &&
                !byte.TryParse(source.Slice(6, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out a))
            {
                color = default;
                return false;
            }

            color = new StyleColor(r, g, b, a);
            return true;
        }

        color = default;
        return false;
    }

    private static bool TryShadow(string source, out StyleShadow shadow)
    {
        var parts = TextSplitter.SplitWhitespaceTopLevel(source);
        var lengths = new List<StyleLength>(4);
        var inset = false;
        var color = new StyleColor(0, 0, 0, 84);
        foreach (var part in parts)
        {
            if (part.Equals("inset", StringComparison.OrdinalIgnoreCase))
            {
                if (inset)
                {
                    shadow = default;
                    return false;
                }

                inset = true;
            }
            else if (TryColor(part, out var parsedColor))
            {
                color = parsedColor;
            }
            else if (TryLength(part, out var length))
            {
                lengths.Add(length);
            }
            else
            {
                shadow = default;
                return false;
            }
        }

        if (lengths.Count is < 2 or > 4)
        {
            shadow = default;
            return false;
        }

        var blur = lengths.Count >= 3 ? lengths[2] : StyleLength.Zero;
        var spread = lengths.Count == 4 ? lengths[3] : StyleLength.Zero;
        if (IsDefinitelyNegative(blur))
        {
            shadow = default;
            return false;
        }

        shadow = new StyleShadow(inset, lengths[0], lengths[1], blur, spread, color);
        return true;
    }

    private static bool TryTransformFunction(string name, string body, out StyleTransformOperation operation)
    {
        var commaParts = TextSplitter.SplitTopLevel(body, ',');
        var parts = commaParts.Count > 1 ? commaParts : TextSplitter.SplitWhitespaceTopLevel(body);
        switch (name)
        {
            case "translate":
                if (parts.Count is >= 1 and <= 2 && TryLength(parts[0], out var tx))
                {
                    var ty = StyleLength.Zero;
                    if (parts.Count == 2 && !TryLength(parts[1], out ty))
                    {
                        break;
                    }

                    operation = StyleTransformOperation.Translate(tx, ty);
                    return true;
                }

                break;
            case "translatex":
                if (parts.Count == 1 && TryLength(parts[0], out var translateX))
                {
                    operation = StyleTransformOperation.Translate(translateX, StyleLength.Zero);
                    return true;
                }

                break;
            case "translatey":
                if (parts.Count == 1 && TryLength(parts[0], out var translateY))
                {
                    operation = StyleTransformOperation.Translate(StyleLength.Zero, translateY);
                    return true;
                }

                break;
            case "scale":
                if (parts.Count is >= 1 and <= 2 && TryNumber(parts[0], out var sx))
                {
                    var sy = sx;
                    if (parts.Count == 2 && !TryNumber(parts[1], out sy))
                    {
                        break;
                    }

                    operation = StyleTransformOperation.Scale(sx, sy);
                    return true;
                }

                break;
            case "scalex":
                if (parts.Count == 1 && TryNumber(parts[0], out var scaleX))
                {
                    operation = StyleTransformOperation.Scale(scaleX, 1f);
                    return true;
                }

                break;
            case "scaley":
                if (parts.Count == 1 && TryNumber(parts[0], out var scaleY))
                {
                    operation = StyleTransformOperation.Scale(1f, scaleY);
                    return true;
                }

                break;
            case "rotate":
                if (parts.Count == 1 && TryAngle(parts[0], out var rotation))
                {
                    operation = StyleTransformOperation.Rotate(rotation);
                    return true;
                }

                break;
            case "skew":
                if (parts.Count is >= 1 and <= 2 && TryAngle(parts[0], out var skewX))
                {
                    var skewY = 0f;
                    if (parts.Count == 2 && !TryAngle(parts[1], out skewY))
                    {
                        break;
                    }

                    operation = StyleTransformOperation.Skew(skewX, skewY);
                    return true;
                }

                break;
        }

        operation = default;
        return false;
    }

    private static bool TryPositionComponent(string source, bool horizontal, out StyleLength value)
    {
        var trimmed = source.Trim();
        if (trimmed.Equals("center", StringComparison.OrdinalIgnoreCase))
        {
            value = StyleLength.PercentValue(50f);
            return true;
        }

        if ((horizontal && trimmed.Equals("left", StringComparison.OrdinalIgnoreCase)) ||
            (!horizontal && trimmed.Equals("top", StringComparison.OrdinalIgnoreCase)))
        {
            value = StyleLength.PercentValue(0f);
            return true;
        }

        if ((horizontal && trimmed.Equals("right", StringComparison.OrdinalIgnoreCase)) ||
            (!horizontal && trimmed.Equals("bottom", StringComparison.OrdinalIgnoreCase)))
        {
            value = StyleLength.PercentValue(100f);
            return true;
        }

        return TryLength(trimmed, out value);
    }

    private static bool TryLengthList(
        string source,
        StyleValueParser<StyleLength> parser,
        int minimum,
        int maximum,
        out IReadOnlyList<StyleLength> values)
    {
        var parts = TextSplitter.SplitWhitespaceTopLevel(source);
        if (parts.Count < minimum || parts.Count > maximum)
        {
            values = Array.Empty<StyleLength>();
            return false;
        }

        var result = new List<StyleLength>(parts.Count);
        foreach (var part in parts)
        {
            if (!parser(part, out var parsed))
            {
                values = Array.Empty<StyleLength>();
                return false;
            }

            result.Add(parsed);
        }

        values = result;
        return true;
    }

    private static StyleEdges<T> ExpandFour<T>(IReadOnlyList<T> values) => values.Count switch
    {
        1 => new StyleEdges<T>(values[0]),
        2 => new StyleEdges<T>(values[0], values[1], values[0], values[1]),
        3 => new StyleEdges<T>(values[0], values[1], values[2], values[1]),
        4 => new StyleEdges<T>(values[0], values[1], values[2], values[3]),
        _ => throw new ArgumentOutOfRangeException(nameof(values)),
    };

    private static bool TryRgbChannel(string source, out byte value)
    {
        var trimmed = source.Trim();
        if (trimmed.EndsWith('%') && TryFiniteFloat(trimmed[..^1], out var percent))
        {
            value = ToByte01(Math.Clamp(percent / 100f, 0f, 1f));
            return true;
        }

        if (TryFiniteFloat(trimmed, out var number))
        {
            value = (byte)Math.Clamp(MathF.Round(number), 0f, 255f);
            return true;
        }

        value = default;
        return false;
    }

    private static bool TryAlpha(string? source, out byte alpha)
    {
        if (string.IsNullOrWhiteSpace(source))
        {
            alpha = 255;
            return true;
        }

        var trimmed = source.Trim();
        if (trimmed.EndsWith('%') && TryFiniteFloat(trimmed[..^1], out var percent))
        {
            alpha = ToByte01(Math.Clamp(percent / 100f, 0f, 1f));
            return true;
        }

        if (TryFiniteFloat(trimmed, out var value))
        {
            alpha = ToByte01(Math.Clamp(value, 0f, 1f));
            return true;
        }

        alpha = default;
        return false;
    }

    private static bool TryPercentage01(string source, out float value)
    {
        var trimmed = source.Trim();
        if (trimmed.EndsWith('%') && TryFiniteFloat(trimmed[..^1], out var percent))
        {
            value = Math.Clamp(percent / 100f, 0f, 1f);
            return true;
        }

        value = default;
        return false;
    }

    private static bool TryAngle(string source, out float radians)
    {
        var trimmed = source.Trim().ToLowerInvariant();
        var multiplier = 1f;
        ReadOnlySpan<char> numeric = trimmed.AsSpan();
        if (trimmed.EndsWith("deg", StringComparison.Ordinal))
        {
            multiplier = MathF.PI / 180f;
            numeric = numeric[..^3];
        }
        else if (trimmed.EndsWith("grad", StringComparison.Ordinal))
        {
            multiplier = MathF.PI / 200f;
            numeric = numeric[..^4];
        }
        else if (trimmed.EndsWith("turn", StringComparison.Ordinal))
        {
            multiplier = 2f * MathF.PI;
            numeric = numeric[..^4];
        }
        else if (trimmed.EndsWith("rad", StringComparison.Ordinal))
        {
            numeric = numeric[..^3];
        }
        else if (trimmed != "0")
        {
            radians = default;
            return false;
        }

        if (float.TryParse(numeric, NumberStyles.Float, CultureInfo.InvariantCulture, out var value) &&
            float.IsFinite(value))
        {
            radians = value * multiplier;
            return true;
        }

        radians = default;
        return false;
    }

    private static bool TryReadFunctionBody(string source, ref int index, out string body)
    {
        if (source[index] != '(')
        {
            body = string.Empty;
            return false;
        }

        var start = ++index;
        var depth = 1;
        var quote = '\0';
        var escaped = false;
        while (index < source.Length)
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
            }
            else if (current is '\'' or '"')
            {
                quote = current;
            }
            else if (current == '(')
            {
                depth++;
            }
            else if (current == ')' && --depth == 0)
            {
                body = source[start..index];
                index++;
                return true;
            }

            index++;
        }

        body = string.Empty;
        return false;
    }

    private static void SkipWhitespace(string source, ref int index)
    {
        while (index < source.Length && char.IsWhiteSpace(source[index]))
        {
            index++;
        }
    }

    private static bool IsDefinitelyNegative(StyleLength value) =>
        value.Kind == StyleLengthKind.Linear &&
        value.Pixels < 0f && value.Percent <= 0f && value.Em <= 0f && value.Rem <= 0f &&
        value.ViewportWidth <= 0f && value.ViewportHeight <= 0f;

    private static bool TryHexNibble(char value, out int result)
    {
        if (value is >= '0' and <= '9')
        {
            result = value - '0';
            return true;
        }

        if (value is >= 'a' and <= 'f')
        {
            result = value - 'a' + 10;
            return true;
        }

        if (value is >= 'A' and <= 'F')
        {
            result = value - 'A' + 10;
            return true;
        }

        result = default;
        return false;
    }

    private static bool TryFiniteFloat(string source, out float value) =>
        float.TryParse(source, NumberStyles.Float, CultureInfo.InvariantCulture, out value) && float.IsFinite(value);

    private static byte ToByte01(float value) => (byte)Math.Clamp(MathF.Round(value * 255f), 0f, 255f);

    private static bool Fail(out StyleColor color)
    {
        color = default;
        return false;
    }

    private readonly record struct CalcValue(bool IsNumber, float Number, StyleLength Length)
    {
        public static CalcValue Scalar(float value) => new(true, value, default);
        public static CalcValue Dimension(StyleLength value) => new(false, 0f, value);
    }

    private sealed class LinearLengthParser
    {
        private readonly string _source;
        private int _index;

        public LinearLengthParser(string source)
        {
            _source = source;
        }

        public bool TryParse(out StyleLength value)
        {
            if (!TryExpression(out var result))
            {
                value = default;
                return false;
            }

            SkipWhitespace(_source, ref _index);
            if (_index != _source.Length)
            {
                value = default;
                return false;
            }

            if (result.IsNumber)
            {
                if (result.Number == 0f)
                {
                    value = StyleLength.Zero;
                    return true;
                }

                value = default;
                return false;
            }

            value = result.Length;
            return true;
        }

        private bool TryExpression(out CalcValue value)
        {
            if (!TryTerm(out value))
            {
                return false;
            }

            while (true)
            {
                SkipWhitespace(_source, ref _index);
                if (_index >= _source.Length || _source[_index] is not ('+' or '-'))
                {
                    return true;
                }

                var operation = _source[_index++];
                if (!TryTerm(out var right) || !TryAdd(value, right, operation == '-' ? -1f : 1f, out value))
                {
                    return false;
                }
            }
        }

        private bool TryTerm(out CalcValue value)
        {
            if (!TryFactor(out value))
            {
                return false;
            }

            while (true)
            {
                SkipWhitespace(_source, ref _index);
                if (_index >= _source.Length || _source[_index] is not ('*' or '/'))
                {
                    return true;
                }

                var operation = _source[_index++];
                if (!TryFactor(out var right) || !TryMultiply(value, right, operation, out value))
                {
                    return false;
                }
            }
        }

        private bool TryFactor(out CalcValue value)
        {
            SkipWhitespace(_source, ref _index);
            var sign = 1f;
            while (_index < _source.Length && _source[_index] is '+' or '-')
            {
                if (_source[_index++] == '-')
                {
                    sign = -sign;
                }

                SkipWhitespace(_source, ref _index);
            }

            if (_index < _source.Length && _source[_index] == '(')
            {
                _index++;
                if (!TryExpression(out value))
                {
                    return false;
                }

                SkipWhitespace(_source, ref _index);
                if (_index >= _source.Length || _source[_index++] != ')')
                {
                    return false;
                }

                value = Scale(value, sign);
                return true;
            }

            if (!TryReadNumber(out var number))
            {
                value = default;
                return false;
            }

            number *= sign;
            var unitStart = _index;
            if (_index < _source.Length && _source[_index] == '%')
            {
                _index++;
            }
            else
            {
                while (_index < _source.Length && char.IsLetter(_source[_index]))
                {
                    _index++;
                }
            }

            var unit = _source[unitStart.._index].ToLowerInvariant();
            value = unit switch
            {
                "px" => CalcValue.Dimension(StyleLength.PixelsValue(number)),
                "%" => CalcValue.Dimension(StyleLength.PercentValue(number)),
                "em" => CalcValue.Dimension(StyleLength.EmValue(number)),
                "rem" => CalcValue.Dimension(StyleLength.RemValue(number)),
                "vw" => CalcValue.Dimension(StyleLength.ViewportWidthValue(number)),
                "vh" => CalcValue.Dimension(StyleLength.ViewportHeightValue(number)),
                "" => CalcValue.Scalar(number),
                _ => default,
            };

            return unit is "px" or "%" or "em" or "rem" or "vw" or "vh" or "";
        }

        private bool TryReadNumber(out float value)
        {
            var start = _index;
            var hasDigits = false;
            while (_index < _source.Length && char.IsDigit(_source[_index]))
            {
                hasDigits = true;
                _index++;
            }

            if (_index < _source.Length && _source[_index] == '.')
            {
                _index++;
                while (_index < _source.Length && char.IsDigit(_source[_index]))
                {
                    hasDigits = true;
                    _index++;
                }
            }

            if (!hasDigits)
            {
                value = default;
                return false;
            }

            if (_index < _source.Length && _source[_index] is 'e' or 'E')
            {
                var exponentStart = _index++;
                if (_index < _source.Length && _source[_index] is '+' or '-')
                {
                    _index++;
                }

                var exponentDigits = _index;
                while (_index < _source.Length && char.IsDigit(_source[_index]))
                {
                    _index++;
                }

                if (exponentDigits == _index)
                {
                    _index = exponentStart;
                }
            }

            return TryFiniteFloat(_source[start.._index], out value);
        }

        private static bool TryAdd(CalcValue left, CalcValue right, float rightScale, out CalcValue result)
        {
            if (left.IsNumber && right.IsNumber)
            {
                result = CalcValue.Scalar(left.Number + (right.Number * rightScale));
                return true;
            }

            if (!left.IsNumber && !right.IsNumber)
            {
                result = CalcValue.Dimension(left.Length.Add(right.Length.Multiply(rightScale)));
                return true;
            }

            if (left.IsNumber && left.Number == 0f && !right.IsNumber)
            {
                result = CalcValue.Dimension(right.Length.Multiply(rightScale));
                return true;
            }

            if (!left.IsNumber && right.IsNumber && right.Number == 0f)
            {
                result = left;
                return true;
            }

            result = default;
            return false;
        }

        private static bool TryMultiply(CalcValue left, CalcValue right, char operation, out CalcValue result)
        {
            if (operation == '/')
            {
                if (!right.IsNumber || right.Number == 0f)
                {
                    result = default;
                    return false;
                }

                result = Scale(left, 1f / right.Number);
                return true;
            }

            if (left.IsNumber && right.IsNumber)
            {
                result = CalcValue.Scalar(left.Number * right.Number);
                return true;
            }

            if (left.IsNumber && !right.IsNumber)
            {
                result = CalcValue.Dimension(right.Length.Multiply(left.Number));
                return true;
            }

            if (!left.IsNumber && right.IsNumber)
            {
                result = CalcValue.Dimension(left.Length.Multiply(right.Number));
                return true;
            }

            result = default;
            return false;
        }

        private static CalcValue Scale(CalcValue value, float scalar) => value.IsNumber
            ? CalcValue.Scalar(value.Number * scalar)
            : CalcValue.Dimension(value.Length.Multiply(scalar));
    }
}
