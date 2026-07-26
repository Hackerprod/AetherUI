using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Primitives;

namespace AetherUI.Layout;

public enum LayoutBoxKind : byte
{
    Element,
    Text,
}

/// <summary>Host-facing projection of a native layout box.</summary>
public sealed class LayoutBox
{
    private readonly List<LayoutBox> _children = [];

    internal LayoutBox(LayoutBoxKind kind, ElementNode element, ComputedStyle style)
    {
        Kind = kind;
        Element = element;
        Style = style;
    }

    public LayoutBoxKind Kind { get; }
    public ElementNode Element { get; }
    public ComputedStyle Style { get; }
    public IReadOnlyList<LayoutBox> Children => _children;
    public string? Text { get; internal set; }
    public RectF MarginBox { get; internal set; }
    public RectF BorderBox { get; internal set; }
    public RectF PaddingBox { get; internal set; }
    public RectF ContentBox { get; internal set; }
    public bool IsAtomicControl { get; internal set; }
    public float ScrollableOverflowY { get; internal set; }
    public float ScrollOffsetY { get; internal set; }

    internal void AddChild(LayoutBox child) => _children.Add(child);

    public IEnumerable<LayoutBox> DescendantsAndSelf()
    {
        yield return this;
        foreach (var child in _children)
        {
            foreach (var descendant in child.DescendantsAndSelf()) yield return descendant;
        }
    }
}

/// <summary>Host-facing projection of a native layout result.</summary>
public sealed class LayoutResult
{
    private readonly IReadOnlyDictionary<ElementNode, LayoutBox> _primaryBoxes;
    private readonly IReadOnlyList<LayoutBox> _paintOrder;

    internal LayoutResult(LayoutBox root, IReadOnlyDictionary<ElementNode, LayoutBox> primaryBoxes,
        IReadOnlyList<LayoutBox> paintOrder, SizeF viewport)
    {
        Root = root;
        _primaryBoxes = primaryBoxes;
        _paintOrder = paintOrder;
        Viewport = viewport;
    }

    public LayoutBox Root { get; }
    public SizeF Viewport { get; }
    public IReadOnlyList<LayoutBox> PaintOrder => _paintOrder;

    public LayoutBox? FindPrimaryBox(ElementNode element) =>
        _primaryBoxes.TryGetValue(element, out var box) ? box : null;

    public ElementNode? HitTest(PointF point)
    {
        for (var index = _paintOrder.Count - 1; index >= 0; index--)
        {
            var box = _paintOrder[index];
            if (box.Style.Visible && box.Style.Display != DisplayKind.None && box.BorderBox.Contains(point))
            {
                return box.Element;
            }
        }

        return null;
    }
}
