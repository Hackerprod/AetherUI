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

    public NativeLayoutResult CreateNativeLayout(DomInterop dom, NativeStyleMap nativeStyles, SizeF viewport,
        Func<ElementNode, float> scrollOffsetFor) =>
        _native.Layout(dom.Native, nativeStyles, viewport.Width, viewport.Height,
            handle => dom.GetManagedElement(handle) is { } element ? scrollOffsetFor(element) : 0);

    public LayoutResult Project(NativeLayoutResult nativeResult, DomInterop dom, StyleMap styles, SizeF viewport)
    {
        var boxes = new Dictionary<IntPtr, LayoutBox>();
        var primary = new Dictionary<ElementNode, LayoutBox>();
        var root = Project(nativeResult.Root, nativeResult, dom, styles, boxes, primary);
        var paintOrder = new List<LayoutBox>(nativeResult.PaintOrderCount);
        for (var index = 0; index < nativeResult.PaintOrderCount; index++)
        {
            var nativeBox = nativeResult.GetPaintOrderBox(index);
            if (boxes.TryGetValue(nativeBox.Handle, out var box)) paintOrder.Add(box);
        }

        return new LayoutResult(root, primary, paintOrder, viewport);
    }

    public void Dispose() => _native.Dispose();

    private static LayoutBox Project(NativeLayoutBox nativeBox, NativeLayoutResult nativeResult, DomInterop dom,
        StyleMap styles, Dictionary<IntPtr, LayoutBox> boxes, Dictionary<ElementNode, LayoutBox> primary)
    {
        if (nativeBox.Handle == IntPtr.Zero)
        {
            throw new InvalidOperationException("Native layout returned a null root box.");
        }

        if (boxes.TryGetValue(nativeBox.Handle, out var existing)) return existing;
        var element = dom.GetManagedElement(nativeBox.Element)
            ?? throw new InvalidOperationException("Native layout box points outside the current DOM.");
        var kind = nativeBox.Kind == 2 ? LayoutBoxKind.Text : LayoutBoxKind.Element;
        var box = new LayoutBox(kind, element, styles[element])
        {
            Text = nativeBox.Text,
            MarginBox = ToRect(nativeBox.MarginBox),
            BorderBox = ToRect(nativeBox.BorderBox),
            PaddingBox = ToRect(nativeBox.PaddingBox),
            ContentBox = ToRect(nativeBox.ContentBox),
            IsAtomicControl = nativeBox.IsAtomicControl,
            ScrollableOverflowY = nativeBox.ScrollableOverflowY,
            ScrollOffsetY = nativeBox.ScrollOffsetY,
        };
        boxes.Add(nativeBox.Handle, box);
        if (kind == LayoutBoxKind.Element) primary.TryAdd(element, box);
        for (var index = 0; index < nativeBox.ChildCount; index++)
        {
            box.AddChild(Project(nativeResult.GetChild(nativeBox, index), nativeResult, dom, styles, boxes, primary));
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
}
