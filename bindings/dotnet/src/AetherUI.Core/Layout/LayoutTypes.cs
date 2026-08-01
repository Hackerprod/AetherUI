using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Primitives;

namespace AetherUI.Layout;

public enum LayoutBoxKind : byte
{
    Element,
    Text,
}

/// <summary>Independent scroll ranges and offsets for a layout box.</summary>
public readonly record struct ScrollMetrics(
    float ScrollableOverflowX,
    float ScrollableOverflowY,
    float ScrollOffsetX,
    float ScrollOffsetY);

/// <summary>Host-owned offsets supplied to native layout for one scroll container.</summary>
public readonly record struct ScrollOffset(float X, float Y);

/// <summary>Resolved native scrollbar chrome geometry for one layout box.</summary>
public readonly record struct ScrollbarGeometry(
    bool HorizontalVisible,
    bool VerticalVisible,
    RectF HorizontalTrack,
    RectF HorizontalThumb,
    RectF VerticalTrack,
    RectF VerticalThumb,
    RectF Corner);

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
    public ComputedStyle Style { get; internal set; }
    public IReadOnlyList<LayoutBox> Children => _children;
    public string? Text { get; internal set; }
    public RectF MarginBox { get; internal set; }
    public RectF BorderBox { get; internal set; }
    public RectF PaddingBox { get; internal set; }
    public RectF ContentBox { get; internal set; }
    public bool IsAtomicControl { get; internal set; }
    public ScrollMetrics Scroll { get; internal set; }
    public ScrollbarGeometry Scrollbar { get; internal set; }
    public float ScrollableOverflowX => Scroll.ScrollableOverflowX;
    public float ScrollableOverflowY => Scroll.ScrollableOverflowY;
    public float ScrollOffsetX => Scroll.ScrollOffsetX;
    public float ScrollOffsetY => Scroll.ScrollOffsetY;
    internal IntPtr NativeHandle { get; init; }
    internal long StyleNodeId { get; init; }

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
    private readonly Dictionary<ElementNode, IReadOnlyList<LayoutBox>> _paintBoxesByElement;

    internal LayoutResult(LayoutBox root, IReadOnlyDictionary<ElementNode, LayoutBox> primaryBoxes,
        IReadOnlyList<LayoutBox> paintOrder, SizeF viewport)
    {
        Root = root;
        _primaryBoxes = primaryBoxes;
        _paintOrder = paintOrder;
        var paintBoxesByElement = new Dictionary<ElementNode, List<LayoutBox>>();
        foreach (var box in paintOrder)
        {
            if (!paintBoxesByElement.TryGetValue(box.Element, out var boxes))
            {
                boxes = [];
                paintBoxesByElement[box.Element] = boxes;
            }

            boxes.Add(box);
        }

        var indexed = new Dictionary<ElementNode, IReadOnlyList<LayoutBox>>(paintBoxesByElement.Count);
        foreach (var (element, boxes) in paintBoxesByElement)
            indexed[element] = boxes;
        _paintBoxesByElement = indexed;
        Viewport = viewport;
    }

    public LayoutBox Root { get; }
    public SizeF Viewport { get; }
    public IReadOnlyList<LayoutBox> PaintOrder => _paintOrder;

    public LayoutBox? FindPrimaryBox(ElementNode element) =>
        _primaryBoxes.TryGetValue(element, out var box) ? box : null;

    public IReadOnlyList<LayoutBox> FindPaintBoxes(ElementNode element) =>
        _paintBoxesByElement.TryGetValue(element, out var boxes) ? boxes : Array.Empty<LayoutBox>();

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

    public ElementNode? HitTest(PointF point, Func<ElementNode, ScrollOffset> offsetFor)
    {
        for (var index = _paintOrder.Count - 1; index >= 0; index--)
        {
            var box = _paintOrder[index];
            var local = point;
            for (var ancestor = box.Element.Parent as ElementNode; ancestor is not null; ancestor = ancestor.Parent as ElementNode)
            {
                var offset = offsetFor(ancestor);
                local = new PointF(local.X + offset.X, local.Y + offset.Y);
            }
            if (box.Style.Visible && box.Style.Display != DisplayKind.None && box.BorderBox.Contains(local)) return box.Element;
        }
        return null;
    }
}
