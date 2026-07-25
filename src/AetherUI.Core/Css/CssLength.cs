using System.Globalization;

namespace AetherUI.Css;

public enum CssUnit : byte
{
    Auto,
    Pixels,
    Percent,
    Em,
    Rem,
    ViewportWidth,
    ViewportHeight,
}

public readonly record struct CssLength(float Value, CssUnit Unit)
{
    public static readonly CssLength Auto = new(0, CssUnit.Auto);
    public static readonly CssLength Zero = new(0, CssUnit.Pixels);

    public bool IsAuto => Unit == CssUnit.Auto;

    public float Resolve(float reference, float fontSize, float rootFontSize, float viewportWidth, float viewportHeight) => Unit switch
    {
        CssUnit.Auto => float.NaN,
        CssUnit.Pixels => Value,
        CssUnit.Percent => reference * Value / 100f,
        CssUnit.Em => fontSize * Value,
        CssUnit.Rem => rootFontSize * Value,
        CssUnit.ViewportWidth => viewportWidth * Value / 100f,
        CssUnit.ViewportHeight => viewportHeight * Value / 100f,
        _ => Value,
    };

    public static bool TryParse(string? input, out CssLength length)
    {
        length = Auto;
        if (string.IsNullOrWhiteSpace(input))
        {
            return false;
        }

        var value = input.Trim().ToLowerInvariant();
        if (value == "auto")
        {
            length = Auto;
            return true;
        }

        if (value == "0")
        {
            length = Zero;
            return true;
        }

        var unit = CssUnit.Pixels;
        var numeric = value.AsSpan();
        if (value.EndsWith("px", StringComparison.Ordinal))
        {
            numeric = numeric[..^2];
        }
        else if (value.EndsWith("rem", StringComparison.Ordinal))
        {
            numeric = numeric[..^3];
            unit = CssUnit.Rem;
        }
        else if (value.EndsWith("em", StringComparison.Ordinal))
        {
            numeric = numeric[..^2];
            unit = CssUnit.Em;
        }
        else if (value.EndsWith("vw", StringComparison.Ordinal))
        {
            numeric = numeric[..^2];
            unit = CssUnit.ViewportWidth;
        }
        else if (value.EndsWith("vh", StringComparison.Ordinal))
        {
            numeric = numeric[..^2];
            unit = CssUnit.ViewportHeight;
        }
        else if (value.EndsWith('%'))
        {
            numeric = numeric[..^1];
            unit = CssUnit.Percent;
        }

        if (!float.TryParse(numeric, NumberStyles.Float, CultureInfo.InvariantCulture, out var number) || !float.IsFinite(number))
        {
            return false;
        }

        length = new CssLength(number, unit);
        return true;
    }
}

public readonly record struct CssEdges(CssLength Left, CssLength Top, CssLength Right, CssLength Bottom)
{
    public CssEdges(CssLength uniform) : this(uniform, uniform, uniform, uniform)
    {
    }

    public static readonly CssEdges Zero = new(CssLength.Zero);
}
