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

    public LayoutEngine(ITextMeasurer textMeasurer)
    {
        _textMeasurer = textMeasurer ?? throw new ArgumentNullException(nameof(textMeasurer));
    }

    public LayoutResult Layout(DocumentNode document, StyleMap styles, SizeF viewport)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(styles);
        if (viewport.Width <= 0 || viewport.Height <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(viewport), "El viewport debe tener dimensiones positivas.");
        }

        _styles = styles;
        _viewport = viewport;
        _primaryBoxes.Clear();
        _paintOrder.Clear();

        var rootElement = document.Body ?? document.DocumentElement
            ?? throw new InvalidOperationException("El documento no contiene un elemento renderizable.");
        _rootFontSize = styles[rootElement].FontSize;

        var root = LayoutElement(rootElement, new RectF(0, 0, viewport.Width, viewport.Height), 0, forceAvailableWidth: true);
        return new LayoutResult(root, new Dictionary<ElementNode, LayoutBox>(_primaryBoxes), _paintOrder.ToArray(), viewport);
    }

    private LayoutBox LayoutElement(ElementNode element, RectF containingBlock, float normalFlowY, bool forceAvailableWidth = false)
    {
        var style = _styles[element];
        var box = new LayoutBox(LayoutBoxKind.Element, element, style);
        Register(box);

        if (style.Display == DisplayKind.None || !style.Visible)
        {
            return box;
        }

        var margin = ResolveEdges(style.Margin, containingBlock.Width, style);
        var padding = ResolveEdges(style.Padding, containingBlock.Width, style);
        var border = ResolveEdges(style.BorderWidth, containingBlock.Width, style);

        var availableBorderWidth = MathF.Max(0, containingBlock.Width - margin.Horizontal);
        var specifiedWidth = Resolve(style.Width, containingBlock.Width, style);
        var borderWidth = specifiedWidth;

        if (!float.IsFinite(borderWidth))
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

        var explicitHeight = Resolve(style.Height, containingBlock.Height, style);
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
        var naturalHeight = style.FontSize * style.LineHeight;
        if (float.IsFinite(explicitHeight))
        {
            naturalHeight = style.BoxSizing == BoxSizing.ContentBox
                ? explicitHeight
                : MathF.Max(0, explicitHeight - padding.Vertical - border.Vertical);
        }

        return MathF.Max(naturalHeight, style.FontSize * 1.1f);
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
            return 0;
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

        var rowBoxes = new List<LayoutBox>(children.Length);
        var totalWidth = 0f;
        var maxHeight = 0f;
        foreach (var child in children)
        {
            var childBox = LayoutElement(child, new RectF(content.X, content.Y, content.Width, content.Height), content.Y);
            rowBoxes.Add(childBox);
            totalWidth += childBox.MarginBox.Width;
            maxHeight = MathF.Max(maxHeight, childBox.MarginBox.Height);
        }

        totalWidth += style.Gap * Math.Max(0, rowBoxes.Count - 1);
        var free = MathF.Max(0, content.Width - totalWidth);
        var spacing = style.Gap;
        var cursorX = style.JustifyContent switch
        {
            JustifyContent.Center => content.X + free / 2,
            JustifyContent.End => content.X + free,
            _ => content.X,
        };
        if (style.JustifyContent == JustifyContent.SpaceBetween && rowBoxes.Count > 1)
        {
            spacing += free / (rowBoxes.Count - 1);
        }

        var crossExtent = hasDefiniteHeight ? content.Height : maxHeight;
        foreach (var childBox in rowBoxes)
        {
            var y = style.AlignItems switch
            {
                AlignItems.Center => content.Y + (crossExtent - childBox.MarginBox.Height) / 2,
                AlignItems.End => content.Y + crossExtent - childBox.MarginBox.Height,
                _ => content.Y,
            };
            OffsetBox(childBox, cursorX - childBox.MarginBox.X, y - childBox.MarginBox.Y);
            parentBox.AddChild(childBox);
            cursorX += childBox.MarginBox.Width + spacing;
        }

        return hasDefiniteHeight ? MathF.Max(maxHeight, content.Height) : maxHeight;
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
        var childMargin = ResolveEdges(childStyle.Margin, maximum, childStyle);
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
