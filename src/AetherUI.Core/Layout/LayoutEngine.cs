using System.Text;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Primitives;

namespace AetherUI.Layout;

/// <summary>
/// CSS layout profile implemented by AetherUI 0.1: block flow, inline wrapping,
/// inline-block controls, and one-dimensional flex containers.
/// </summary>
public sealed class LayoutEngine
{
    private readonly ITextMeasurer _textMeasurer;
    private readonly Dictionary<ElementNode, LayoutBox> _primaryBoxes = [];
    private readonly List<LayoutBox> _paintOrder = [];
    private StyleMap _styles = null!;
    private SizeF _viewport;
    private float _rootFontSize = 16;
    private Func<ElementNode, float>? _getScrollOffsetY;

    public LayoutEngine(ITextMeasurer textMeasurer)
    {
        _textMeasurer = textMeasurer ?? throw new ArgumentNullException(nameof(textMeasurer));
    }

    public LayoutResult Layout(DocumentNode document, StyleMap styles, SizeF viewport, Func<ElementNode, float>? getScrollOffsetY = null)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(styles);
        if (viewport.Width <= 0 || viewport.Height <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(viewport), "El viewport debe tener dimensiones positivas.");
        }

        _styles = styles;
        _viewport = viewport;
        _getScrollOffsetY = getScrollOffsetY;
        _primaryBoxes.Clear();
        _paintOrder.Clear();

        var rootElement = document.Body ?? document.DocumentElement
            ?? throw new InvalidOperationException("El documento no contiene un elemento renderizable.");
        _rootFontSize = styles[rootElement].FontSize;

        var root = LayoutElement(rootElement, new RectF(0, 0, viewport.Width, viewport.Height), 0, forceAvailableWidth: true);
        return new LayoutResult(root, new Dictionary<ElementNode, LayoutBox>(_primaryBoxes), _paintOrder.ToArray(), viewport);
    }

    private LayoutBox LayoutElement(
        ElementNode element,
        RectF containingBlock,
        float normalFlowY,
        bool forceAvailableWidth = false,
        float? forcedBorderWidth = null,
        float? forcedBorderHeight = null)
    {
        var style = _styles[element];
        var box = new LayoutBox(LayoutBoxKind.Element, element, style);
        Register(box);

        if (style.Display == DisplayKind.None || !style.Visible)
        {
            return box;
        }

        var margin = ResolveMarginEdges(style.Margin, containingBlock.Width, style);
        var padding = ResolveEdges(style.Padding, containingBlock.Width, style);
        var border = ResolveEdges(style.BorderWidth, containingBlock.Width, style);

        var availableBorderWidth = MathF.Max(0, containingBlock.Width - margin.Horizontal);
        var specifiedWidth = Resolve(style.Width, containingBlock.Width, style);
        var borderWidth = specifiedWidth;

        if (forcedBorderWidth.HasValue)
        {
            // A flex-item's grow-distributed width (computed by the parent flex container from
            // leftover free space), overriding the item's own auto/specified width for this pass.
            borderWidth = forcedBorderWidth.Value;
        }
        else if (!float.IsFinite(borderWidth))
        {
            borderWidth = forceAvailableWidth
                ? availableBorderWidth
                : MeasureIntrinsicWidth(element, style, padding, border, containingBlock.Width);
        }
        else if (style.BoxSizing == BoxSizing.ContentBox)
        {
            borderWidth += padding.Horizontal + border.Horizontal;
        }

        var minWidth = Resolve(style.MinWidth, containingBlock.Width, style);
        if (float.IsFinite(minWidth))
        {
            var minBorder = style.BoxSizing == BoxSizing.ContentBox ? minWidth + padding.Horizontal + border.Horizontal : minWidth;
            borderWidth = MathF.Max(borderWidth, minBorder);
        }

        var maxWidth = Resolve(style.MaxWidth, containingBlock.Width, style);
        if (float.IsFinite(maxWidth))
        {
            var maxBorder = style.BoxSizing == BoxSizing.ContentBox ? maxWidth + padding.Horizontal + border.Horizontal : maxWidth;
            borderWidth = MathF.Min(borderWidth, maxBorder);
        }

        borderWidth = MathF.Max(border.Horizontal + padding.Horizontal, MathF.Min(borderWidth, availableBorderWidth));
        var borderX = containingBlock.X + margin.Left;
        var borderY = normalFlowY + margin.Top;
        var contentX = borderX + border.Left + padding.Left;
        var contentY = borderY + border.Top + padding.Top;
        var contentWidth = MathF.Max(0, borderWidth - border.Horizontal - padding.Horizontal);

        // A forced height (align-items:stretch on a row-direction flex container) names the
        // final border-box height directly, matching forcedBorderWidth; convert it through the
        // same box-sizing math as an explicit CSS height so it reproduces exactly that border box.
        var explicitHeight = forcedBorderHeight.HasValue
            ? style.BoxSizing == BoxSizing.ContentBox
                ? MathF.Max(0, forcedBorderHeight.Value - padding.Vertical - border.Vertical)
                : forcedBorderHeight.Value
            : Resolve(style.Height, containingBlock.Height, style);
        var intrinsicControl = IsAtomicControl(element);
        box.IsAtomicControl = intrinsicControl;
        var definiteContentHeight = float.IsFinite(explicitHeight)
            ? style.BoxSizing == BoxSizing.ContentBox
                ? MathF.Max(0, explicitHeight)
                : MathF.Max(0, explicitHeight - padding.Vertical - border.Vertical)
            : float.NaN;
        var childContainingHeight = float.IsFinite(definiteContentHeight)
            ? definiteContentHeight
            : containingBlock.Height;

        float contentHeight;
        if (intrinsicControl)
        {
            contentHeight = LayoutAtomicControl(box, element, style, contentWidth, explicitHeight, padding, border);
        }
        else if (style.Display == DisplayKind.Flex)
        {
            contentHeight = LayoutFlexChildren(
                box,
                element,
                new RectF(contentX, contentY, contentWidth, childContainingHeight),
                style,
                float.IsFinite(definiteContentHeight));
        }
        else
        {
            contentHeight = LayoutFlowChildren(box, element, new RectF(contentX, contentY, contentWidth, childContainingHeight), style);
        }

        // A scroll container only makes sense when its own content-box height is definite (an
        // auto-height box just grows to fit its content, so there's nothing to scroll to). Content
        // is already laid out in absolute coordinates at this point, so overflowing it is just a
        // matter of shifting every child up by the clamped offset — the same primitive used for
        // text-align and flex positioning (OffsetBox).
        if ((style.Overflow is OverflowKind.Auto or OverflowKind.Scroll) && float.IsFinite(definiteContentHeight))
        {
            var scrollableOverflowY = MathF.Max(0, contentHeight - definiteContentHeight);
            box.ScrollableOverflowY = scrollableOverflowY;
            if (scrollableOverflowY > 0)
            {
                var desired = _getScrollOffsetY?.Invoke(element) ?? 0f;
                var scrollOffsetY = Math.Clamp(desired, 0f, scrollableOverflowY);
                box.ScrollOffsetY = scrollOffsetY;
                if (scrollOffsetY != 0f)
                {
                    foreach (var child in box.Children)
                    {
                        OffsetBox(child, 0, -scrollOffsetY);
                    }
                }
            }
        }

        var borderHeight = float.IsFinite(explicitHeight)
            ? (style.BoxSizing == BoxSizing.ContentBox ? explicitHeight + padding.Vertical + border.Vertical : explicitHeight)
            : contentHeight + padding.Vertical + border.Vertical;

        var minHeight = Resolve(style.MinHeight, containingBlock.Height, style);
        if (float.IsFinite(minHeight))
        {
            var minBorder = style.BoxSizing == BoxSizing.ContentBox ? minHeight + padding.Vertical + border.Vertical : minHeight;
            borderHeight = MathF.Max(borderHeight, minBorder);
        }

        var maxHeight = Resolve(style.MaxHeight, containingBlock.Height, style);
        if (float.IsFinite(maxHeight))
        {
            var maxBorder = style.BoxSizing == BoxSizing.ContentBox ? maxHeight + padding.Vertical + border.Vertical : maxHeight;
            borderHeight = MathF.Min(borderHeight, maxBorder);
        }

        borderHeight = MathF.Max(border.Vertical + padding.Vertical, borderHeight);
        box.BorderBox = new RectF(borderX, borderY, borderWidth, borderHeight);
        box.PaddingBox = box.BorderBox.Deflate(border);
        box.ContentBox = box.PaddingBox.Deflate(padding);
        box.MarginBox = new RectF(containingBlock.X, normalFlowY, borderWidth + margin.Horizontal, borderHeight + margin.Vertical);
        return box;
    }

    private float LayoutAtomicControl(
        LayoutBox box,
        ElementNode element,
        ComputedStyle style,
        float contentWidth,
        float explicitHeight,
        ThicknessF padding,
        ThicknessF border)
    {
        float naturalHeight;
        if (float.IsFinite(explicitHeight))
        {
            naturalHeight = style.BoxSizing == BoxSizing.ContentBox
                ? explicitHeight
                : MathF.Max(0, explicitHeight - padding.Vertical - border.Vertical);
        }
        else
        {
            // FontSize*LineHeight is only an approximation of a real font's rendered glyph
            // height — bold weights in particular can render taller than that guess, which used
            // to clip the top of button/input text against a content box sized from the guess
            // instead of the actual metrics DisplayListBuilder paints with.
            var displayedText = GetAtomicControlText(element);
            naturalHeight = string.IsNullOrEmpty(displayedText)
                ? style.FontSize * style.LineHeight
                : _textMeasurer.MeasureText(displayedText, style, contentWidth).Height;
        }

        return MathF.Max(naturalHeight, style.FontSize * 1.1f);
    }

    private static string GetAtomicControlText(ElementNode element)
    {
        if (element.TagName == "button")
        {
            return NormalizeWhitespace(element.TextContent);
        }

        if (element.RuntimeValue is { Length: > 0 } value)
        {
            return value;
        }

        return element.GetAttribute("value") is { Length: > 0 } attrValue
            ? attrValue
            : element.GetAttribute("placeholder") ?? string.Empty;
    }

    private float LayoutFlowChildren(LayoutBox parentBox, ElementNode parent, RectF content, ComputedStyle parentStyle)
    {
        var cursorY = content.Y;
        var inlineItems = new List<InlineItem>();

        void FlushInline()
        {
            if (inlineItems.Count == 0)
            {
                return;
            }

            cursorY += LayoutInlineItems(parentBox, inlineItems, content.X, cursorY, content.Width, parentStyle);
            inlineItems.Clear();
        }

        foreach (var node in parent.Children)
        {
            switch (node)
            {
                case CommentNode:
                    continue;
                case TextNode textNode:
                    CollectTextItems(parent, parentStyle, textNode.TextContent, inlineItems);
                    break;
                case ElementNode child:
                {
                    var childStyle = _styles[child];
                    if (childStyle.Display == DisplayKind.None || !childStyle.Visible)
                    {
                        continue;
                    }

                    if (childStyle.Display is DisplayKind.Block or DisplayKind.Flex)
                    {
                        FlushInline();
                        var childBox = LayoutElement(child, new RectF(content.X, cursorY, content.Width, content.Height), cursorY, forceAvailableWidth: true);
                        parentBox.AddChild(childBox);
                        cursorY += childBox.MarginBox.Height;
                    }
                    else if (childStyle.Display == DisplayKind.LineBreak)
                    {
                        inlineItems.Add(InlineItem.LineBreak(child, childStyle));
                    }
                    else if (IsAtomicControl(child) || childStyle.Display == DisplayKind.InlineBlock)
                    {
                        inlineItems.Add(InlineItem.Atomic(child, childStyle));
                    }
                    else
                    {
                        CollectInlineElement(child, childStyle, inlineItems);
                    }

                    break;
                }
            }
        }

        FlushInline();
        return MathF.Max(0, cursorY - content.Y);
    }

    private float LayoutInlineItems(
        LayoutBox parentBox,
        IReadOnlyList<InlineItem> items,
        float startX,
        float startY,
        float availableWidth,
        ComputedStyle parentStyle)
    {
        var cursorX = startX;
        var cursorY = startY;
        var lineHeight = MathF.Max(parentStyle.FontSize * parentStyle.LineHeight, 1);
        var lineStartIndex = parentBox.Children.Count;
        var lineWidths = new List<(int Start, int End, float Width)>();

        void FinishLine(bool forceAdvance)
        {
            var end = parentBox.Children.Count;
            var width = MathF.Max(0, cursorX - startX);
            if (end > lineStartIndex || forceAdvance)
            {
                lineWidths.Add((lineStartIndex, end, width));
                cursorY += lineHeight;
            }

            cursorX = startX;
            lineHeight = MathF.Max(parentStyle.FontSize * parentStyle.LineHeight, 1);
            lineStartIndex = end;
        }

        foreach (var item in items)
        {
            if (item.IsLineBreak)
            {
                FinishLine(forceAdvance: true);
                continue;
            }

            if (item.IsAtomic)
            {
                var provisional = LayoutElement(
                    item.Element,
                    new RectF(startX, cursorY, availableWidth, _viewport.Height),
                    cursorY);
                var width = provisional.MarginBox.Width;
                if (cursorX > startX && cursorX + width > startX + availableWidth)
                {
                    var oldY = provisional.MarginBox.Y;
                    FinishLine(forceAdvance: false);
                    OffsetBox(provisional, 0, cursorY - oldY);
                }

                var dx = cursorX - provisional.MarginBox.X;
                OffsetBox(provisional, dx, 0);
                parentBox.AddChild(provisional);
                cursorX += provisional.MarginBox.Width;
                lineHeight = MathF.Max(lineHeight, provisional.MarginBox.Height);
                continue;
            }

            foreach (var token in TokenizeInlineText(item.Text!))
            {
                var isWhitespace = token.All(char.IsWhiteSpace);
                var normalized = isWhitespace ? " " : token;
                if (isWhitespace && cursorX <= startX)
                {
                    continue;
                }

                var metrics = _textMeasurer.MeasureText(normalized, item.Style);
                if (!isWhitespace && cursorX > startX && cursorX + metrics.Width > startX + availableWidth)
                {
                    FinishLine(forceAdvance: false);
                }

                var fragment = new LayoutBox(LayoutBoxKind.Text, item.Element, item.Style)
                {
                    Text = normalized,
                    BorderBox = new RectF(cursorX, cursorY, metrics.Width, metrics.Height),
                    PaddingBox = new RectF(cursorX, cursorY, metrics.Width, metrics.Height),
                    ContentBox = new RectF(cursorX, cursorY, metrics.Width, metrics.Height),
                    MarginBox = new RectF(cursorX, cursorY, metrics.Width, metrics.Height),
                };
                parentBox.AddChild(fragment);
                _paintOrder.Add(fragment);
                cursorX += metrics.Width;
                lineHeight = MathF.Max(lineHeight, metrics.Height);
            }
        }

        if (parentBox.Children.Count > lineStartIndex)
        {
            FinishLine(forceAdvance: false);
        }

        ApplyLineAlignment(parentBox, lineWidths, startX, availableWidth, parentStyle.TextAlign);
        return lineWidths.Count == 0 ? 0 : MathF.Max(0, cursorY - startY);
    }

    private float LayoutFlexChildren(
        LayoutBox parentBox,
        ElementNode parent,
        RectF content,
        ComputedStyle style,
        bool hasDefiniteHeight)
    {
        var children = parent.Children.OfType<ElementNode>()
            .Where(child => _styles[child].Display != DisplayKind.None && _styles[child].Visible)
            .ToArray();
        if (children.Length == 0)
        {
            // A flex container with only text content (no element children) has no flex items to
            // lay out, but the text itself must still render — fall back to normal inline flow
            // instead of silently dropping it (e.g. <div class="nav-item" style="display:flex">
            // Dashboard</div>, a common pattern for icon+label rows written without a wrapping span).
            var inlineItems = new List<InlineItem>();
            foreach (var node in parent.Children)
            {
                if (node is TextNode textNode)
                {
                    CollectTextItems(parent, style, textNode.TextContent, inlineItems);
                }
            }

            if (inlineItems.Count == 0)
            {
                return 0;
            }

            var usedHeight = LayoutInlineItems(parentBox, inlineItems, content.X, content.Y, content.Width, style);

            // LayoutInlineItems only knows plain top-aligned block/inline flow — it has no notion
            // of this element's own justify-content/align-items. A flex container that happens to
            // hold nothing but text (e.g. a single emoji centered in a circular avatar) still owes
            // its content that cross-axis alignment, so re-center the produced line(s) here.
            if (hasDefiniteHeight)
            {
                var verticalFree = MathF.Max(0, content.Height - usedHeight);
                var dy = style.AlignItems switch
                {
                    AlignItems.Center => verticalFree / 2,
                    AlignItems.End => verticalFree,
                    _ => 0f,
                };
                if (dy != 0f)
                {
                    foreach (var child in parentBox.Children)
                    {
                        OffsetBox(child, 0, dy);
                    }
                }
            }

            return usedHeight;
        }

        if (style.FlexDirection == FlexDirection.Column)
        {
            var boxes = new List<LayoutBox>(children.Length);
            var totalHeight = 0f;
            foreach (var child in children)
            {
                var childBox = LayoutElement(
                    child,
                    new RectF(content.X, content.Y, content.Width, content.Height),
                    content.Y,
                    forceAvailableWidth: style.AlignItems == AlignItems.Stretch);
                boxes.Add(childBox);
                totalHeight += childBox.MarginBox.Height;
            }

            totalHeight += style.Gap * Math.Max(0, boxes.Count - 1);
            var columnFree = hasDefiniteHeight ? MathF.Max(0, content.Height - totalHeight) : 0;
            var columnSpacing = style.Gap;
            var cursorY = style.JustifyContent switch
            {
                JustifyContent.Center => content.Y + columnFree / 2,
                JustifyContent.End => content.Y + columnFree,
                _ => content.Y,
            };
            if (style.JustifyContent == JustifyContent.SpaceBetween && boxes.Count > 1)
            {
                columnSpacing += columnFree / (boxes.Count - 1);
            }

            foreach (var childBox in boxes)
            {
                var x = style.AlignItems switch
                {
                    AlignItems.Center => content.X + (content.Width - childBox.MarginBox.Width) / 2,
                    AlignItems.End => content.Right - childBox.MarginBox.Width,
                    _ => content.X,
                };
                OffsetBox(childBox, x - childBox.MarginBox.X, cursorY - childBox.MarginBox.Y);
                parentBox.AddChild(childBox);
                cursorY += childBox.MarginBox.Height + columnSpacing;
            }

            return totalHeight;
        }

        // align-items:stretch only makes sense for a single line — with wrap, each line keeps its
        // own natural cross-size (no align-content/multi-line distribution implemented).
        var stretchRow = !style.FlexWrap && hasDefiniteHeight && style.AlignItems == AlignItems.Stretch;

        // Base pass: measure every child at its natural (pre-grow) size, needed to decide wrapping.
        var baseBoxes = new LayoutBox[children.Length];
        for (var i = 0; i < children.Length; i++)
        {
            var childStyle = _styles[children[i]];
            var stretchHeight = stretchRow && childStyle.Height.IsAuto ? content.Height : (float?)null;
            baseBoxes[i] = LayoutElement(
                children[i],
                new RectF(content.X, content.Y, content.Width, content.Height),
                content.Y,
                forcedBorderHeight: stretchHeight);
        }

        var lines = new List<List<int>>();
        if (style.FlexWrap)
        {
            var current = new List<int>();
            var runningWidth = 0f;
            for (var i = 0; i < children.Length; i++)
            {
                var itemWidth = baseBoxes[i].MarginBox.Width;
                if (current.Count > 0 && runningWidth + style.Gap + itemWidth > content.Width)
                {
                    lines.Add(current);
                    current = [];
                    runningWidth = 0f;
                }

                current.Add(i);
                runningWidth += (current.Count > 1 ? style.Gap : 0) + itemWidth;
            }

            if (current.Count > 0)
            {
                lines.Add(current);
            }
        }
        else
        {
            lines.Add([.. Enumerable.Range(0, children.Length)]);
        }

        var rowCursorY = content.Y;
        var rowTotalHeight = 0f;
        foreach (var line in lines)
        {
            var lineBoxes = new List<LayoutBox>(line.Count);
            var lineWidth = 0f;
            var lineMaxHeight = 0f;
            foreach (var i in line)
            {
                lineBoxes.Add(baseBoxes[i]);
                lineWidth += baseBoxes[i].MarginBox.Width;
                lineMaxHeight = MathF.Max(lineMaxHeight, baseBoxes[i].MarginBox.Height);
            }

            lineWidth += style.Gap * Math.Max(0, line.Count - 1);

            // flex-grow: leftover free space in this line is distributed among its auto-width
            // children proportionally to their flex-grow.
            var growTotal = 0f;
            foreach (var i in line)
            {
                var childStyle = _styles[children[i]];
                if (childStyle.FlexGrow > 0 && childStyle.Width.IsAuto)
                {
                    growTotal += childStyle.FlexGrow;
                }
            }

            if (growTotal > 0)
            {
                var growable = MathF.Max(0, content.Width - lineWidth);
                if (growable > 0)
                {
                    for (var li = 0; li < line.Count; li++)
                    {
                        var i = line[li];
                        var childStyle = _styles[children[i]];
                        if (childStyle.FlexGrow <= 0 || !childStyle.Width.IsAuto)
                        {
                            continue;
                        }

                        var extra = growable * (childStyle.FlexGrow / growTotal);
                        var stretchHeight = stretchRow && childStyle.Height.IsAuto ? content.Height : (float?)null;
                        var grown = LayoutElement(
                            children[i],
                            new RectF(content.X, content.Y, content.Width, content.Height),
                            content.Y,
                            forcedBorderWidth: lineBoxes[li].BorderBox.Width + extra,
                            forcedBorderHeight: stretchHeight);
                        lineBoxes[li] = grown;
                        lineMaxHeight = MathF.Max(lineMaxHeight, grown.MarginBox.Height);
                    }

                    lineWidth = content.Width;
                }
            }

            var free = MathF.Max(0, content.Width - lineWidth);
            var spacing = style.Gap;
            var cursorX = style.JustifyContent switch
            {
                JustifyContent.Center => content.X + free / 2,
                JustifyContent.End => content.X + free,
                _ => content.X,
            };
            if (style.JustifyContent == JustifyContent.SpaceBetween && lineBoxes.Count > 1)
            {
                spacing += free / (lineBoxes.Count - 1);
            }

            var crossExtent = lines.Count == 1 && hasDefiniteHeight ? content.Height : lineMaxHeight;
            foreach (var childBox in lineBoxes)
            {
                var y = style.AlignItems switch
                {
                    AlignItems.Center => rowCursorY + (crossExtent - childBox.MarginBox.Height) / 2,
                    AlignItems.End => rowCursorY + crossExtent - childBox.MarginBox.Height,
                    _ => rowCursorY,
                };
                OffsetBox(childBox, cursorX - childBox.MarginBox.X, y - childBox.MarginBox.Y);
                parentBox.AddChild(childBox);
                cursorX += childBox.MarginBox.Width + spacing;
            }

            rowCursorY += lineMaxHeight + style.Gap;
            rowTotalHeight += lineMaxHeight;
        }

        rowTotalHeight += style.Gap * Math.Max(0, lines.Count - 1);
        return hasDefiniteHeight ? MathF.Max(rowTotalHeight, content.Height) : rowTotalHeight;
    }

    private void CollectInlineElement(ElementNode element, ComputedStyle style, List<InlineItem> output)
    {
        foreach (var node in element.Children)
        {
            if (node is TextNode text)
            {
                CollectTextItems(element, style, text.TextContent, output);
            }
            else if (node is ElementNode child)
            {
                var childStyle = _styles[child];
                if (childStyle.Display == DisplayKind.LineBreak)
                {
                    output.Add(InlineItem.LineBreak(child, childStyle));
                }
                else if (IsAtomicControl(child) || childStyle.Display == DisplayKind.InlineBlock)
                {
                    output.Add(InlineItem.Atomic(child, childStyle));
                }
                else if (childStyle.Display != DisplayKind.None && childStyle.Visible)
                {
                    CollectInlineElement(child, childStyle, output);
                }
            }
        }
    }

    private static void CollectTextItems(ElementNode owner, ComputedStyle style, string text, List<InlineItem> output)
    {
        if (!string.IsNullOrEmpty(text))
        {
            output.Add(InlineItem.TextItem(owner, style, text));
        }
    }

    private float MeasureIntrinsicWidth(ElementNode element, ComputedStyle style, ThicknessF padding, ThicknessF border, float maximum)
    {
        if (element.TagName == "input")
        {
            return MathF.Min(maximum, 180 + padding.Horizontal + border.Horizontal);
        }

        if (style.Display == DisplayKind.Flex)
        {
            return MeasureIntrinsicFlexWidth(element, style, padding, border, maximum);
        }

        if (element.Children.OfType<ElementNode>().Any())
        {
            // A block container's own text (its ElementNode.TextContent) concatenates every
            // descendant's text recursively — meaningful for a text-only leaf (e.g. a button or an
            // inline-flex nav item), but nonsense for a div full of nested block children (it would
            // measure one enormous single-line string across the whole subtree's text, wildly
            // overshooting its real size). This engine has no real shrink-to-fit/min-content
            // algorithm for nested block content, so treat it like flex-basis:0 — an empty base that
            // an explicit min-width (if any) still clamps upward, and that flex-grow (if any) then
            // expands to fill the real available space. Returning the full "maximum" here instead
            // (tried first) breaks both the wrap-fit and the grow math: every such box would claim
            // the whole line's width up front, leaving no free space left for the grow pass to work
            // with, which overflows containers instead of sharing space with their siblings.
            return padding.Horizontal + border.Horizontal;
        }

        var text = NormalizeWhitespace(element.TextContent);
        var metrics = _textMeasurer.MeasureText(text, style);
        return metrics.Width + padding.Horizontal + border.Horizontal;
    }

    private float MeasureIntrinsicFlexWidth(ElementNode element, ComputedStyle style, ThicknessF padding, ThicknessF border, float maximum)
    {
        var children = element.Children.OfType<ElementNode>()
            .Where(child => _styles[child].Display != DisplayKind.None && _styles[child].Visible)
            .ToArray();

        if (children.Length == 0)
        {
            var text = NormalizeWhitespace(element.TextContent);
            var metrics = _textMeasurer.MeasureText(text, style);
            return metrics.Width + padding.Horizontal + border.Horizontal;
        }

        var childWidths = children.Select(child => MeasureChildNaturalWidth(child, maximum)).ToArray();
        var content = style.FlexDirection == FlexDirection.Column
            ? childWidths.Max()
            : childWidths.Sum() + style.Gap * Math.Max(0, children.Length - 1);

        return content + padding.Horizontal + border.Horizontal;
    }

    private float MeasureChildNaturalWidth(ElementNode child, float maximum)
    {
        var childStyle = _styles[child];
        var childMargin = ResolveMarginEdges(childStyle.Margin, maximum, childStyle);
        var specified = Resolve(childStyle.Width, maximum, childStyle);
        var childPadding = ResolveEdges(childStyle.Padding, maximum, childStyle);
        var childBorder = ResolveEdges(childStyle.BorderWidth, maximum, childStyle);

        float width;
        if (float.IsFinite(specified))
        {
            width = childStyle.BoxSizing == BoxSizing.ContentBox
                ? specified + childPadding.Horizontal + childBorder.Horizontal
                : specified;
        }
        else
        {
            width = MeasureIntrinsicWidth(child, childStyle, childPadding, childBorder, maximum);
        }

        return width + childMargin.Horizontal;
    }

    private ThicknessF ResolveEdges(CssEdges edges, float reference, ComputedStyle style) => new(
        ResolveOrZero(edges.Left, reference, style),
        ResolveOrZero(edges.Top, reference, style),
        ResolveOrZero(edges.Right, reference, style),
        ResolveOrZero(edges.Bottom, reference, style));

    private ThicknessF ResolveMarginEdges(CssEdges edges, float reference, ComputedStyle style) => new(
        ResolveMarginValue(edges.Left, reference, style),
        ResolveMarginValue(edges.Top, reference, style),
        ResolveMarginValue(edges.Right, reference, style),
        ResolveMarginValue(edges.Bottom, reference, style));

    private float ResolveMarginValue(CssLength value, float reference, ComputedStyle style)
    {
        var resolved = Resolve(value, reference, style);
        return float.IsFinite(resolved) ? resolved : 0;
    }

    private float Resolve(CssLength value, float reference, ComputedStyle style) =>
        value.Resolve(reference, style.FontSize, _rootFontSize, _viewport.Width, _viewport.Height);

    private float ResolveOrZero(CssLength value, float reference, ComputedStyle style)
    {
        var resolved = Resolve(value, reference, style);
        return float.IsFinite(resolved) ? MathF.Max(0, resolved) : 0;
    }

    private void Register(LayoutBox box)
    {
        _primaryBoxes.TryAdd(box.Element, box);
        _paintOrder.Add(box);
    }

    private static bool IsAtomicControl(ElementNode element) => element.TagName is "input" or "button";

    private static IEnumerable<string> TokenizeInlineText(string text)
    {
        var builder = new StringBuilder();
        var whitespace = false;
        foreach (var character in text)
        {
            var currentWhitespace = char.IsWhiteSpace(character);
            if (builder.Length > 0 && currentWhitespace != whitespace)
            {
                yield return builder.ToString();
                builder.Clear();
            }

            whitespace = currentWhitespace;
            builder.Append(character);
        }

        if (builder.Length > 0)
        {
            yield return builder.ToString();
        }
    }

    private static string NormalizeWhitespace(string value) =>
        string.Join(' ', value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));

    private static void ApplyLineAlignment(LayoutBox parent, IReadOnlyList<(int Start, int End, float Width)> lines, float x, float availableWidth, TextAlign align)
    {
        if (align == TextAlign.Start)
        {
            return;
        }

        foreach (var line in lines)
        {
            var offset = align == TextAlign.Center
                ? (availableWidth - line.Width) / 2
                : availableWidth - line.Width;
            if (offset <= 0)
            {
                continue;
            }

            for (var i = line.Start; i < line.End; i++)
            {
                OffsetBox(parent.Children[i], offset, 0);
            }
        }
    }

    private static void OffsetBox(LayoutBox box, float dx, float dy)
    {
        box.MarginBox = Offset(box.MarginBox, dx, dy);
        box.BorderBox = Offset(box.BorderBox, dx, dy);
        box.PaddingBox = Offset(box.PaddingBox, dx, dy);
        box.ContentBox = Offset(box.ContentBox, dx, dy);
        foreach (var child in box.Children)
        {
            OffsetBox(child, dx, dy);
        }
    }

    private static RectF Offset(RectF rect, float dx, float dy) =>
        new(rect.X + dx, rect.Y + dy, rect.Width, rect.Height);

    private sealed record InlineItem(ElementNode Element, ComputedStyle Style, string? Text, bool IsAtomic, bool IsLineBreak)
    {
        public static InlineItem TextItem(ElementNode element, ComputedStyle style, string text) => new(element, style, text, false, false);
        public static InlineItem Atomic(ElementNode element, ComputedStyle style) => new(element, style, null, true, false);
        public static InlineItem LineBreak(ElementNode element, ComputedStyle style) => new(element, style, null, false, true);
    }
}
