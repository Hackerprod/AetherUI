using System.Globalization;

namespace AetherUI.StyleRuntime;

/// <summary>
/// A reduced linear length expression. It can preserve mixed units such as
/// calc(100% - 24px) until layout supplies the actual reference values.
/// </summary>
public readonly record struct StyleLength(
    StyleLengthKind Kind,
    float Pixels,
    float Percent,
    float Em,
    float Rem,
    float ViewportWidth,
    float ViewportHeight)
{
    public static StyleLength Zero => PixelsValue(0f);
    public static StyleLength Auto => new(StyleLengthKind.Auto, 0f, 0f, 0f, 0f, 0f, 0f);
    public static StyleLength None => new(StyleLengthKind.None, 0f, 0f, 0f, 0f, 0f, 0f);
    public static StyleLength MinContent => new(StyleLengthKind.MinContent, 0f, 0f, 0f, 0f, 0f, 0f);
    public static StyleLength MaxContent => new(StyleLengthKind.MaxContent, 0f, 0f, 0f, 0f, 0f, 0f);
    public static StyleLength FitContent => new(StyleLengthKind.FitContent, 0f, 0f, 0f, 0f, 0f, 0f);

    public static StyleLength PixelsValue(float value) =>
        new(StyleLengthKind.Linear, value, 0f, 0f, 0f, 0f, 0f);

    public static StyleLength PercentValue(float value) =>
        new(StyleLengthKind.Linear, 0f, value, 0f, 0f, 0f, 0f);

    public static StyleLength EmValue(float value) =>
        new(StyleLengthKind.Linear, 0f, 0f, value, 0f, 0f, 0f);

    public static StyleLength RemValue(float value) =>
        new(StyleLengthKind.Linear, 0f, 0f, 0f, value, 0f, 0f);

    public static StyleLength ViewportWidthValue(float value) =>
        new(StyleLengthKind.Linear, 0f, 0f, 0f, 0f, value, 0f);

    public static StyleLength ViewportHeightValue(float value) =>
        new(StyleLengthKind.Linear, 0f, 0f, 0f, 0f, 0f, value);

    public bool IsLinear => Kind == StyleLengthKind.Linear;
    public bool IsZero => IsLinear && Pixels == 0f && Percent == 0f && Em == 0f && Rem == 0f &&
        ViewportWidth == 0f && ViewportHeight == 0f;

    public float Resolve(in StyleLengthResolveContext context)
    {
        if (!IsLinear)
        {
            return float.NaN;
        }

        return Pixels +
            (Percent * context.PercentageReference / 100f) +
            (Em * context.FontSize) +
            (Rem * context.RootFontSize) +
            (ViewportWidth * context.ViewportWidth / 100f) +
            (ViewportHeight * context.ViewportHeight / 100f);
    }

    public StyleLength Add(StyleLength other)
    {
        if (!IsLinear || !other.IsLinear)
        {
            throw new InvalidOperationException("Only linear lengths can be combined.");
        }

        return new StyleLength(
            StyleLengthKind.Linear,
            Pixels + other.Pixels,
            Percent + other.Percent,
            Em + other.Em,
            Rem + other.Rem,
            ViewportWidth + other.ViewportWidth,
            ViewportHeight + other.ViewportHeight);
    }

    public StyleLength Multiply(float scalar)
    {
        if (!IsLinear || !float.IsFinite(scalar))
        {
            throw new InvalidOperationException("Only finite linear lengths can be scaled.");
        }

        return new StyleLength(
            StyleLengthKind.Linear,
            Pixels * scalar,
            Percent * scalar,
            Em * scalar,
            Rem * scalar,
            ViewportWidth * scalar,
            ViewportHeight * scalar);
    }

    public override string ToString()
    {
        if (Kind != StyleLengthKind.Linear)
        {
            return Kind.ToString();
        }

        if (Percent == 0f && Em == 0f && Rem == 0f && ViewportWidth == 0f && ViewportHeight == 0f)
        {
            return Pixels.ToString(CultureInfo.InvariantCulture) + "px";
        }

        return FormattableString.Invariant(
            $"linear(px:{Pixels},%:{Percent},em:{Em},rem:{Rem},vw:{ViewportWidth},vh:{ViewportHeight})");
    }
}

public readonly record struct StyleLengthResolveContext(
    float PercentageReference,
    float FontSize,
    float RootFontSize,
    float ViewportWidth,
    float ViewportHeight);

public readonly record struct StyleEdges<T>(T Top, T Right, T Bottom, T Left)
{
    public StyleEdges(T uniform) : this(uniform, uniform, uniform, uniform)
    {
    }
}

public readonly record struct StyleCornerRadius(StyleLength Horizontal, StyleLength Vertical)
{
    public StyleCornerRadius(StyleLength uniform) : this(uniform, uniform)
    {
    }
}

public readonly record struct StyleCornerRadii(
    StyleCornerRadius TopLeft,
    StyleCornerRadius TopRight,
    StyleCornerRadius BottomRight,
    StyleCornerRadius BottomLeft)
{
    public StyleCornerRadii(StyleCornerRadius uniform) : this(uniform, uniform, uniform, uniform)
    {
    }

    public static StyleCornerRadii Zero => new(new StyleCornerRadius(StyleLength.Zero));
}

public readonly record struct StyleColor(byte Red, byte Green, byte Blue, byte Alpha, bool IsCurrentColor = false)
{
    public static StyleColor Transparent => new(0, 0, 0, 0);
    public static StyleColor Black => new(0, 0, 0, 255);
    public static StyleColor White => new(255, 255, 255, 255);
    public static StyleColor CurrentColor => new(0, 0, 0, 255, true);

    public float RedF => Red / 255f;
    public float GreenF => Green / 255f;
    public float BlueF => Blue / 255f;
    public float AlphaF => Alpha / 255f;

    public StyleColor ResolveCurrentColor(StyleColor currentColor) => IsCurrentColor ? currentColor : this;
}

public readonly record struct StyleBorderLine(StyleLength Width, BorderLineStyle Style, StyleColor Color)
{
    public static StyleBorderLine None => new(StyleLength.Zero, BorderLineStyle.None, StyleColor.CurrentColor);
}

public readonly record struct StyleGradientStop(StyleColor Color, float Offset);

/// <summary>
/// A CSS <c>linear-gradient()</c>. The axis is stored as a unit direction vector (not a raw angle)
/// so consumers can map it directly onto a fill rectangle without repeating the angle math.
/// </summary>
public readonly record struct StyleLinearGradient(float DirectionX, float DirectionY, IReadOnlyList<StyleGradientStop> Stops);

public readonly record struct StyleShadow(
    bool Inset,
    StyleLength OffsetX,
    StyleLength OffsetY,
    StyleLength BlurRadius,
    StyleLength SpreadRadius,
    StyleColor Color);

public readonly record struct StyleTransformOperation(
    StyleTransformKind Kind,
    StyleLength X,
    StyleLength Y,
    float First,
    float Second)
{
    public static StyleTransformOperation Translate(StyleLength x, StyleLength y) =>
        new(StyleTransformKind.Translate, x, y, 0f, 0f);

    public static StyleTransformOperation Scale(float x, float y) =>
        new(StyleTransformKind.Scale, StyleLength.Zero, StyleLength.Zero, x, y);

    public static StyleTransformOperation Rotate(float radians) =>
        new(StyleTransformKind.Rotate, StyleLength.Zero, StyleLength.Zero, radians, 0f);

    public static StyleTransformOperation Skew(float xRadians, float yRadians) =>
        new(StyleTransformKind.Skew, StyleLength.Zero, StyleLength.Zero, xRadians, yRadians);
}
