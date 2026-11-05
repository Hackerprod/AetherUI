using AetherUI.Css;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using System.Diagnostics;

namespace AetherUI.Runtime;

/// <summary>
/// Document host region: Scroll offsets, wheel gestures and scrollbar interaction.
/// </summary>
public sealed partial class UiDocumentHost
{
    /// <summary>
    /// Two-axis scroll offset per scroll-container element. Most real markup scrolls a plain
    /// <c>&lt;div class="..."&gt;</c> with no id, so this is keyed by <see cref="ElementNode"/>
    /// instance (like <see cref="_computedStyles"/>) rather than by id — which means, like every
    /// other node-identity-dependent piece of state here, it resets on a markup rebuild instead of
    /// surviving it. Cleared alongside <see cref="_computedStyles"/> in RebuildDocument.
    /// </summary>
    private readonly Dictionary<ElementNode, ScrollOffset> _scrollOffsets = new();
    private readonly Dictionary<long, ScrollVisualOffset> _scrollOffsetsByStyleNode = new();
    private readonly Dictionary<long, LayoutBox> _scrollBoxesByStyleNode = new();

    private ScrollbarDrag? _scrollbarDrag;
    // A scrollbar press belongs to user-agent chrome, not DOM content. Keep this
    // state until release even if a resize/rebuild cancels the live drag.
    private bool _scrollbarPointerCaptured;

    private enum ScrollAxis : byte
    {
        Horizontal,
        Vertical,
    }

    private sealed record ScrollbarDrag(ElementNode Container, ScrollAxis Axis, float GrabOffset);

    private ScrollOffset ScrollOffsetFor(ElementNode element) =>
        _scrollOffsets.TryGetValue(element, out var offset) ? offset : default;

    public ScrollOffset GetScrollOffset(ElementNode element)
    {
        ArgumentNullException.ThrowIfNull(element);
        return ScrollOffsetFor(element);
    }

    /// <summary>
    /// Programmatic scroll (CSS Overflow 3.1: hidden containers accept script-driven offsets even
    /// though the user cannot scroll them). No-op for elements that are not scroll containers.
    /// </summary>
    public void SetScrollOffset(ElementNode element, ScrollOffset offset)
    {
        ArgumentNullException.ThrowIfNull(element);
        ThrowIfDisposed();
        EnsureVisualTree();
        if (_layout?.FindPrimaryBox(element) is not { } box || !IsScrollContainer(box)) return;
        var clamped = new ScrollOffset(
            Math.Clamp(offset.X, 0f, box.ScrollableOverflowX),
            Math.Clamp(offset.Y, 0f, box.ScrollableOverflowY));
        if (clamped == ScrollOffsetFor(element)) return;
        _scrollOffsets[element] = clamped;
        _scrollOffsetsByStyleNode[_nativeDom!.GetStyleNodeId(element)] = new ScrollVisualOffset(clamped.X, clamped.Y);
        InvalidateScroll();
    }

    private void RebuildScrollBoxIndex()
    {
        _scrollBoxesByStyleNode.Clear();
        var liveScrollElements = new HashSet<ElementNode>();
        if (_layout is null) return;
        foreach (var box in _layout.Root.DescendantsAndSelf())
        {
            if (box.StyleNodeId == 0) continue;
            if (IsScrollContainer(box))
            {
                _scrollBoxesByStyleNode[box.StyleNodeId] = box;
                liveScrollElements.Add(box.Element);
                if (_scrollOffsets.TryGetValue(box.Element, out var offset))
                {
                    offset = new ScrollOffset(
                        Math.Clamp(offset.X, 0f, box.ScrollableOverflowX),
                        Math.Clamp(offset.Y, 0f, box.ScrollableOverflowY));
                    _scrollOffsets[box.Element] = offset;
                    _scrollOffsetsByStyleNode[box.StyleNodeId] = new ScrollVisualOffset(offset.X, offset.Y);
                }
            }
        }

        foreach (var element in _scrollOffsets.Keys.Where(element => !liveScrollElements.Contains(element)).ToArray())
        {
            _scrollOffsets.Remove(element);
            if (_nativeDom is not null) _scrollOffsetsByStyleNode.Remove(_nativeDom.GetStyleNodeId(element));
        }
    }

    /// <summary>
    /// Routes a mouse-wheel notch to the nearest scrollable ancestor of whatever's under the
    /// cursor (walking up like <see cref="Dispatch"/> does for bubbling DOM events), matching how
    /// a real browser picks which scroll container a wheel event affects.
    /// </summary>
    public void MouseWheelScrolled(PointF point, float notches)
    {
        ScrollByWheel(point, ScrollAxis.Vertical, -notches * 48f);
    }

    /// <summary>Routes a horizontal wheel gesture, including Shift+wheel, through nested overflow.</summary>
    public void MouseHorizontalWheelScrolled(PointF point, float notches)
    {
        // WM_MOUSEHWHEEL reports positive deltas for motion towards the right.
        ScrollByWheel(point, ScrollAxis.Horizontal, notches * 48f);
    }

    private void ScrollByWheel(PointF point, ScrollAxis axis, float delta)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        for (var current = HitTest(point); current is not null; current = current.Parent as ElementNode)
        {
            var box = _layout!.FindPrimaryBox(current);
            if (box is null) continue;
            if (!IsUserScrollable(EffectiveOverflow(box, axis))) continue;

            var range = ScrollRange(box, axis);
            if (range <= 0) continue;
            var currentOffset = ScrollOffsetOnAxis(current, axis);
            var next = Math.Clamp(currentOffset + delta, 0f, range);
            // At a limit this container cannot consume the gesture: chain it to
            // the nearest ancestor rather than swallowing it.
            if (MathF.Abs(next - currentOffset) < .001f) continue;

            SetScrollOffset(current, box, axis, next);
            InvalidateScroll();
            return;
        }
    }

    private bool TryBeginScrollbarInteraction(PointF point)
    {
        foreach (var box in _layout!.PaintOrder.Reverse())
        {
            if (!IsCurrentlyVisible(box.Element)) continue;
            var scrollbar = ScrollbarGeometryForPointer(box);
            if (scrollbar.HorizontalVisible && scrollbar.HorizontalThumb.Contains(point))
            {
                BeginScrollbarDrag(box, ScrollAxis.Horizontal, point.X - scrollbar.HorizontalThumb.X);
                return true;
            }
            if (scrollbar.VerticalVisible && scrollbar.VerticalThumb.Contains(point))
            {
                BeginScrollbarDrag(box, ScrollAxis.Vertical, point.Y - scrollbar.VerticalThumb.Y);
                return true;
            }
            if (scrollbar.HorizontalVisible && scrollbar.HorizontalTrack.Contains(point))
            {
                PageScroll(box, ScrollAxis.Horizontal, point.X);
                return true;
            }
            if (scrollbar.VerticalVisible && scrollbar.VerticalTrack.Contains(point))
            {
                PageScroll(box, ScrollAxis.Vertical, point.Y);
                return true;
            }
        }
        return false;
    }

    private void BeginScrollbarDrag(LayoutBox box, ScrollAxis axis, float grabOffset)
    {
        _scrollbarDrag = new ScrollbarDrag(box.Element, axis, grabOffset);
        _scrollbarPointerCaptured = true;
    }

    private void UpdateScrollbarDrag(PointF point)
    {
        var drag = _scrollbarDrag;
        if (drag is null || !IsCurrentlyVisible(drag.Container) ||
            _layout?.FindPrimaryBox(drag.Container) is not { } box)
        {
            CancelScrollbarDrag();
            return;
        }

        var scrollbar = ScrollbarGeometryForPointer(box);
        var track = drag.Axis == ScrollAxis.Horizontal ? scrollbar.HorizontalTrack : scrollbar.VerticalTrack;
        var thumb = drag.Axis == ScrollAxis.Horizontal ? scrollbar.HorizontalThumb : scrollbar.VerticalThumb;
        var trackStart = drag.Axis == ScrollAxis.Horizontal ? track.X : track.Y;
        var trackLength = drag.Axis == ScrollAxis.Horizontal ? track.Width : track.Height;
        var thumbLength = drag.Axis == ScrollAxis.Horizontal ? thumb.Width : thumb.Height;
        var pointer = drag.Axis == ScrollAxis.Horizontal ? point.X : point.Y;
        var available = Math.Max(0f, trackLength - thumbLength);
        var thumbStart = Math.Clamp(pointer - drag.GrabOffset, trackStart, trackStart + available);
        var progress = available > 0 ? (thumbStart - trackStart) / available : 0;
        SetScrollOffset(drag.Container, box, drag.Axis, progress * ScrollRange(box, drag.Axis));
        InvalidateScroll();
    }

    private void PageScroll(LayoutBox box, ScrollAxis axis, float pointer)
    {
        var scrollbar = ScrollbarGeometryForPointer(box);
        var thumb = axis == ScrollAxis.Horizontal ? scrollbar.HorizontalThumb : scrollbar.VerticalThumb;
        var thumbStart = axis == ScrollAxis.Horizontal ? thumb.X : thumb.Y;
        var thumbEnd = axis == ScrollAxis.Horizontal ? thumb.Right : thumb.Bottom;
        var direction = pointer < thumbStart ? -1f : pointer > thumbEnd ? 1f : 0f;
        if (direction == 0) return;

        var page = axis == ScrollAxis.Horizontal ? box.ContentBox.Width : box.ContentBox.Height;
        var current = ScrollOffsetOnAxis(box.Element, axis);
        var next = Math.Clamp(current + direction * page, 0f, ScrollRange(box, axis));
        if (MathF.Abs(next - current) < .001f) return;
        _scrollbarPointerCaptured = true;
        SetScrollOffset(box.Element, box, axis, next);
        InvalidateScroll();
    }

    private void SetScrollOffset(ElementNode element, LayoutBox box, ScrollAxis axis, float value)
    {
        var current = ScrollOffsetFor(element);
        _scrollOffsets[element] = axis == ScrollAxis.Horizontal
            ? new ScrollOffset(value, current.Y)
            : new ScrollOffset(current.X, value);
        var offset = _scrollOffsets[element];
        _scrollOffsetsByStyleNode[_nativeDom!.GetStyleNodeId(element)] = new ScrollVisualOffset(offset.X, offset.Y);
    }

    private static float ScrollRange(LayoutBox box, ScrollAxis axis) =>
        axis == ScrollAxis.Horizontal ? box.ScrollableOverflowX : box.ScrollableOverflowY;

    /// <summary>
    /// CSS Overflow 3.3: the root box stands in for the viewport scroll, so its container and
    /// user-scroll decisions follow the propagated viewport overflow while its own style keeps its
    /// used value for paint. Every other box uses its own computed overflow.
    /// </summary>
    private OverflowKind EffectiveOverflow(LayoutBox box, ScrollAxis axis)
    {
        if (_layout is not null && ReferenceEquals(box, _layout.Root))
        {
            return axis == ScrollAxis.Horizontal ? _layout.ViewportOverflowX : _layout.ViewportOverflowY;
        }
        return axis == ScrollAxis.Horizontal ? box.Style.OverflowX : box.Style.OverflowY;
    }

    private static bool IsScrollContainerAxis(OverflowKind overflow) =>
        overflow is OverflowKind.Auto or OverflowKind.Hidden or OverflowKind.Scroll;

    private bool IsScrollContainer(LayoutBox box)
    {
        var x = EffectiveOverflow(box, ScrollAxis.Horizontal);
        var y = EffectiveOverflow(box, ScrollAxis.Vertical);
        // `scroll` is always a scroll container, even with zero overflow (§3.1).
        if (x == OverflowKind.Scroll || y == OverflowKind.Scroll) return true;
        return (IsScrollContainerAxis(x) && box.ScrollableOverflowX > 0) ||
               (IsScrollContainerAxis(y) && box.ScrollableOverflowY > 0);
    }

    private static bool IsUserScrollable(OverflowKind overflow) =>
        overflow is OverflowKind.Auto or OverflowKind.Scroll;

    private float ScrollOffsetOnAxis(ElementNode element, ScrollAxis axis)
    {
        var offset = ScrollOffsetFor(element);
        return axis == ScrollAxis.Horizontal ? offset.X : offset.Y;
    }

    private ScrollbarGeometry ScrollbarGeometryForPointer(LayoutBox box) =>
        TranslateScrollbarGeometry(
            _layoutInterop.GetScrollbarGeometryAtOffset(box, ScrollOffsetFor(box.Element)),
            AncestorScrollTranslation(box.Element));

    private PointF AncestorScrollTranslation(ElementNode element)
    {
        var x = 0f;
        var y = 0f;
        for (var ancestor = element.Parent as ElementNode; ancestor is not null; ancestor = ancestor.Parent as ElementNode)
        {
            var offset = ScrollOffsetFor(ancestor);
            x -= offset.X;
            y -= offset.Y;
        }

        return new PointF(x, y);
    }

    private static ScrollbarGeometry TranslateScrollbarGeometry(ScrollbarGeometry geometry, PointF delta) => new(
        geometry.HorizontalVisible,
        geometry.VerticalVisible,
        Translate(geometry.HorizontalTrack, delta),
        Translate(geometry.HorizontalThumb, delta),
        Translate(geometry.VerticalTrack, delta),
        Translate(geometry.VerticalThumb, delta),
        Translate(geometry.Corner, delta));

    private static RectF Translate(RectF rect, PointF delta) =>
        rect.IsEmpty ? rect : new RectF(rect.X + delta.X, rect.Y + delta.Y, rect.Width, rect.Height);

    private void CancelScrollbarDrag() => _scrollbarDrag = null;

    private void InvalidateScroll()
    {
        _invalidation |= (VisualInvalidation)NativeRenderPlanInterop.VisualInvalidationForScroll(
            _displayListContainsPatchedChunks);
        RequestRender();
    }

}
