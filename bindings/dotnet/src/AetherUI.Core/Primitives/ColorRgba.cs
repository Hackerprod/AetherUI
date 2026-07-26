using System.Globalization;

namespace AetherUI.Primitives;

public readonly record struct ColorRgba(float R, float G, float B, float A = 1f)
{
    public static readonly ColorRgba Transparent = new(0, 0, 0, 0);
    public static readonly ColorRgba Black = new(0, 0, 0, 1);
    public static readonly ColorRgba White = new(1, 1, 1, 1);

    public ColorRgba WithAlpha(float alpha) => this with { A = Math.Clamp(alpha, 0, 1) };

    public static bool TryParse(string? input, out ColorRgba color)
    {
        color = Transparent;
        if (string.IsNullOrWhiteSpace(input))
        {
            return false;
        }

        var value = input.Trim().ToLowerInvariant();
        if (Named.TryGetValue(value, out color))
        {
            return true;
        }

        if (value[0] == '#')
        {
            return TryParseHex(value.AsSpan(1), out color);
        }

        if (value.StartsWith("rgb(", StringComparison.Ordinal) && value.EndsWith(')'))
        {
            return TryParseRgb(value.AsSpan(4, value.Length - 5), false, out color);
        }

        if (value.StartsWith("rgba(", StringComparison.Ordinal) && value.EndsWith(')'))
        {
            return TryParseRgb(value.AsSpan(5, value.Length - 6), true, out color);
        }

        return false;
    }

    private static bool TryParseHex(ReadOnlySpan<char> value, out ColorRgba color)
    {
        color = Transparent;
        Span<byte> parts = stackalloc byte[4];
        parts[3] = 255;

        if (value.Length is 3 or 4)
        {
            for (var i = 0; i < value.Length; i++)
            {
                if (!byte.TryParse(new string(value[i], 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out parts[i]))
                {
                    return false;
                }
            }
        }
        else if (value.Length is 6 or 8)
        {
            for (var i = 0; i < value.Length / 2; i++)
            {
                if (!byte.TryParse(value.Slice(i * 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out parts[i]))
                {
                    return false;
                }
            }
        }
        else
        {
            return false;
        }

        color = FromBytes(parts[0], parts[1], parts[2], parts[3]);
        return true;
    }

    private static bool TryParseRgb(ReadOnlySpan<char> value, bool hasAlpha, out ColorRgba color)
    {
        color = Transparent;
        var pieces = value.ToString().Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (pieces.Length != (hasAlpha ? 4 : 3))
        {
            return false;
        }

        if (!TryChannel(pieces[0], out var red) ||
            !TryChannel(pieces[1], out var green) ||
            !TryChannel(pieces[2], out var blue))
        {
            return false;
        }

        var alpha = 1f;
        if (hasAlpha && !TryAlpha(pieces[3], out alpha))
        {
            return false;
        }

        color = new(red, green, blue, alpha);
        return true;
    }

    private static bool TryChannel(string text, out float value)
    {
        value = 0;
        if (text.EndsWith('%') && float.TryParse(text.AsSpan(0, text.Length - 1), NumberStyles.Float, CultureInfo.InvariantCulture, out var percent))
        {
            value = Math.Clamp(percent / 100f, 0, 1);
            return true;
        }

        if (!float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var channel))
        {
            return false;
        }

        value = Math.Clamp(channel / 255f, 0, 1);
        return true;
    }

    private static bool TryAlpha(string text, out float value)
    {
        value = 1;
        if (text.EndsWith('%') && float.TryParse(text.AsSpan(0, text.Length - 1), NumberStyles.Float, CultureInfo.InvariantCulture, out var percent))
        {
            value = Math.Clamp(percent / 100f, 0, 1);
            return true;
        }

        if (!float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var alpha))
        {
            return false;
        }

        value = Math.Clamp(alpha, 0, 1);
        return true;
    }

    private static ColorRgba FromBytes(byte red, byte green, byte blue, byte alpha = 255) =>
        new(red / 255f, green / 255f, blue / 255f, alpha / 255f);

    private static readonly IReadOnlyDictionary<string, ColorRgba> Named =
        new Dictionary<string, ColorRgba>(StringComparer.OrdinalIgnoreCase)
        {
            ["transparent"] = Transparent,
            ["black"] = Black,
            ["white"] = White,
            ["red"] = FromBytes(255, 0, 0),
            ["green"] = FromBytes(0, 128, 0),
            ["blue"] = FromBytes(0, 0, 255),
            ["gray"] = FromBytes(128, 128, 128),
            ["grey"] = FromBytes(128, 128, 128),
            ["silver"] = FromBytes(192, 192, 192),
            ["navy"] = FromBytes(0, 0, 128),
            ["teal"] = FromBytes(0, 128, 128),
            ["purple"] = FromBytes(128, 0, 128),
            ["orange"] = FromBytes(255, 165, 0),
            ["yellow"] = FromBytes(255, 255, 0),
            ["whitesmoke"] = FromBytes(245, 245, 245),
            ["aliceblue"] = FromBytes(240, 248, 255),
        };
}
