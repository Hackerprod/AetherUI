using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;

namespace AetherUI.Rendering;

public sealed class DisplayListBuilder
{
    private readonly ITextMeasurer _textMeasurer;
    private readonly List<PaintCommand> _commands = [];

    public DisplayListBuilder(ITextMeasurer textMeasurer)
    {
        _textMeasurer = textMeasurer ?? throw new ArgumentNullException(nameof(textMeasurer));
    }

    public DisplayList Build(LayoutResult layout, ElementNode? focusedElement = null)
    {
        ArgumentNullException.ThrowIfNull(layout);
        _commands.Clear();
        PaintBox(layout.Root, focusedElement);
        return new DisplayList(_commands.ToArray());
    }

    private void PaintBox(LayoutBox box, ElementNode? focusedElement)
    {
        var style = box.Style;
        if (!style.Visible || style.Display == DisplayKind.None || box.BorderBox.IsEmpty)
        {
            return;
        }

        if (box.Kind == LayoutBoxKind.Text)
        {
            if (!string.IsNullOrEmpty(box.Text))
            {
                _commands.Add(new DrawTextCommand(
                    box.Text,
                    box.ContentBox,
                    style.FontFamily,
                    style.FontSize,
                    style.FontWeight,
                    TextAlign.Start,
                    ApplyOpacity(style.Color, style.Opacity)));
            }

            return;
        }

        PaintBackgroundAndBorder(box);
        var clips = style.Overflow == OverflowKind.Hidden;
        if (clips)
        {
            _commands.Add(new PushClipCommand(box.PaddingBox));
        }

        if (box.IsAtomicControl)
        {
            PaintControl(box, focusedElement);
        }
        else
        {
            foreach (var child in box.Children)
            {
                PaintBox(child, focusedElement);
            }
        }

        if (clips)
        {
            _commands.Add(new PopClipCommand());
        }

        if (ReferenceEquals(box.Element, focusedElement))
        {
            _commands.Add(new StrokeRoundedRectangleCommand(
                box.BorderBox.Inflate(2),
                style.BorderRadius + 2,
                1,
                new ColorRgba(0.145f, 0.388f, 0.922f, .75f)));
        }
    }

    private void PaintBackgroundAndBorder(LayoutBox box)
    {
        var style = box.Style;
        var background = ApplyOpacity(style.BackgroundColor, style.Opacity);
        if (background.A > 0)
        {
            _commands.Add(new FillRoundedRectangleCommand(box.BorderBox, style.BorderRadius, background));
        }

        var borderWidth = ResolveUniformBorder(style.BorderWidth, box.BorderBox.Width, style.FontSize);
        var borderColor = ApplyOpacity(style.BorderColor, style.Opacity);
        if (borderWidth > 0 && borderColor.A > 0)
        {
            var inset = borderWidth / 2;
            var rect = new RectF(
                box.BorderBox.X + inset,
                box.BorderBox.Y + inset,
                MathF.Max(0, box.BorderBox.Width - borderWidth),
                MathF.Max(0, box.BorderBox.Height - borderWidth));
            _commands.Add(new StrokeRoundedRectangleCommand(rect, MathF.Max(0, style.BorderRadius - inset), borderWidth, borderColor));
        }
    }

    private void PaintControl(LayoutBox box, ElementNode? focusedElement)
    {
        var element = box.Element;
        var style = box.Style;
        if (element.TagName == "button")
        {
            var text = NormalizeWhitespace(element.TextContent);
            _commands.Add(new DrawTextCommand(
                text,
                box.ContentBox,
                style.FontFamily,
                style.FontSize,
                style.FontWeight,
                TextAlign.Center,
                ApplyOpacity(style.Color, style.Opacity)));
            return;
        }

        var value = element.RuntimeValue ?? element.GetAttribute("value") ?? string.Empty;
        var placeholder = element.GetAttribute("placeholder") ?? string.Empty;
        var displayed = value.Length > 0 ? value : placeholder;
        var color = value.Length > 0
            ? style.Color
            : new ColorRgba(.42f, .45f, .5f, 1);
        var textRect = new RectF(
            box.ContentBox.X,
            box.ContentBox.Y,
            box.ContentBox.Width,
            box.ContentBox.Height);
        _commands.Add(new DrawTextCommand(
            displayed,
            textRect,
            style.FontFamily,
            style.FontSize,
            style.FontWeight,
            TextAlign.Start,
            ApplyOpacity(color, style.Opacity)));

        if (ReferenceEquals(element, focusedElement))
        {
            var metrics = _textMeasurer.MeasureText(value, style, box.ContentBox.Width);
            var caretX = MathF.Min(box.ContentBox.Right - 1, box.ContentBox.X + metrics.Width + 1);
            var caretTop = box.ContentBox.Y + MathF.Max(1, (box.ContentBox.Height - style.FontSize * 1.15f) / 2);
            var caretHeight = MathF.Min(box.ContentBox.Height - 2, style.FontSize * 1.15f);
            _commands.Add(new FillRoundedRectangleCommand(
                new RectF(caretX, caretTop, 1, MathF.Max(1, caretHeight)),
                0,
                ApplyOpacity(style.Color, style.Opacity)));
        }
    }

    private static float ResolveUniformBorder(CssEdges edges, float reference, float fontSize)
    {
        static float Resolve(CssLength value, float referenceValue, float size)
        {
            var resolved = value.Resolve(referenceValue, size, size, referenceValue, referenceValue);
            return float.IsFinite(resolved) ? MathF.Max(0, resolved) : 0;
        }

        return new[]
        {
            Resolve(edges.Left, reference, fontSize),
            Resolve(edges.Top, reference, fontSize),
            Resolve(edges.Right, reference, fontSize),
            Resolve(edges.Bottom, reference, fontSize),
        }.Max();
    }

    private static ColorRgba ApplyOpacity(ColorRgba color, float opacity) =>
        color.WithAlpha(color.A * Math.Clamp(opacity, 0, 1));

    private static string NormalizeWhitespace(string value) =>
        string.Join(' ', value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
}
