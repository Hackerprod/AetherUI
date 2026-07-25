namespace AetherUI.StyleRuntime;

internal static class BuiltinStyleProperties
{
    private static readonly IReadOnlyDictionary<string, DisplayMode> DisplayValues =
        Map(
            ("none", DisplayMode.None),
            ("block", DisplayMode.Block),
            ("inline", DisplayMode.Inline),
            ("inline-block", DisplayMode.InlineBlock),
            ("flex", DisplayMode.Flex));

    private static readonly IReadOnlyDictionary<string, PositionMode> PositionValues =
        Map(
            ("static", PositionMode.Static),
            ("relative", PositionMode.Relative),
            ("absolute", PositionMode.Absolute),
            ("fixed", PositionMode.Fixed));

    private static readonly IReadOnlyDictionary<string, BoxSizingMode> BoxSizingValues =
        Map(("content-box", BoxSizingMode.ContentBox), ("border-box", BoxSizingMode.BorderBox));

    private static readonly IReadOnlyDictionary<string, OverflowMode> OverflowValues =
        Map(
            ("visible", OverflowMode.Visible),
            ("hidden", OverflowMode.Hidden),
            ("clip", OverflowMode.Clip),
            ("auto", OverflowMode.Auto),
            ("scroll", OverflowMode.Scroll));

    private static readonly IReadOnlyDictionary<string, VisibilityMode> VisibilityValues =
        Map(
            ("visible", VisibilityMode.Visible),
            ("hidden", VisibilityMode.Hidden),
            ("collapse", VisibilityMode.Collapse));

    private static readonly IReadOnlyDictionary<string, FlexDirectionMode> FlexDirectionValues =
        Map(
            ("row", FlexDirectionMode.Row),
            ("row-reverse", FlexDirectionMode.RowReverse),
            ("column", FlexDirectionMode.Column),
            ("column-reverse", FlexDirectionMode.ColumnReverse));

    private static readonly IReadOnlyDictionary<string, FlexWrapMode> FlexWrapValues =
        Map(
            ("nowrap", FlexWrapMode.NoWrap),
            ("wrap", FlexWrapMode.Wrap),
            ("wrap-reverse", FlexWrapMode.WrapReverse));

    private static readonly IReadOnlyDictionary<string, JustifyContentMode> JustifyValues =
        Map(
            ("start", JustifyContentMode.Start),
            ("flex-start", JustifyContentMode.Start),
            ("end", JustifyContentMode.End),
            ("flex-end", JustifyContentMode.End),
            ("center", JustifyContentMode.Center),
            ("space-between", JustifyContentMode.SpaceBetween),
            ("space-around", JustifyContentMode.SpaceAround),
            ("space-evenly", JustifyContentMode.SpaceEvenly));

    private static readonly IReadOnlyDictionary<string, AlignItemsMode> AlignItemsValues =
        Map(
            ("stretch", AlignItemsMode.Stretch),
            ("start", AlignItemsMode.Start),
            ("flex-start", AlignItemsMode.Start),
            ("end", AlignItemsMode.End),
            ("flex-end", AlignItemsMode.End),
            ("center", AlignItemsMode.Center),
            ("baseline", AlignItemsMode.Baseline));

    private static readonly IReadOnlyDictionary<string, AlignSelfMode> AlignSelfValues =
        Map(
            ("auto", AlignSelfMode.Auto),
            ("stretch", AlignSelfMode.Stretch),
            ("start", AlignSelfMode.Start),
            ("flex-start", AlignSelfMode.Start),
            ("end", AlignSelfMode.End),
            ("flex-end", AlignSelfMode.End),
            ("center", AlignSelfMode.Center),
            ("baseline", AlignSelfMode.Baseline));

    private static readonly IReadOnlyDictionary<string, AlignContentMode> AlignContentValues =
        Map(
            ("stretch", AlignContentMode.Stretch),
            ("start", AlignContentMode.Start),
            ("flex-start", AlignContentMode.Start),
            ("end", AlignContentMode.End),
            ("flex-end", AlignContentMode.End),
            ("center", AlignContentMode.Center),
            ("space-between", AlignContentMode.SpaceBetween),
            ("space-around", AlignContentMode.SpaceAround),
            ("space-evenly", AlignContentMode.SpaceEvenly));

    private static readonly IReadOnlyDictionary<string, TextAlignMode> TextAlignValues =
        Map(
            ("start", TextAlignMode.Start),
            ("end", TextAlignMode.End),
            ("left", TextAlignMode.Left),
            ("right", TextAlignMode.Right),
            ("center", TextAlignMode.Center),
            ("justify", TextAlignMode.Justify));

    private static readonly IReadOnlyDictionary<string, FontStyleMode> FontStyleValues =
        Map(
            ("normal", FontStyleMode.Normal),
            ("italic", FontStyleMode.Italic),
            ("oblique", FontStyleMode.Oblique));

    private static readonly IReadOnlyDictionary<string, WhiteSpaceMode> WhiteSpaceValues =
        Map(
            ("normal", WhiteSpaceMode.Normal),
            ("nowrap", WhiteSpaceMode.NoWrap),
            ("pre", WhiteSpaceMode.Pre),
            ("pre-wrap", WhiteSpaceMode.PreWrap),
            ("pre-line", WhiteSpaceMode.PreLine));

    private static readonly IReadOnlyDictionary<string, CursorMode> CursorValues =
        Map(
            ("default", CursorMode.Default),
            ("pointer", CursorMode.Pointer),
            ("text", CursorMode.Text),
            ("move", CursorMode.Move),
            ("crosshair", CursorMode.Crosshair),
            ("wait", CursorMode.Wait),
            ("help", CursorMode.Help),
            ("not-allowed", CursorMode.NotAllowed),
            ("grab", CursorMode.Grab),
            ("grabbing", CursorMode.Grabbing),
            ("ew-resize", CursorMode.ResizeHorizontal),
            ("col-resize", CursorMode.ResizeHorizontal),
            ("ns-resize", CursorMode.ResizeVertical),
            ("row-resize", CursorMode.ResizeVertical));

    private static readonly IReadOnlyDictionary<string, PointerEventsMode> PointerEventsValues =
        Map(("auto", PointerEventsMode.Auto), ("none", PointerEventsMode.None));

    internal static void Register(StylePropertyRegistry registry)
    {
        RegisterLayout(registry);
        RegisterFlex(registry);
        RegisterText(registry);
        RegisterPaint(registry);
        RegisterInteraction(registry);
        RegisterTransforms(registry);
        RegisterShorthands(registry);
    }

    private static void RegisterLayout(StylePropertyRegistry registry)
    {
        registry.Register("display", false, StyleImpact.RebuildVisual | StyleImpact.Layout | StyleImpact.Paint | StyleImpact.HitTest,
            DisplayMode.Inline, Parse(DisplayValues), (b, v) => b.Display = v, s => s.Layout.Display);
        registry.Register("position", false, StyleImpact.Layout | StyleImpact.Paint | StyleImpact.HitTest,
            PositionMode.Static, Parse(PositionValues), (b, v) => b.Position = v, s => s.Layout.Position);
        registry.Register("visibility", true, StyleImpact.Paint | StyleImpact.HitTest,
            VisibilityMode.Visible, Parse(VisibilityValues), (b, v) => b.Visibility = v, s => s.Layout.Visibility);
        registry.Register("box-sizing", false, StyleImpact.Layout,
            BoxSizingMode.ContentBox, Parse(BoxSizingValues), (b, v) => b.BoxSizing = v, s => s.Layout.BoxSizing);
        registry.Register("overflow-x", false, StyleImpact.Layout | StyleImpact.Paint | StyleImpact.HitTest,
            OverflowMode.Visible, Parse(OverflowValues), (b, v) => b.OverflowX = v, s => s.Layout.OverflowX);
        registry.Register("overflow-y", false, StyleImpact.Layout | StyleImpact.Paint | StyleImpact.HitTest,
            OverflowMode.Visible, Parse(OverflowValues), (b, v) => b.OverflowY = v, s => s.Layout.OverflowY);

        RegisterLength(registry, "width", StyleLength.Auto, StyleValueParsers.TrySize,
            (b, v) => b.Width = v, s => s.Layout.Width);
        RegisterLength(registry, "height", StyleLength.Auto, StyleValueParsers.TrySize,
            (b, v) => b.Height = v, s => s.Layout.Height);
        RegisterLength(registry, "min-width", StyleLength.Zero, StyleValueParsers.TryNonNegativeLength,
            (b, v) => b.MinWidth = v, s => s.Layout.MinWidth);
        RegisterLength(registry, "min-height", StyleLength.Zero, StyleValueParsers.TryNonNegativeLength,
            (b, v) => b.MinHeight = v, s => s.Layout.MinHeight);
        RegisterLength(registry, "max-width", StyleLength.None, StyleValueParsers.TryMaxSize,
            (b, v) => b.MaxWidth = v, s => s.Layout.MaxWidth);
        RegisterLength(registry, "max-height", StyleLength.None, StyleValueParsers.TryMaxSize,
            (b, v) => b.MaxHeight = v, s => s.Layout.MaxHeight);

        RegisterEdgeLength(registry, "margin-top", StyleLength.Zero, StyleValueParsers.TryLengthOrAuto,
            (e, v) => e with { Top = v }, s => s.Layout.Margin.Top, (b, e) => b.Margin = e, b => b.Margin);
        RegisterEdgeLength(registry, "margin-right", StyleLength.Zero, StyleValueParsers.TryLengthOrAuto,
            (e, v) => e with { Right = v }, s => s.Layout.Margin.Right, (b, e) => b.Margin = e, b => b.Margin);
        RegisterEdgeLength(registry, "margin-bottom", StyleLength.Zero, StyleValueParsers.TryLengthOrAuto,
            (e, v) => e with { Bottom = v }, s => s.Layout.Margin.Bottom, (b, e) => b.Margin = e, b => b.Margin);
        RegisterEdgeLength(registry, "margin-left", StyleLength.Zero, StyleValueParsers.TryLengthOrAuto,
            (e, v) => e with { Left = v }, s => s.Layout.Margin.Left, (b, e) => b.Margin = e, b => b.Margin);

        RegisterEdgeLength(registry, "padding-top", StyleLength.Zero, StyleValueParsers.TryNonNegativeLength,
            (e, v) => e with { Top = v }, s => s.Layout.Padding.Top, (b, e) => b.Padding = e, b => b.Padding);
        RegisterEdgeLength(registry, "padding-right", StyleLength.Zero, StyleValueParsers.TryNonNegativeLength,
            (e, v) => e with { Right = v }, s => s.Layout.Padding.Right, (b, e) => b.Padding = e, b => b.Padding);
        RegisterEdgeLength(registry, "padding-bottom", StyleLength.Zero, StyleValueParsers.TryNonNegativeLength,
            (e, v) => e with { Bottom = v }, s => s.Layout.Padding.Bottom, (b, e) => b.Padding = e, b => b.Padding);
        RegisterEdgeLength(registry, "padding-left", StyleLength.Zero, StyleValueParsers.TryNonNegativeLength,
            (e, v) => e with { Left = v }, s => s.Layout.Padding.Left, (b, e) => b.Padding = e, b => b.Padding);

        RegisterEdgeLength(registry, "top", StyleLength.Auto, StyleValueParsers.TryLengthOrAuto,
            (e, v) => e with { Top = v }, s => s.Layout.Inset.Top, (b, e) => b.Inset = e, b => b.Inset);
        RegisterEdgeLength(registry, "right", StyleLength.Auto, StyleValueParsers.TryLengthOrAuto,
            (e, v) => e with { Right = v }, s => s.Layout.Inset.Right, (b, e) => b.Inset = e, b => b.Inset);
        RegisterEdgeLength(registry, "bottom", StyleLength.Auto, StyleValueParsers.TryLengthOrAuto,
            (e, v) => e with { Bottom = v }, s => s.Layout.Inset.Bottom, (b, e) => b.Inset = e, b => b.Inset);
        RegisterEdgeLength(registry, "left", StyleLength.Auto, StyleValueParsers.TryLengthOrAuto,
            (e, v) => e with { Left = v }, s => s.Layout.Inset.Left, (b, e) => b.Inset = e, b => b.Inset);

        registry.Register("z-index", false, StyleImpact.Paint | StyleImpact.HitTest,
            (int?)null, StyleValueParsers.TryNullableInteger, (b, v) => b.ZIndex = v, s => s.Layout.ZIndex);
    }

    private static void RegisterFlex(StylePropertyRegistry registry)
    {
        registry.Register("flex-direction", false, StyleImpact.Layout,
            FlexDirectionMode.Row, Parse(FlexDirectionValues), (b, v) => b.FlexDirection = v, s => s.Flex.Direction);
        registry.Register("flex-wrap", false, StyleImpact.Layout,
            FlexWrapMode.NoWrap, Parse(FlexWrapValues), (b, v) => b.FlexWrap = v, s => s.Flex.Wrap);
        registry.Register("flex-grow", false, StyleImpact.Layout,
            0f, StyleValueParsers.TryNonNegativeNumber, (b, v) => b.FlexGrow = v, s => s.Flex.Grow);
        registry.Register("flex-shrink", false, StyleImpact.Layout,
            1f, StyleValueParsers.TryNonNegativeNumber, (b, v) => b.FlexShrink = v, s => s.Flex.Shrink);
        registry.Register("flex-basis", false, StyleImpact.Layout,
            StyleLength.Auto, StyleValueParsers.TrySize, (b, v) => b.FlexBasis = v, s => s.Flex.Basis);
        registry.Register("justify-content", false, StyleImpact.Layout,
            JustifyContentMode.Start, Parse(JustifyValues), (b, v) => b.JustifyContent = v, s => s.Flex.JustifyContent);
        registry.Register("align-items", false, StyleImpact.Layout,
            AlignItemsMode.Stretch, Parse(AlignItemsValues), (b, v) => b.AlignItems = v, s => s.Flex.AlignItems);
        registry.Register("align-self", false, StyleImpact.Layout,
            AlignSelfMode.Auto, Parse(AlignSelfValues), (b, v) => b.AlignSelf = v, s => s.Flex.AlignSelf);
        registry.Register("align-content", false, StyleImpact.Layout,
            AlignContentMode.Stretch, Parse(AlignContentValues), (b, v) => b.AlignContent = v, s => s.Flex.AlignContent);
        registry.Register("row-gap", false, StyleImpact.Layout,
            StyleLength.Zero, StyleValueParsers.TryNonNegativeLength, (b, v) => b.RowGap = v, s => s.Flex.RowGap);
        registry.Register("column-gap", false, StyleImpact.Layout,
            StyleLength.Zero, StyleValueParsers.TryNonNegativeLength, (b, v) => b.ColumnGap = v, s => s.Flex.ColumnGap);
        registry.Register("order", false, StyleImpact.Layout,
            0, StyleValueParsers.TryInteger, (b, v) => b.Order = v, s => s.Flex.Order);
    }

    private static void RegisterText(StylePropertyRegistry registry)
    {
        registry.Register("color", true, StyleImpact.Paint,
            StyleColor.Black, StyleValueParsers.TryColor, (b, v) => b.Color = v, s => s.Text.Color);

        // -webkit-text-fill-color: null means "not set" (real default is currentcolor, i.e. Color).
        StyleValueParser<StyleColor?> tryTextFillColor = static (string source, out StyleColor? value) =>
        {
            if (StyleValueParsers.TryColor(source, out var color))
            {
                value = color;
                return true;
            }

            value = null;
            return false;
        };
        registry.Register("-webkit-text-fill-color", true, StyleImpact.Paint,
            (StyleColor?)null, tryTextFillColor, (b, v) => b.TextFillColor = v, s => s.Text.TextFillColor);
        registry.Register("text-fill-color", true, StyleImpact.Paint,
            (StyleColor?)null, tryTextFillColor, (b, v) => b.TextFillColor = v, s => s.Text.TextFillColor);
        registry.Register("font-family", true, StyleImpact.TextLayout | StyleImpact.Paint,
            "Segoe UI", StyleValueParsers.TryFontFamily, (b, v) => b.FontFamily = v, s => s.Text.FontFamily);
        registry.Register("font-size", true, StyleImpact.TextLayout | StyleImpact.Layout | StyleImpact.Paint,
            StyleLength.PixelsValue(16f), StyleValueParsers.TryFontSize, (b, v) => b.FontSize = v, s => s.Text.FontSize);
        registry.Register("font-weight", true, StyleImpact.TextLayout | StyleImpact.Paint,
            (ushort)400, StyleValueParsers.TryFontWeight, (b, v) => b.FontWeight = v, s => s.Text.FontWeight);
        registry.Register("font-style", true, StyleImpact.TextLayout | StyleImpact.Paint,
            FontStyleMode.Normal, Parse(FontStyleValues), (b, v) => b.FontStyle = v, s => s.Text.FontStyle);
        registry.Register("line-height", true, StyleImpact.TextLayout | StyleImpact.Layout,
            StyleLength.EmValue(1.2f), StyleValueParsers.TryLineHeight, (b, v) => b.LineHeight = v, s => s.Text.LineHeight);
        registry.Register("text-align", true, StyleImpact.TextLayout | StyleImpact.Paint,
            TextAlignMode.Start, Parse(TextAlignValues), (b, v) => b.TextAlign = v, s => s.Text.TextAlign);
        registry.Register("white-space", true, StyleImpact.TextLayout | StyleImpact.Layout,
            WhiteSpaceMode.Normal, Parse(WhiteSpaceValues), (b, v) => b.WhiteSpace = v, s => s.Text.WhiteSpace);
        registry.Register("letter-spacing", true, StyleImpact.TextLayout | StyleImpact.Layout,
            StyleLength.Zero, ParseLetterSpacing, (b, v) => b.LetterSpacing = v, s => s.Text.LetterSpacing);
    }

    private static void RegisterPaint(StylePropertyRegistry registry)
    {
        registry.Register("background-color", false, StyleImpact.Paint,
            StyleColor.Transparent, StyleValueParsers.TryColor, (b, v) => b.BackgroundColor = v, s => s.Paint.BackgroundColor);
        registry.Register("background-image", false, StyleImpact.Paint,
            (StyleLinearGradient?)null,
            static (string source, out StyleLinearGradient? value) =>
            {
                if (StyleValueParsers.TryLinearGradient(source, out var gradient))
                {
                    value = gradient;
                    return true;
                }

                value = null;
                return false;
            },
            (b, v) => b.BackgroundImage = v, s => s.Paint.BackgroundImage);
        registry.Register("opacity", false, StyleImpact.Composite | StyleImpact.HitTest,
            1f, StyleValueParsers.TryOpacity, (b, v) => b.Opacity = v, s => s.Paint.Opacity);

        StyleValueParser<bool> tryBackgroundClipText = static (string source, out bool value) =>
        {
            value = string.Equals(source.Trim(), "text", StringComparison.OrdinalIgnoreCase);
            return true;
        };
        registry.Register("-webkit-background-clip", false, StyleImpact.Paint,
            false, tryBackgroundClipText, (b, v) => b.BackgroundClipText = v, s => s.Paint.BackgroundClipText);
        registry.Register("background-clip", false, StyleImpact.Paint,
            false, tryBackgroundClipText, (b, v) => b.BackgroundClipText = v, s => s.Paint.BackgroundClipText);

        RegisterBorderWidth(registry, "border-top-width", (e, v) => e with { Top = v }, s => s.Paint.BorderWidth.Top);
        RegisterBorderWidth(registry, "border-right-width", (e, v) => e with { Right = v }, s => s.Paint.BorderWidth.Right);
        RegisterBorderWidth(registry, "border-bottom-width", (e, v) => e with { Bottom = v }, s => s.Paint.BorderWidth.Bottom);
        RegisterBorderWidth(registry, "border-left-width", (e, v) => e with { Left = v }, s => s.Paint.BorderWidth.Left);

        RegisterBorderStyle(registry, "border-top-style", (e, v) => e with { Top = v }, s => s.Paint.BorderStyle.Top);
        RegisterBorderStyle(registry, "border-right-style", (e, v) => e with { Right = v }, s => s.Paint.BorderStyle.Right);
        RegisterBorderStyle(registry, "border-bottom-style", (e, v) => e with { Bottom = v }, s => s.Paint.BorderStyle.Bottom);
        RegisterBorderStyle(registry, "border-left-style", (e, v) => e with { Left = v }, s => s.Paint.BorderStyle.Left);

        RegisterBorderColor(registry, "border-top-color", (e, v) => e with { Top = v }, s => s.Paint.BorderColor.Top);
        RegisterBorderColor(registry, "border-right-color", (e, v) => e with { Right = v }, s => s.Paint.BorderColor.Right);
        RegisterBorderColor(registry, "border-bottom-color", (e, v) => e with { Bottom = v }, s => s.Paint.BorderColor.Bottom);
        RegisterBorderColor(registry, "border-left-color", (e, v) => e with { Left = v }, s => s.Paint.BorderColor.Left);

        registry.Register("border-radius", false, StyleImpact.Paint | StyleImpact.HitTest,
            StyleCornerRadii.Zero, StyleValueParsers.TryCornerRadii,
            (b, v) => b.BorderRadius = v, s => s.Paint.BorderRadius);
        registry.Register("box-shadow", false, StyleImpact.Paint,
            (IReadOnlyList<StyleShadow>)Array.Empty<StyleShadow>(), StyleValueParsers.TryBoxShadows,
            (b, v) => b.BoxShadows = v, s => s.Paint.BoxShadows);
    }

    private static void RegisterInteraction(StylePropertyRegistry registry)
    {
        registry.Register("cursor", true, StyleImpact.Cursor,
            CursorMode.Default, Parse(CursorValues), (b, v) => b.Cursor = v, s => s.Interaction.Cursor);
        registry.Register("pointer-events", true, StyleImpact.HitTest,
            PointerEventsMode.Auto, Parse(PointerEventsValues), (b, v) => b.PointerEvents = v, s => s.Interaction.PointerEvents);
    }

    private static void RegisterTransforms(StylePropertyRegistry registry)
    {
        registry.Register("transform", false, StyleImpact.Composite | StyleImpact.HitTest,
            (IReadOnlyList<StyleTransformOperation>)Array.Empty<StyleTransformOperation>(), StyleValueParsers.TryTransform,
            (b, v) => b.TransformOperations = v, s => s.Transform.Operations);
        registry.Register("transform-origin", false, StyleImpact.Composite | StyleImpact.HitTest,
            (X: StyleLength.PercentValue(50f), Y: StyleLength.PercentValue(50f)), StyleValueParsers.TryTransformOrigin,
            (b, v) => { b.TransformOriginX = v.X; b.TransformOriginY = v.Y; },
            s => (X: s.Transform.OriginX, Y: s.Transform.OriginY));
    }

    private static void RegisterShorthands(StylePropertyRegistry registry)
    {
        RegisterBoxShorthand(registry, "margin", new[] { "margin-top", "margin-right", "margin-bottom", "margin-left" });
        RegisterBoxShorthand(registry, "padding", new[] { "padding-top", "padding-right", "padding-bottom", "padding-left" });
        RegisterBoxShorthand(registry, "inset", new[] { "top", "right", "bottom", "left" });
        RegisterBoxShorthand(registry, "border-width", new[] { "border-top-width", "border-right-width", "border-bottom-width", "border-left-width" });
        RegisterBoxShorthand(registry, "border-style", new[] { "border-top-style", "border-right-style", "border-bottom-style", "border-left-style" });
        RegisterBoxShorthand(registry, "border-color", new[] { "border-top-color", "border-right-color", "border-bottom-color", "border-left-color" });

        registry.RegisterShorthand("overflow", static (source, writer) =>
        {
            if (IsGlobalKeyword(source))
            {
                writer.Set("overflow-x", source);
                writer.Set("overflow-y", source);
                return true;
            }

            var parts = TextSplitter.SplitWhitespaceTopLevel(source);
            if (parts.Count is < 1 or > 2 ||
                !StyleValueParsers.TryKeyword(parts[0], OverflowValues, out _) ||
                parts.Count == 2 && !StyleValueParsers.TryKeyword(parts[1], OverflowValues, out _))
            {
                return false;
            }

            writer.Set("overflow-x", parts[0]);
            writer.Set("overflow-y", parts.Count == 2 ? parts[1] : parts[0]);
            return true;
        });

        registry.RegisterShorthand("gap", static (source, writer) =>
        {
            if (IsGlobalKeyword(source))
            {
                writer.Set("row-gap", source);
                writer.Set("column-gap", source);
                return true;
            }

            var parts = TextSplitter.SplitWhitespaceTopLevel(source);
            if (parts.Count is < 1 or > 2 ||
                !StyleValueParsers.TryNonNegativeLength(parts[0], out _) ||
                parts.Count == 2 && !StyleValueParsers.TryNonNegativeLength(parts[1], out _))
            {
                return false;
            }

            writer.Set("row-gap", parts[0]);
            writer.Set("column-gap", parts.Count == 2 ? parts[1] : parts[0]);
            return true;
        });

        registry.RegisterShorthand("background", static (source, writer) =>
        {
            if (IsGlobalKeyword(source))
            {
                writer.Set("background-color", source);
                return true;
            }

            if (StyleValueParsers.TryLinearGradient(source, out var gradient))
            {
                // A solid fallback color (the gradient's first stop) is written too, so a
                // consumer that only paints background-color still gets a plausible base color.
                writer.Set("background-image", source);
                if (gradient.Stops.Count > 0)
                {
                    var first = gradient.Stops[0].Color;
                    writer.Set("background-color", $"rgba({first.Red}, {first.Green}, {first.Blue}, {first.AlphaF.ToString(System.Globalization.CultureInfo.InvariantCulture)})");
                }

                return true;
            }

            if (!StyleValueParsers.TryColor(source, out _))
            {
                return false;
            }

            writer.Set("background-color", source);
            return true;
        });

        registry.RegisterShorthand("border", ExpandBorder);
        registry.RegisterShorthand("flex", ExpandFlex);
    }

    private static bool ExpandBorder(string source, StyleShorthandWriter writer)
    {
        if (IsGlobalKeyword(source))
        {
            foreach (var side in new[] { "top", "right", "bottom", "left" })
            {
                writer.Set($"border-{side}-width", source);
                writer.Set($"border-{side}-style", source);
                writer.Set($"border-{side}-color", source);
            }

            return true;
        }

        var parts = TextSplitter.SplitWhitespaceTopLevel(source);
        if (parts.Count is < 1 or > 3)
        {
            return false;
        }

        var width = "medium";
        var style = "none";
        var color = "currentColor";
        var widthSet = false;
        var styleSet = false;
        var colorSet = false;
        foreach (var part in parts)
        {
            if (!widthSet && StyleValueParsers.TryBorderWidth(part, out _))
            {
                width = part;
                widthSet = true;
            }
            else if (!styleSet && StyleValueParsers.TryBorderStyle(part, out _))
            {
                style = part;
                styleSet = true;
            }
            else if (!colorSet && StyleValueParsers.TryColor(part, out _))
            {
                color = part;
                colorSet = true;
            }
            else
            {
                return false;
            }
        }

        foreach (var side in new[] { "top", "right", "bottom", "left" })
        {
            writer.Set($"border-{side}-width", width);
            writer.Set($"border-{side}-style", style);
            writer.Set($"border-{side}-color", color);
        }

        return true;
    }

    private static bool ExpandFlex(string source, StyleShorthandWriter writer)
    {
        var trimmed = source.Trim();
        if (IsGlobalKeyword(trimmed))
        {
            writer.Set("flex-grow", trimmed);
            writer.Set("flex-shrink", trimmed);
            writer.Set("flex-basis", trimmed);
            return true;
        }
        if (trimmed.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            writer.Set("flex-grow", "0");
            writer.Set("flex-shrink", "0");
            writer.Set("flex-basis", "auto");
            return true;
        }

        if (trimmed.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            writer.Set("flex-grow", "1");
            writer.Set("flex-shrink", "1");
            writer.Set("flex-basis", "auto");
            return true;
        }

        if (trimmed.Equals("initial", StringComparison.OrdinalIgnoreCase))
        {
            writer.Set("flex-grow", "0");
            writer.Set("flex-shrink", "1");
            writer.Set("flex-basis", "auto");
            return true;
        }

        var parts = TextSplitter.SplitWhitespaceTopLevel(trimmed);
        if (parts.Count is < 1 or > 3 || !StyleValueParsers.TryNonNegativeNumber(parts[0], out _))
        {
            return false;
        }

        var grow = parts[0];
        var shrink = "1";
        var basis = "0px";
        if (parts.Count >= 2)
        {
            if (StyleValueParsers.TryNonNegativeNumber(parts[1], out _))
            {
                shrink = parts[1];
                if (parts.Count == 3)
                {
                    basis = parts[2];
                }
            }
            else
            {
                basis = parts[1];
                if (parts.Count == 3)
                {
                    return false;
                }
            }
        }

        if (!StyleValueParsers.TrySize(basis, out _))
        {
            return false;
        }

        writer.Set("flex-grow", grow);
        writer.Set("flex-shrink", shrink);
        writer.Set("flex-basis", basis);
        return true;
    }


    private static bool IsGlobalKeyword(string source)
    {
        var value = source.Trim();
        return value.Equals("initial", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("inherit", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("unset", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("revert", StringComparison.OrdinalIgnoreCase);
    }

    private static void RegisterBoxShorthand(StylePropertyRegistry registry, string name, IReadOnlyList<string> targets)
    {
        registry.RegisterShorthand(name, (source, writer) =>
        {
            var parts = TextSplitter.SplitWhitespaceTopLevel(source);
            if (parts.Count is < 1 or > 4)
            {
                return false;
            }

            var expanded = parts.Count switch
            {
                1 => new[] { parts[0], parts[0], parts[0], parts[0] },
                2 => new[] { parts[0], parts[1], parts[0], parts[1] },
                3 => new[] { parts[0], parts[1], parts[2], parts[1] },
                4 => new[] { parts[0], parts[1], parts[2], parts[3] },
                _ => throw new InvalidOperationException(),
            };

            for (var index = 0; index < targets.Count; index++)
            {
                writer.Set(targets[index], expanded[index]);
            }

            return true;
        });
    }

    private static void RegisterLength(
        StylePropertyRegistry registry,
        string name,
        StyleLength initial,
        StyleValueParser<StyleLength> parser,
        Action<AetherStyleBuilder, StyleLength> apply,
        Func<AetherComputedStyle, StyleLength> read)
    {
        registry.Register(name, false, StyleImpact.Layout | StyleImpact.Paint | StyleImpact.HitTest,
            initial, parser, apply, read);
    }

    private static void RegisterEdgeLength(
        StylePropertyRegistry registry,
        string name,
        StyleLength initial,
        StyleValueParser<StyleLength> parser,
        Func<StyleEdges<StyleLength>, StyleLength, StyleEdges<StyleLength>> replace,
        Func<AetherComputedStyle, StyleLength> read,
        Action<AetherStyleBuilder, StyleEdges<StyleLength>> assign,
        Func<AetherStyleBuilder, StyleEdges<StyleLength>> current)
    {
        registry.Register(name, false, StyleImpact.Layout | StyleImpact.Paint | StyleImpact.HitTest,
            initial, parser, (builder, value) => assign(builder, replace(current(builder), value)), read);
    }

    private static void RegisterBorderWidth(
        StylePropertyRegistry registry,
        string name,
        Func<StyleEdges<StyleLength>, StyleLength, StyleEdges<StyleLength>> replace,
        Func<AetherComputedStyle, StyleLength> read)
    {
        registry.Register(name, false, StyleImpact.Layout | StyleImpact.Paint | StyleImpact.HitTest,
            StyleLength.Zero, StyleValueParsers.TryBorderWidth,
            (builder, value) => builder.BorderWidth = replace(builder.BorderWidth, value), read);
    }

    private static void RegisterBorderStyle(
        StylePropertyRegistry registry,
        string name,
        Func<StyleEdges<BorderLineStyle>, BorderLineStyle, StyleEdges<BorderLineStyle>> replace,
        Func<AetherComputedStyle, BorderLineStyle> read)
    {
        registry.Register(name, false, StyleImpact.Layout | StyleImpact.Paint,
            BorderLineStyle.None, StyleValueParsers.TryBorderStyle,
            (builder, value) => builder.BorderStyle = replace(builder.BorderStyle, value), read);
    }

    private static void RegisterBorderColor(
        StylePropertyRegistry registry,
        string name,
        Func<StyleEdges<StyleColor>, StyleColor, StyleEdges<StyleColor>> replace,
        Func<AetherComputedStyle, StyleColor> read)
    {
        registry.Register(name, false, StyleImpact.Paint,
            StyleColor.CurrentColor, StyleValueParsers.TryColor,
            (builder, value) => builder.BorderColor = replace(builder.BorderColor, value), read);
    }

    private static bool ParseLetterSpacing(string source, out StyleLength value)
    {
        if (source.Trim().Equals("normal", StringComparison.OrdinalIgnoreCase))
        {
            value = StyleLength.Zero;
            return true;
        }

        return StyleValueParsers.TryLength(source, out value);
    }

    private static StyleValueParser<T> Parse<T>(IReadOnlyDictionary<string, T> values) =>
        (string source, out T value) => StyleValueParsers.TryKeyword(source, values, out value);

    private static IReadOnlyDictionary<string, T> Map<T>(params (string Name, T Value)[] values) =>
        values.ToDictionary(static item => item.Name, static item => item.Value, StringComparer.OrdinalIgnoreCase);
}
