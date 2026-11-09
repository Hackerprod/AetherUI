using AetherUI.Dom;

namespace AetherUI.Css;

public sealed class StyleMap
{
    private readonly IReadOnlyDictionary<ElementNode, ComputedStyle> _styles;

    internal StyleMap(IReadOnlyDictionary<ElementNode, ComputedStyle> styles)
    {
        _styles = styles;
    }

    public ComputedStyle this[ElementNode element] => _styles[element];
    public bool TryGetValue(ElementNode element, out ComputedStyle style) => _styles.TryGetValue(element, out style!);
}
