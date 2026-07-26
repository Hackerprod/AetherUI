using AetherUI.Dom;
using AetherUI.Primitives;
using AetherUI.StyleRuntime;

namespace AetherUI.Css;

/// <summary>
/// Converts AetherUI.StyleRuntime's typed <see cref="AetherComputedStyle"/> (parsing, cascade,
/// selectors, inheritance, custom properties, media queries) into this engine's own
/// <see cref="ComputedStyle"/>, so <c>LayoutEngine</c> and <c>DisplayListBuilder</c> need no changes.
/// </summary>
/// <remarks>
/// StyleRuntime's model is richer than what this engine's layout/paint code can act on yet
/// (per-axis flex wrap/basis/order, <c>position</c>, per-side border style/color, box-shadow,
/// transforms). Those values are parsed correctly by StyleRuntime but have no field to land in
/// here — they are intentionally dropped at this boundary, not silently mis-approximated. Growing
/// this bridge (and the matching <see cref="ComputedStyle"/> fields) is how those capabilities get
/// wired up later, one family at a time, without touching parsing/cascade/selectors again.
/// </remarks>
public static class StyleRuntimeBridge
{
    public static ComputedStyle ToComputedStyle(AetherComputedStyle source, ElementNode element)
    {
        var gradient = ToLinearGradient(source.Paint.BackgroundImage);
        // -webkit-background-clip: text means the gradient paints the text's fill, not the box's —
        // painting it as a box fill too would be wrong, so it's one or the other, never both.
        var clipToText = source.Paint.BackgroundClipText && gradient is not null;

        var style = new ComputedStyle
        {
            Display = element.TagName == "br" ? DisplayKind.LineBreak : ToDisplayKind(source.Layout.Display),
            FlexDirection = ToFlexDirection(source.Flex.Direction),
            JustifyContent = ToJustifyContent(source.Flex.JustifyContent),
            AlignItems = ToAlignItems(source.Flex.AlignItems),
            FlexGrow = MathF.Max(0, source.Flex.Grow),
            FlexWrap = source.Flex.Wrap != FlexWrapMode.NoWrap,
            // This engine models a single main-axis Gap. row-gap is vertical spacing (relevant to
            // a Column main axis); column-gap is horizontal spacing (relevant to a Row main axis).
            Gap = MathF.Max(0, ResolveOrZero(
                source.Flex.Direction is FlexDirectionMode.Column or FlexDirectionMode.ColumnReverse
                    ? source.Flex.RowGap
                    : source.Flex.ColumnGap)),

            Width = ToCssLength(source.Layout.Width),
            Height = ToCssLength(source.Layout.Height),
            MinWidth = ToCssLength(source.Layout.MinWidth),
            MinHeight = ToCssLength(source.Layout.MinHeight),
            MaxWidth = ToCssLength(source.Layout.MaxWidth),
            MaxHeight = ToCssLength(source.Layout.MaxHeight),
            Margin = ToCssEdges(source.Layout.Margin),
            Padding = ToCssEdges(source.Layout.Padding),
            BorderWidth = ToCssEdges(source.Paint.BorderWidth),
            BoxSizing = source.Layout.BoxSizing == BoxSizingMode.BorderBox ? BoxSizing.BorderBox : BoxSizing.ContentBox,

            Color = ToColorRgba(source.Text.Color),
            // The "background" shorthand also writes background-color as a plain-fill fallback
            // for the gradient's first stop (for a consumer that can't paint gradients at all) —
            // but when the gradient is clipped to text, painting that fallback as a solid BOX fill
            // would be wrong: only the text should show any of this color.
            BackgroundColor = clipToText ? ColorRgba.Transparent : ToColorRgba(source.Paint.BackgroundColor),
            BackgroundGradient = clipToText ? null : gradient,
            TextGradient = clipToText ? gradient : null,
            BorderColor = ToColorRgba(source.Paint.BorderColor.Top),
            BorderRadius = ToCssLength(source.Paint.BorderRadius.TopLeft.Horizontal),
            Opacity = Math.Clamp(source.Paint.Opacity, 0f, 1f),

            FontFamily = source.Text.FontFamily,
            FontWeight = (FontWeight)source.Text.FontWeight,
            TextAlign = ToTextAlign(source.Text.TextAlign),

            // This engine only implements vertical scrolling, so overflow-y is what decides
            // clip/scroll-container behavior here (overflow-x is parsed but has no effect yet).
            Overflow = ToOverflowKind(source.Layout.OverflowY),
            Cursor = ToCursorKind(source.Interaction.Cursor),
            Visible = source.Layout.Visibility == VisibilityMode.Visible,
        };

        // FontSize must land before LineHeight, since an em/percent line-height resolves against it.
        style.FontSize = MathF.Max(1f, ResolveOrZero(source.Text.FontSize, fontSize: 16f));
        style.LineHeight = ToLineHeightMultiplier(source.Text.LineHeight, style.FontSize);
        style.BoxShadows = ToBoxShadows(source.Paint.BoxShadows, style.FontSize);

        ReportUnrenderableCapabilities(source, style.BoxShadows.Count);
        return style;
    }

    /// <summary>
    /// StyleRuntime understands more of CSS than this engine's layout/paint code can act on yet.
    /// Those values parse and cascade correctly but have no field on <see cref="ComputedStyle"/>
    /// to land in — flag that explicitly instead of letting them disappear silently at this
    /// boundary, the same way an unknown property or an unparseable value already are.
    /// </summary>
    private static void ReportUnrenderableCapabilities(AetherComputedStyle source, int renderedShadowCount)
    {
        // Outer box-shadows are painted (see ToBoxShadows); inset ones are parsed but still
        // dropped here — inset shadows need clipping to the box interior, not implemented yet.
        if (source.Paint.BoxShadows.Count > renderedShadowCount)
        {
            CssDiagnostics.ReportUnknownProperty("box-shadow", "<inset, not yet painted>");
        }

        if (source.Layout.Position != PositionMode.Static)
        {
            CssDiagnostics.ReportUnknownProperty("position", source.Layout.Position.ToString());
        }

        if (source.Transform.Operations.Count > 0)
        {
            CssDiagnostics.ReportUnknownProperty("transform", "<parsed, not yet composited>");
        }
    }

    private static DisplayKind ToDisplayKind(DisplayMode mode) => mode switch
    {
        DisplayMode.None => DisplayKind.None,
        DisplayMode.Block => DisplayKind.Block,
        DisplayMode.InlineBlock => DisplayKind.InlineBlock,
        DisplayMode.Flex => DisplayKind.Flex,
        _ => DisplayKind.Inline,
    };

    private static FlexDirection ToFlexDirection(FlexDirectionMode mode) =>
        mode is FlexDirectionMode.Column or FlexDirectionMode.ColumnReverse ? FlexDirection.Column : FlexDirection.Row;

    private static JustifyContent ToJustifyContent(JustifyContentMode mode) => mode switch
    {
        JustifyContentMode.Center => JustifyContent.Center,
        JustifyContentMode.End => JustifyContent.End,
        // SpaceAround/SpaceEvenly are parsed correctly but LayoutEngine only implements the
        // SpaceBetween distribution algorithm today; this is the closest supported behavior.
        JustifyContentMode.SpaceBetween or JustifyContentMode.SpaceAround or JustifyContentMode.SpaceEvenly => JustifyContent.SpaceBetween,
        _ => JustifyContent.Start,
    };

    private static AlignItems ToAlignItems(AlignItemsMode mode) => mode switch
    {
        AlignItemsMode.Center => AlignItems.Center,
        AlignItemsMode.End => AlignItems.End,
        AlignItemsMode.Start or AlignItemsMode.Baseline => AlignItems.Start,
        _ => AlignItems.Stretch,
    };

    private static TextAlign ToTextAlign(TextAlignMode mode) => mode switch
    {
        TextAlignMode.Center => TextAlign.Center,
        TextAlignMode.End or TextAlignMode.Right => TextAlign.End,
        _ => TextAlign.Start,
    };

    private static OverflowKind ToOverflowKind(OverflowMode mode) => mode switch
    {
        OverflowMode.Hidden or OverflowMode.Clip => OverflowKind.Hidden,
        OverflowMode.Auto => OverflowKind.Auto,
        OverflowMode.Scroll => OverflowKind.Scroll,
        _ => OverflowKind.Visible,
    };

    private static CursorKind ToCursorKind(CursorMode mode) => mode switch
    {
        CursorMode.Pointer => CursorKind.Pointer,
        CursorMode.Text => CursorKind.Text,
        _ => CursorKind.Default,
    };

    private static ColorRgba ToColorRgba(StyleColor color) => new(color.RedF, color.GreenF, color.BlueF, color.AlphaF);

    /// <summary>
    /// Only non-inset shadows are resolved here — an inset shadow needs clipping to the box's
    /// interior to look right (it should never spill past the box's own edges), which this engine
    /// doesn't implement yet, so it's left out and separately reported as unrenderable.
    /// </summary>
    private static IReadOnlyList<BoxShadow> ToBoxShadows(IReadOnlyList<StyleShadow> shadows, float fontSize)
    {
        if (shadows.Count == 0)
        {
            return [];
        }

        var resolved = new List<BoxShadow>(shadows.Count);
        foreach (var shadow in shadows)
        {
            if (shadow.Inset)
            {
                continue;
            }

            resolved.Add(new BoxShadow(
                ResolveOrZero(shadow.OffsetX, fontSize: fontSize),
                ResolveOrZero(shadow.OffsetY, fontSize: fontSize),
                MathF.Max(0, ResolveOrZero(shadow.BlurRadius, fontSize: fontSize)),
                ResolveOrZero(shadow.SpreadRadius, fontSize: fontSize),
                ToColorRgba(shadow.Color),
                Inset: false));
        }

        return resolved;
    }

    private static LinearGradient? ToLinearGradient(StyleLinearGradient? gradient)
    {
        if (gradient is not { } value || value.Stops.Count < 2)
        {
            return null;
        }

        var stops = new GradientStop[value.Stops.Count];
        for (var i = 0; i < value.Stops.Count; i++)
        {
            stops[i] = new GradientStop(ToColorRgba(value.Stops[i].Color), Math.Clamp(value.Stops[i].Offset, 0f, 1f));
        }

        return new LinearGradient(value.DirectionX, value.DirectionY, stops);
    }

    private static CssEdges ToCssEdges(StyleEdges<StyleLength> edges) => new(
        ToCssLength(edges.Left),
        ToCssLength(edges.Top),
        ToCssLength(edges.Right),
        ToCssLength(edges.Bottom));

    private static CssLength ToCssLength(StyleLength length)
    {
        if (length.Kind != StyleLengthKind.Linear)
        {
            // Auto/None/MinContent/MaxContent/FitContent: this engine has no distinct concept for
            // any of these yet, so they all fall back to its own auto-sizing behavior.
            return CssLength.Auto;
        }

        var nonZero = 0;
        if (length.Pixels != 0f) nonZero++;
        if (length.Percent != 0f) nonZero++;
        if (length.Em != 0f) nonZero++;
        if (length.Rem != 0f) nonZero++;
        if (length.ViewportWidth != 0f) nonZero++;
        if (length.ViewportHeight != 0f) nonZero++;

        if (nonZero == 0)
        {
            return CssLength.Zero;
        }

        if (nonZero > 1)
        {
            // A genuine calc() mix of units: preserve every term instead of collapsing it.
            return CssLength.Linear(length.Pixels, length.Percent, length.Em, length.Rem, length.ViewportWidth, length.ViewportHeight);
        }

        if (length.Percent != 0f) return new CssLength(length.Percent, CssUnit.Percent);
        if (length.Em != 0f) return new CssLength(length.Em, CssUnit.Em);
        if (length.Rem != 0f) return new CssLength(length.Rem, CssUnit.Rem);
        if (length.ViewportWidth != 0f) return new CssLength(length.ViewportWidth, CssUnit.ViewportWidth);
        if (length.ViewportHeight != 0f) return new CssLength(length.ViewportHeight, CssUnit.ViewportHeight);
        return new CssLength(length.Pixels, CssUnit.Pixels);
    }

    private static float ResolveOrZero(StyleLength length, float reference = 0f, float fontSize = 0f, float rootFontSize = 16f) =>
        length.Kind == StyleLengthKind.Linear
            ? length.Resolve(new StyleLengthResolveContext(reference, fontSize, rootFontSize, 0, 0))
            : 0f;

    private static float ToLineHeightMultiplier(StyleLength length, float fontSizePixels)
    {
        if (length.Kind != StyleLengthKind.Linear || fontSizePixels <= 0f)
        {
            return 1.25f;
        }

        // Percent line-height (like em) is relative to the element's own font-size.
        var resolved = length.Resolve(new StyleLengthResolveContext(fontSizePixels, fontSizePixels, fontSizePixels, 0, 0));
        return resolved > 0f ? resolved / fontSizePixels : 1.25f;
    }
}
