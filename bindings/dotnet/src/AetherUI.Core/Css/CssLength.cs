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

    /// <summary>
    /// A general linear combination of the units above (the result of a <c>calc()</c> expression
    /// with more than one term). <see cref="CssLength.Value"/> holds the pixel term; the extra
    /// fields hold the remaining terms. All terms apply simultaneously in <see cref="Resolve"/>.
    /// </summary>
    Linear,
}

public readonly record struct CssLength(
    float Value,
    CssUnit Unit,
    float LinearPercent = 0f,
    float LinearEm = 0f,
    float LinearRem = 0f,
    float LinearViewportWidth = 0f,
    float LinearViewportHeight = 0f)
{
    public static readonly CssLength Auto = new(0, CssUnit.Auto);
    public static readonly CssLength Zero = new(0, CssUnit.Pixels);

    public bool IsAuto => Unit == CssUnit.Auto;

    public static CssLength Linear(float pixels, float percent, float em, float rem, float viewportWidth, float viewportHeight) =>
        new(pixels, CssUnit.Linear, percent, em, rem, viewportWidth, viewportHeight);

    public float Resolve(float reference, float fontSize, float rootFontSize, float viewportWidth, float viewportHeight) => Unit switch
    {
        CssUnit.Auto => float.NaN,
        CssUnit.Pixels => Value,
        CssUnit.Percent => reference * Value / 100f,
        CssUnit.Em => fontSize * Value,
        CssUnit.Rem => rootFontSize * Value,
        CssUnit.ViewportWidth => viewportWidth * Value / 100f,
        CssUnit.ViewportHeight => viewportHeight * Value / 100f,
        CssUnit.Linear => Value
            + (reference * LinearPercent / 100f)
            + (fontSize * LinearEm)
            + (rootFontSize * LinearRem)
            + (viewportWidth * LinearViewportWidth / 100f)
            + (viewportHeight * LinearViewportHeight / 100f),
        _ => Value,
    };
}

public readonly record struct CssEdges(CssLength Left, CssLength Top, CssLength Right, CssLength Bottom)
{
    public CssEdges(CssLength uniform) : this(uniform, uniform, uniform, uniform)
    {
    }

    public static readonly CssEdges Zero = new(CssLength.Zero);
}

public readonly record struct CssCornerRadius(CssLength Horizontal, CssLength Vertical)
{
    public CssCornerRadius(CssLength uniform) : this(uniform, uniform) { }
    public static readonly CssCornerRadius Zero = new(CssLength.Zero);
}

public readonly record struct CssBorderRadii(
    CssCornerRadius TopLeft, CssCornerRadius TopRight,
    CssCornerRadius BottomRight, CssCornerRadius BottomLeft)
{
    public static readonly CssBorderRadii Zero = new(CssCornerRadius.Zero, CssCornerRadius.Zero,
        CssCornerRadius.Zero, CssCornerRadius.Zero);
}
