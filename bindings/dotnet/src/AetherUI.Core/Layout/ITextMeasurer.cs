using AetherUI.Css;
using AetherUI.Primitives;

namespace AetherUI.Layout;

public readonly record struct TextMetrics(float Width, float Height, float Baseline);

public interface ITextMeasurer
{
    TextMetrics MeasureText(string text, ComputedStyle style, float maxWidth = float.PositiveInfinity);
}

/// <summary>
/// Deterministic approximation used by tests and headless tooling. Native hosts should provide
/// DirectWrite, CoreText, or HarfBuzz-backed measurement.
/// </summary>
public sealed class ApproximateTextMeasurer : ITextMeasurer
{
    public TextMetrics MeasureText(string text, ComputedStyle style, float maxWidth = float.PositiveInfinity)
    {
        ArgumentNullException.ThrowIfNull(text);
        ArgumentNullException.ThrowIfNull(style);
        var width = text.Sum(static character => char.IsWhiteSpace(character) ? .33f : .56f) * style.FontSize;
        width = MathF.Min(width, maxWidth);
        var height = style.FontSize * style.LineHeight;
        return new TextMetrics(width, height, style.FontSize * .8f);
    }
}
