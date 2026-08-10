using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Primitives;

namespace AetherUI.Layout;

/// <summary>Projects a native layout result into the existing managed host-facing layout contract.</summary>
internal sealed class LayoutInterop : IDisposable
{
    private readonly ITextMeasurer _textMeasurer;
    private readonly NativeLayoutInterop _native;

    public LayoutInterop(ITextMeasurer textMeasurer)
    {
        _textMeasurer = textMeasurer ?? throw new ArgumentNullException(nameof(textMeasurer));
        _native = new NativeLayoutInterop(Measure);
    }

    public NativeLayoutResult CreateNativeLayout(DomInterop dom, NativeStyleMap nativeStyles, SizeF viewport) =>
        _native.Layout(dom.Native, nativeStyles, viewport.Width, viewport.Height,
            _ => default);

    public LayoutResult Project(NativeLayoutResult nativeResult, DomInterop dom, StyleMap styles, SizeF viewport)
    {
        var boxes = new Dictionary<IntPtr, LayoutBox>();
        var primary = new Dictionary<ElementNode, LayoutBox>();
        var root = Project(nativeResult.Root, nativeResult, dom, styles, boxes, primary)
            ?? throw new InvalidOperationException("Native layout returned an internal root box.");
        var paintOrder = new List<LayoutBox>(nativeResult.PaintOrderCount);
        for (var index = 0; index < nativeResult.PaintOrderCount; index++)
        {
            var nativeBox = nativeResult.GetPaintOrderBox(index);
            if (boxes.TryGetValue(nativeBox.Handle, out var box)) paintOrder.Add(box);
        }

        return new LayoutResult(root, primary, paintOrder, viewport,
            (OverflowKind)nativeResult.ViewportOverflowX,
            (OverflowKind)nativeResult.ViewportOverflowY);
    }

    public ElementNode? HitTest(NativeLayoutResult nativeResult, NativeStyleMap? currentStyles,
        DomInterop dom, PointF point, Func<ElementNode, ScrollOffset> scrollOffsetFor)
    {
        var handle = _native.HitTest(nativeResult, currentStyles, point.X, point.Y,
            nativeElement => dom.GetManagedElement(nativeElement) is { } element
                ? new NativeScrollOffset(scrollOffsetFor(element).X, scrollOffsetFor(element).Y)
                : default);
        return handle == IntPtr.Zero ? null : dom.GetManagedElement(handle);
    }

    public ScrollbarGeometry GetScrollbarGeometryAtOffset(LayoutBox box, ScrollOffset offset) =>
        ToScrollbarGeometry(_native.GetScrollbarGeometryAtOffset(
            box.NativeHandle,
            new NativeScrollOffset(offset.X, offset.Y)));

    public void Dispose() => _native.Dispose();

    private static LayoutBox? Project(NativeLayoutBox nativeBox, NativeLayoutResult nativeResult, DomInterop dom,
        StyleMap styles, Dictionary<IntPtr, LayoutBox> boxes, Dictionary<ElementNode, LayoutBox> primary)
    {
        if (nativeBox.Handle == IntPtr.Zero)
        {
            throw new InvalidOperationException("Native layout returned a null root box.");
        }

        if (boxes.TryGetValue(nativeBox.Handle, out var existing)) return existing;
        // The native engine may create layout-only nodes for CSS generated content. They must
        // paint and take part in layout, but are deliberately not exposed as managed DOM nodes
        // (and cannot be event targets). Leave them out of the host-facing projection.
        var element = dom.GetManagedElement(nativeBox.Element);
        if (element is null) return null;
        var kind = nativeBox.Kind == 2 ? LayoutBoxKind.Text : LayoutBoxKind.Element;
        var box = new LayoutBox(kind, element, styles[element])
        {
            NativeHandle = nativeBox.Handle,
            StyleNodeId = nativeBox.Element == IntPtr.Zero ? 0 : dom.Native.GetStyleNodeId(nativeBox.Element),
            Text = nativeBox.Text,
            MarginBox = ToRect(nativeBox.MarginBox),
            BorderBox = ToRect(nativeBox.BorderBox),
            PaddingBox = ToRect(nativeBox.PaddingBox),
            ContentBox = ToRect(nativeBox.ContentBox),
            IsAtomicControl = nativeBox.IsAtomicControl,
            Scroll = new ScrollMetrics(nativeBox.ScrollMetrics.ScrollableOverflowX,
                nativeBox.ScrollMetrics.ScrollableOverflowY,
                nativeBox.ScrollMetrics.ScrollOffsetX,
                nativeBox.ScrollMetrics.ScrollOffsetY),
            Scrollbar = new ScrollbarGeometry(nativeBox.ScrollbarGeometry.HorizontalVisible != 0,
                nativeBox.ScrollbarGeometry.VerticalVisible != 0,
                ToRect(nativeBox.ScrollbarGeometry.HorizontalTrack),
                ToRect(nativeBox.ScrollbarGeometry.HorizontalThumb),
                ToRect(nativeBox.ScrollbarGeometry.VerticalTrack),
                ToRect(nativeBox.ScrollbarGeometry.VerticalThumb),
                ToRect(nativeBox.ScrollbarGeometry.Corner)),
        };
        boxes.Add(nativeBox.Handle, box);
        if (kind == LayoutBoxKind.Element) primary.TryAdd(element, box);
        for (var index = 0; index < nativeBox.ChildCount; index++)
        {
            if (Project(nativeResult.GetChild(nativeBox, index), nativeResult, dom, styles, boxes, primary) is { } child)
            {
                box.AddChild(child);
            }
        }

        return box;
    }

    private NativeTextMetrics Measure(string text, NativeComputedStyle style, float maxWidth)
    {
        var managedStyle = StyleRuntimeBridge.ToComputedStyle(style, new ElementNode("span"));
        // The C ABI uses zero as its unconstrained-width sentinel. ITextMeasurer follows the
        // .NET convention and expects PositiveInfinity for that same meaning.
        var metrics = _textMeasurer.MeasureText(text, managedStyle,
            maxWidth > 0 ? maxWidth : float.PositiveInfinity);
        return new NativeTextMetrics(metrics.Width, metrics.Height, metrics.Baseline);
    }

    private static RectF ToRect(NativeRect rect) => new(rect.X, rect.Y, rect.Width, rect.Height);

    private static ScrollbarGeometry ToScrollbarGeometry(NativeScrollbarGeometry geometry) => new(
        geometry.HorizontalVisible != 0,
        geometry.VerticalVisible != 0,
        ToRect(geometry.HorizontalTrack),
        ToRect(geometry.HorizontalThumb),
        ToRect(geometry.VerticalTrack),
        ToRect(geometry.VerticalThumb),
        ToRect(geometry.Corner));
}
