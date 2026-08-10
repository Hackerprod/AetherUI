using System.Net;
using AetherUI.Css;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Generator;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;
using AetherUI.Windows.Resources;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;
using System.Diagnostics;
using System.Security.Cryptography;

namespace AetherUI.Tests;

internal static class Program
{
    private static readonly (string Name, Action Test)[] Tests =
    [
        ("HTML parser", HtmlParserBuildsDocument),
        ("CSS cascade and pseudo classes", CssCascadeUsesState),
        ("Layout and hit testing", LayoutProducesHitTargets),
        ("Safe generated markup and event dispatch", MarkupIsEncodedAndEventsDispatch),
        ("Required form validation", RequiredInputBlocksSubmit),
        (".cshtml compiler", CshtmlCompilerGeneratesCSharp),
        ("Modern .view.html compiler", ModernViewCompilerGeneratesCSharp),
        ("StyleRuntime integration: negative margin + diagnostics", StyleRuntimeHandlesNegativeMarginAndReportsGaps),
        ("Gradient text via -webkit-background-clip", GradientTextClipAppliesToTextRun),
        ("Native ABI contract rejects incompatible runtime metadata", NativeAbiContractRejectsIncompatibleMetadata),
        ("Native style runtime replaces DOM on markup rebuild", NativeStyleRuntimeRebuildReplacesDom),
        ("Native style runtime paints body shorthand gradients", NativeStyleRuntimePaintsBodyShorthandGradient),
        ("Native style runtime emits body solid background", NativeStyleRuntimeEmitsBodySolidBackground),
        ("Native style runtime emits nested element background", NativeStyleRuntimeEmitsNestedElementBackground),
        ("Native box-shadow lists preserve declaration and paint order", NativeBoxShadowListsPreservePaintOrder),
        ("Native text-shadow inherits and emits flat paint commands", NativeTextShadowInheritsAndEmitsPaintCommands),
        ("Native backdrop-filter emits ordered declarative commands", NativeBackdropFilterEmitsOrderedDeclarativeCommands),
        ("Native transform cascade preserves typed 2D operations", NativeTransformCascadePreservesTypedOperations),
        ("Native rotated border transform matches its border-box origin", NativeRotatedBorderTransformMatchesBorderBoxOrigin),
        ("Native absolute flex static positions center ring boxes", NativeAbsoluteFlexStaticPositionsCenterRingBoxes),
        ("Native pipeline renders dashboard fixture", NativePipelineRendersDashboardFixture),
        ("Native flex layout preserves nested content width", NativeFlexLayoutPreservesNestedContentWidth),
        ("Native layout positions absolute children and anonymous flex text", NativeLayoutPositionsAbsoluteChildrenAndAnonymousFlexText),
        ("Native text alignment preserves CSS semantics and control overrides", NativeTextAlignmentPreservesSemanticsAndControlOverrides),
        ("Native runtime renders generated pseudo-elements", NativeRuntimeRendersGeneratedPseudoElements),
        ("Native CSS typography, side borders, and column flex grow", NativeCssTypographySideBordersAndColumnFlexGrow),
        ("Native root scrolls overflowing content", NativeRootScrollsOverflowingContent),
        ("Native visible overflow does not become a scroll container", NativeVisibleOverflowDoesNotBecomeScrollContainer),
        ("Page scrolls by default without an overflow rule", PageScrollsByDefaultWithoutOverflowRule),
        ("Div with hidden overflow is a programmatic scroll container", DivHiddenOverflowIsProgrammaticScrollContainer),
        ("Div with clip overflow is never a scroll container", DivClipIsNeverScrollContainer),
        ("Overflow scroll always shows chrome", OverflowScrollAlwaysShowsChrome),
        ("One axis visible computes auto on the other axis", OneAxisVisibleComputesAuto),
        ("Native scroll metrics are bidirectional and resize-aware", NativeScrollMetricsAreBidirectionalAndResizeAware),
        ("Native scrollbar chrome supports two-axis pointer interaction and chaining", NativeScrollbarChromeSupportsTwoAxisInteractionAndChaining),
        ("Native hover paint impact skips layout", NativeHoverPaintImpactSkipsLayout),
        ("Native catalog hover paint impact skips layout", NativeCatalogHoverPaintImpactSkipsLayout),
        ("Native nested hover keeps ancestors active", NativeNestedHoverKeepsAncestorsActive),
        ("Native catalog hover storm stays incremental", NativeCatalogHoverStormStaysIncremental),
        ("Native scroll after hover patch rebuilds canonical display list", NativeScrollAfterHoverPatchRebuildsCanonicalDisplayList),
        ("Native hover coalesces pointer moves before paint", NativeHoverCoalescesPointerMovesBeforePaint),
        ("Native pointer-events hover updates hit testing without layout", NativePointerEventsHoverUpdatesHitTestingWithoutLayout),
        ("Native layout disposes before borrowed style map", NativeLayoutDisposesBeforeBorrowedStyleMap),
        ("Image resolver deduplicates resources and reports lifecycle", ImageResolverDeduplicatesResourcesAndReportsLifecycle),
        ("Native image geometry rebuilds when a resource becomes ready", NativeImageGeometryRebuildsWhenResourceReady),
        ("Image command survives hover re-render with resolved geometry", ImageCommandSurvivesHoverRerender),
        ("Native table layout shares automatic column widths across rows", NativeTableLayoutSharesAutomaticColumns),
        ("UA form controls have no min-width floor (square round button)", UaFormControlsHaveNoMinWidthFloor),
        ("UA stylesheet comes from native", UaStylesheetComesFromNative),
        ("Button text centers vertically in a definite height", ButtonTextVerticallyCentersInDefiniteHeight),
        ("Body min-height 100vh centers flex card and anchors absolute bottom circle", BodyMinHeightVhCentersCardAndAnchorsCircle),
        ("Shorthand with CSS variable resolves to gradient", ShorthandWithCssVariableResolvesToGradient),
        ("Parent opacity applies to descendant paint", ParentOpacityAppliesToDescendantPaint),
        ("ModalController toggles overlay and keeps backdrop passive", ModalControllerTogglesOverlayAndKeepsBackdropPassive),
        ("Fixed-size button renders square with centered text", FixedSizeButtonRendersSquare),
        ("Close glyph centered in definite box even when line overflows", CloseGlyphCenteredInDefiniteBox),
        ("ModalController theme overrides via CSS variables", ModalControllerThemeViaCssVariables),
        ("Style binding inside raw-text emits literal", StyleBindingInsideRawTextEmitsLiteral),
        ("Pseudo ::after text uses its own font-size, not the host's", PseudoAfterTextUsesOwnFontSize),
        ("File image loader decodes a file PNG", FileImageResourceLoader_DecodesFilePng),
        ("File image loader rejects http policy", FileImageResourceLoader_RejectsHttp),
        ("Data image loader decodes a base64 data URI", DataImageResourceLoader_DecodesDataUri),
        ("Http image loader rejects file policy", HttpImageResourceLoader_RejectsFilePolicy),
        ("Composite image loader falls through schemes", CompositeImageResourceLoader_FallsThroughSchemes),
        ("Native image decode rejects garbage bytes", NativeImageDecode_RejectsGarbage),
        ("Native z-index reorders paint above DOM order", ZIndexReordersPaintAboveDomOrder),
        ("Opacity animates on state-triggered class change", TransitionsAnimateOpacityOnStateChange),
        ("No transition property means no animation", TransitionsDoNotAnimateWithoutTransitionProperty),
    ];

    public static int Main()
    {
        var failures = 0;
        foreach (var (name, test) in Tests)
        {
            try
            {
                test();
                Console.WriteLine($"PASS  {name}");
            }
            catch (Exception exception)
            {
                failures++;
                Console.Error.WriteLine($"FAIL  {name}\n      {exception.Message}");
            }
        }

        Console.WriteLine($"\n{Tests.Length - failures}/{Tests.Length} pruebas superadas.");
        return failures == 0 ? 0 : 1;
    }

    private static void HtmlParserBuildsDocument()
    {
        var document = HtmlParser.Parse("<body><form id='f'><label>Nombre</label><input id='n' required></form></body>");
        Assert.NotNull(document.Body);
        Assert.Equal("form", document.GetElementById("f")?.TagName);
        Assert.True(document.GetElementById("n")?.IsRequired == true);
        Assert.Equal("Nombre", document.QuerySelector("label")?.TextContent);
    }

    private static void CssCascadeUsesState()
    {
        var component = new CascadeTestComponent();
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();
        var button = Assert.NotNull(host.Document?.GetElementById("send"));
        var normal = Assert.NotNull(host.Layout?.FindPrimaryBox(button));
        Assert.True(normal.Style.Color.R > normal.Style.Color.B);

        var center = new PointF(normal.BorderBox.X + normal.BorderBox.Width / 2, normal.BorderBox.Y + normal.BorderBox.Height / 2);
        host.PointerMoved(center);
        host.Render();

        var hovered = Assert.NotNull(host.Layout?.FindPrimaryBox(button));
        Assert.True(hovered.Style.Color.B > hovered.Style.Color.R);
    }

    private static void LayoutProducesHitTargets()
    {
        var component = new LayoutTestComponent();
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();
        var button = Assert.NotNull(host.Document?.GetElementById("send"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(button));
        var center = new PointF(box.BorderBox.X + box.BorderBox.Width / 2, box.BorderBox.Y + box.BorderBox.Height / 2);
        Assert.Same(button, host.Layout!.HitTest(center));
    }

    private static void NativeTableLayoutSharesAutomaticColumns()
    {
        const string markup = """
            <style>
              body { margin:0; width:300px; box-sizing:border-box; }
              table { width:300px; box-sizing:border-box; }
              td, th { padding:5px; border:1px solid #000; box-sizing:border-box; }
            </style>
            <body><table id="table"><thead><tr id="header"><th id="h1">Invoice</th><th id="h2">Customer</th></tr></thead>
            <tbody><tr id="detail"><td id="d1">Alexander</td><td id="d2">Date</td></tr></tbody></table></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 200, 1);
        host.Render();

        LayoutBox Box(string id) => Assert.NotNull(host.Layout?.FindPrimaryBox(
            Assert.NotNull(host.Document?.GetElementById(id))));
        var table = Box("table");
        var h1 = Box("h1");
        var h2 = Box("h2");
        var d1 = Box("d1");
        var d2 = Box("d2");

        Assert.Equal(h1.BorderBox.X, table.ContentBox.X);
        Assert.Equal(d1.BorderBox.X, table.ContentBox.X);
        Assert.Equal(h2.BorderBox.X, h1.BorderBox.Right);
        Assert.Equal(d2.BorderBox.X, d1.BorderBox.Right);
        Assert.Equal(h1.BorderBox.Width, d1.BorderBox.Width);
        Assert.Equal(h2.BorderBox.Width, d2.BorderBox.Width);
        Assert.Equal(table.ContentBox.Width, h1.BorderBox.Width + h2.BorderBox.Width);
        Assert.True(h1.BorderBox.Width > h2.BorderBox.Width,
            "The longer first-column contribution should receive more of an automatic table's width.");
    }

    private static void ModalControllerTogglesOverlayAndKeepsBackdropPassive()
    {
        var component = new ModalPageComponent();
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        // Closed: the fixed overlay is pointer-events:none, so the trigger button behind it
        // still receives the hit test, and no overlay/card fills are visible.
        var openButton = Assert.NotNull(host.Document?.GetElementById("open"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(openButton));
        var center = new PointF(box.BorderBox.X + box.BorderBox.Width / 2, box.BorderBox.Y + box.BorderBox.Height / 2);
        Assert.Same(openButton, host.Layout!.HitTest(center));
        Assert.True(renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
            .All(c => c.Color.A < .01f || c.Rectangle.Width <= 120f) == true,
            "closed modal must not paint the dark overlay or the card");

        // Click the trigger -> the controller opens (Changed -> StateHasChanged -> re-render).
        host.PointerPressed(center);
        host.PointerReleased(center);
        Assert.Equal(1, component.ClickCount);
        Assert.True(component.Modal.IsOpen);
        host.Render();

        var overlayFill = Assert.NotNull(renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
            .FirstOrDefault(c => c.Color.A > .4f && c.Rectangle.Width > 400f));
        Assert.True(overlayFill.Rectangle.Width >= 640f && overlayFill.Rectangle.Height >= 480f,
            "open modal must paint the fixed overlay over the viewport");
        var cardFill = Assert.NotNull(renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
            .FirstOrDefault(c => c.Color.A > .99f && c.Rectangle.Width > 300f && c.Rectangle.Width < 600f));
        Assert.True(cardFill.Rectangle.Width < 600f, "the card must be narrower than the overlay");

        // Close -> hidden again on the next render.
        component.Modal.Close();
        host.Render();
        Assert.False(component.Modal.IsOpen);
        Assert.True(renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
            .All(c => c.Color.A < .01f || c.Rectangle.Width <= 120f) == true,
            "closed modal must not paint the dark overlay or the card after Close");
    }

    private static void FixedSizeButtonRendersSquare()
    {
        // A page that sizes its close button explicitly (standard UI practice) must get a square
        // box with the glyph centered — the same 22x22 circle bug class (fixed width must win).
        const string markup = """
            <style>body { margin: 0; } .close-btn { width: 32px; height: 32px; background: none; border: none; font-size: 1.5rem; }</style>
            <body><button class="close-btn" id="c">×</button></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("c"))));
        Assert.True(MathF.Abs(box.BorderBox.Width - 32f) < .01f && MathF.Abs(box.BorderBox.Height - 32f) < .01f,
            $"close button must be square 32x32 (got {box.BorderBox.Width:F1}x{box.BorderBox.Height:F1})");
    }

    private static void CloseGlyphCenteredInDefiniteBox()
    {
        // The 32x32 close button (UA padding shrinks the content box to ~20x27, while the 1.5rem
        // glyph's line box is ~30 tall — it OVERFLOWS). Chromium still centers the glyph: the
        // overflow must distribute symmetrically instead of hugging the top.
        const string markup = """
            <style>body { margin: 0; } .close-btn { width: 32px; height: 32px; font-size: 1.5rem; background: none; border: none; }</style>
            <body><button class="close-btn" id="c">×</button></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var button = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("c"))));
        var glyph = Assert.NotNull(renderer.LastDisplayList?.Commands.OfType<DrawTextCommand>().SingleOrDefault(c => c.Text == "×"));
        var contentCenterX = button.ContentBox.X + button.ContentBox.Width / 2f;
        var contentCenterY = button.ContentBox.Y + button.ContentBox.Height / 2f;
        var glyphCenterX = glyph.Rectangle.X + glyph.Rectangle.Width / 2f;
        var glyphCenterY = glyph.Rectangle.Y + glyph.Rectangle.Height / 2f;
        Assert.True(MathF.Abs(glyphCenterX - contentCenterX) < 1f,
            $"glyph horizontal center {glyphCenterX:F2} vs content center {contentCenterX:F2}");
        Assert.True(MathF.Abs(glyphCenterY - contentCenterY) < 1f,
            $"glyph vertical center {glyphCenterY:F2} vs content center {contentCenterY:F2} (line box overflows the content box)");
    }

    private static void ModalControllerThemeViaCssVariables()
    {
        // The controller emits --aether-modal-* custom properties with defaults; a page theme
        // overrides them in its own stylesheet and the open overlay must use the overridden color.
        // This must survive the full open -> close -> reopen cycle (each render re-emits the
        // controller's <style> and the native re-compiles the sheets — the theme is a cascade
        // override of the inherited custom property, not a one-time patch).
        var component = new ThemedModalPageComponent();
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        void AssertThemedOverlay(string stage)
        {
            var overlayFill = Assert.NotNull(renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
                .FirstOrDefault(c => c.Color.A > .5f && c.Rectangle.Width > 400f));
            Assert.True(overlayFill.Color.R > .8f && overlayFill.Color.B < .2f,
                $"[{stage}] themed overlay must use the page's --aether-modal-overlay-bg override (got {overlayFill.Color})");
        }

        component.OpenModal();
        host.Render();
        AssertThemedOverlay("first open");

        component.Modal.Close();
        host.Render();
        component.OpenModal();
        host.Render();
        AssertThemedOverlay("reopen after close");
    }

    private sealed class ThemedModalPageComponent : UiComponent
    {
        public ModalController Modal { get; } = new();

        public ThemedModalPageComponent()
        {
            Modal.Changed += () => StateHasChanged();
        }

        public void OpenModal() => Modal.Show(w => w.WriteLiteral("<h2>Hola</h2>"));

        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style>:root { --aether-modal-overlay-bg: rgba(255, 0, 0, 0.9); } " +
                "body { margin: 0; }</style><body>");
            Modal.Render(writer);
            writer.WriteLiteral("</body>");
        }
    }

    private sealed class ModalPageComponent : UiComponent
    {
        public ModalController Modal { get; } = new();
        public int ClickCount { get; private set; }

        public ModalPageComponent()
        {
            Modal.Changed += () => StateHasChanged();
        }

        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style>body { margin: 0; } #open { width: 120px; height: 40px; }</style>");
            writer.WriteLiteral("<body><button id=\"open\"");
            writer.WriteEventAttribute("onclick", Open);
            writer.WriteLiteral(">Abrir</button>");
            Modal.Render(writer);
            writer.WriteLiteral("</body>");
        }

        private void Open(UiEvent e)
        {
            ClickCount++;
            Modal.Show(w => w.WriteLiteral("<h2>Hola</h2>"));
        }
    }

    private static void ParentOpacityAppliesToDescendantPaint()
    {
        // Regression: modal.html's overlay hides itself with opacity:0, but the white modal
        // card inside it painted at FULL opacity — CSS opacity applies to the whole subtree.
        // A parent at opacity 0 must make descendants fully transparent too (the builder
        // omits alpha-0 fills entirely, so "hidden" means the card fill is absent).
        const string markup = """
            <style>
              body { margin: 0; }
              .overlay { opacity: 0; }
              .overlay.active { opacity: 1; }
              .card { width: 100px; height: 60px; background: white; }
            </style>
            <body><div class="overlay" id="o"><div class="card" id="c"></div></div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        static FillRoundedRectangleCommand? CardFill(DisplayList? displayList) => displayList?.Commands
            .OfType<FillRoundedRectangleCommand>()
            .SingleOrDefault(c => MathF.Abs(c.Rectangle.Width - 100f) < .01f && MathF.Abs(c.Rectangle.Height - 60f) < .01f);

        Assert.True(CardFill(renderer.LastDisplayList) is null,
            "card inside an opacity:0 overlay must not produce a visible fill");

        // Toggle the overlay to .active through a stateful component and re-render.
        var component = new OverlayToggleComponent();
        using var renderer2 = new RecordingRenderer();
        using var host2 = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer2, new UiHostOptions());
        host2.SetViewport(320, 200, 1);
        host2.Render();
        component.OpenModal();
        host2.Render();
        var shown = Assert.NotNull(CardFill(renderer2.LastDisplayList));
        Assert.True(shown.Color.A > .99f, "card inside an opacity:1 overlay must be opaque");
    }

    private sealed class OverlayToggleComponent : UiComponent
    {
        public bool Open { get; private set; }

        public void OpenModal()
        {
            Open = true;
            StateHasChanged();
        }

        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style>body { margin: 0; } .overlay { opacity: 0; } .overlay.active { opacity: 1; } " +
                ".card { width: 100px; height: 60px; background: white; }</style><body><div class=\"overlay" +
                (Open ? " active" : "") + "\" id=\"o\"><div class=\"card\"></div></div></body>");
        }
    }

    private static void ShorthandWithCssVariableResolvesToGradient()
    {
        // modal.html uses `:root { --bg-gradient: linear-gradient(...) }` + `body { background:
        // var(--bg-gradient) }`. Shorthands whose values contain var() were silently dropped at
        // compile time; the body rendered transparent (gray canvas). They must resolve per node
        // and expand like any other shorthand.
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new BodyVariableGradientComponent(), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var body = Assert.NotNull(host.Document?.Body);
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(body));
        Assert.True(box.Style.BackgroundGradient is { },
            "background: var(--bg-gradient) must resolve to a gradient (shorthand with var() was dropped)");
        var gradient = box.Style.BackgroundGradient.GetValueOrDefault();
        Assert.Equal(2, gradient.Stops.Count);
        Assert.True(gradient.Stops[0].Color.B > gradient.Stops[0].Color.R,
            "the resolved gradient must keep the author's 135deg ramp (#667eea -> #764ba2)");
    }

    private static void BodyMinHeightVhCentersCardAndAnchorsCircle()
    {
        // modal.html: body { min-height: 100vh; display: flex; justify-content: center;
        // align-items: center; position: relative; } with an absolutely positioned bottom
        // circle. Chrome: card vertically centered, circle anchored to the viewport bottom.
        const string markup = """
            <style>
              * { margin: 0; padding: 0; box-sizing: border-box; }
              body { min-height: 100vh; display: flex; justify-content: center; align-items: center;
                     position: relative; overflow: hidden; }
              .circle { position: absolute; border-radius: 50%; background: rgba(255,255,255,.1); }
              .circle-2 { width: 200px; height: 200px; bottom: -50px; right: -50px; }
              .container { position: relative; width: 90%; max-width: 400px; padding: 40px; }
            </style>
            <body>
              <div class="circle circle-2" id="c2"></div>
              <div class="container" id="card"><h1>Diseño Moderno</h1></div>
            </body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var body = Assert.NotNull(host.Layout?.Root);
        var card = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("card"))));
        var circle = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("c2"))));
        // Direct check: the card must be vertically centered in the viewport.
        var viewportCenter = 200f / 2f;
        var cardCenter = card.BorderBox.Y + card.BorderBox.Height / 2f;
        Assert.True(MathF.Abs(cardCenter - viewportCenter) < 2f,
            $"card center {cardCenter:F2} vs viewport center {viewportCenter:F2} (card y={card.BorderBox.Y:F2} h={card.BorderBox.Height:F2}, body h={body.BorderBox.Height:F2}, circle bottom={circle.BorderBox.Bottom:F2})");

        // The absolutely positioned bottom circle must anchor to the viewport bottom.
        // The absolutely positioned bottom circle must anchor to the viewport bottom.
        Assert.True(MathF.Abs(circle.BorderBox.Bottom - (200f + 50f)) < 2f,
            $"circle bottom {circle.BorderBox.Bottom:F2} vs expected 250 (circle y={circle.BorderBox.Y:F2})");
    }

    private static void ButtonTextVerticallyCentersInDefiniteHeight()
    {
        // Regression: the navbar .menu-item buttons have an explicit height (34px). Chrome
        // centers in-flow button text vertically in that definite height; the engine used to
        // hug the line box to the top, shifting the navbar labels upward.
        const string markup = """
            <style>
              body { margin: 0; }
              .navbar { display: flex; flex-direction: row; align-items: center; height: 56px; }
              .menu-item { height: 34px; padding: 0 14px; border: none; font-size: 13px; }
            </style>
            <body><div class="navbar"><button class="menu-item" id="refresh">Refresh</button></div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var button = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("refresh"))));
        var text = Assert.NotNull(renderer.LastDisplayList?.Commands.OfType<DrawTextCommand>().SingleOrDefault(c => c.Text == "Refresh"));
        var textCenter = text.Rectangle.Y + text.Rectangle.Height / 2f;
        var buttonCenter = button.ContentBox.Y + button.ContentBox.Height / 2f;
        Assert.True(MathF.Abs(textCenter - buttonCenter) < 1f,
            $"button text center {textCenter:F2} vs button content center {buttonCenter:F2} (text y={text.Rectangle.Y:F2})");
    }

    private static void UaFormControlsHaveNoMinWidthFloor()
    {
        // Regression: the engine UA stylesheet used to force button { min-width: 84px }
        // and input { min-width: 180px }. Chromium's UA has no such floor, so a fixed
        // author width must win; otherwise a 22px round button renders as an 84x22
        // horizontal ellipse (50% border-radius resolves 42x11).
        const string markup = """
            <style>
              body { margin: 0; }
              .tile-uninstall { position: absolute; top: 2px; right: 2px; width: 22px; height: 22px;
                                 padding: 0; border: none; border-radius: 50%; font-size: 14px; line-height: 1; }
              .holder { position: relative; width: 100px; height: 100px; }
            </style>
            <body>
              <div class="holder"><button class="tile-uninstall" id="x">×</button></div>
              <input id="i" style="width: 150px">
            </body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 200, 1);
        host.Render();

        var button = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("x"))));
        Assert.Equal(22f, button.BorderBox.Width);
        Assert.Equal(22f, button.BorderBox.Height);

        var input = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("i"))));
        Assert.Equal(150f, input.BorderBox.Width);
    }

    private static void UaStylesheetComesFromNative()
    {
        // The engine-defaults sheet used to be the managed EngineDefaultsCss constant; it now
        // lives in the native core and the host re-applies it from
        // aether_style_runtime_defaults_css() on every Load. A bare <button>/<input> with no
        // author rules must still receive the UA widget padding, proving the UA comes from native.
        const string markup = """
            <body>
              <button>OK</button>
              <input>
            </body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 200, 1);
        host.Render();

        var button = Assert.NotNull(host.Document?.GetElementsByTagName("button").SingleOrDefault());
        var buttonBox = Assert.NotNull(host.Layout?.FindPrimaryBox(button));
        Assert.Equal(6f, buttonBox.Style.Padding.Left.Value);
        Assert.Equal(2f, buttonBox.Style.Padding.Top.Value);
        Assert.Equal(6f, buttonBox.Style.Padding.Right.Value);
        Assert.Equal(3f, buttonBox.Style.Padding.Bottom.Value);

        var input = Assert.NotNull(host.Document?.GetElementsByTagName("input").SingleOrDefault());
        var inputBox = Assert.NotNull(host.Layout?.FindPrimaryBox(input));
        Assert.Equal(1f, inputBox.Style.Padding.Top.Value);
        Assert.Equal(1f, inputBox.Style.Padding.Bottom.Value);
        Assert.Equal(0f, inputBox.Style.Padding.Left.Value);
    }

    private static void MarkupIsEncodedAndEventsDispatch()
    {
        var component = new ButtonComponent { Label = "<script>alert(1)</script>" };
        var markup = component.RenderMarkup();
        Assert.False(markup.Contains("<script>", StringComparison.Ordinal));
        Assert.True(markup.Contains(WebUtility.HtmlEncode(component.Label), StringComparison.Ordinal));

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();
        var button = Assert.NotNull(host.Document?.GetElementById("button"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(button));
        var point = new PointF(box.BorderBox.X + 2, box.BorderBox.Y + 2);
        host.PointerPressed(point);
        host.PointerReleased(point);
        Assert.Equal(1, component.ClickCount);
        Assert.True(renderer.RenderCount > 0);
    }

    private static void RequiredInputBlocksSubmit()
    {
        var component = new FormComponent();
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        Click(host, "submit");
        Assert.Equal(0, component.SubmitCount);
        var input = Assert.NotNull(host.Document?.GetElementById("name"));
        Assert.True((input.State & ElementState.Invalid) != 0);

        Click(host, "name");
        host.TextInput('A');
        host.Render();
        Assert.True(renderer.LastDisplayList?.Commands.OfType<DrawTextCommand>()
            .Any(command => command.Text == "A") == true);
        Click(host, "submit");
        Assert.Equal(1, component.SubmitCount);
    }

    private static void StyleRuntimeHandlesNegativeMarginAndReportsGaps()
    {
        var reported = new List<CssStyleDiagnostic>();
        void OnReported(CssStyleDiagnostic diagnostic) => reported.Add(diagnostic);
        CssDiagnostics.Reported += OnReported;
        try
        {
            var component = new NegativeMarginComponent();
            using var renderer = new RecordingRenderer();
            using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
            host.SetViewport(640, 480, 1);
            host.Render();

            var first = Assert.NotNull(host.Document?.GetElementById("a"));
            var second = Assert.NotNull(host.Document?.GetElementById("b"));
            var firstBox = Assert.NotNull(host.Layout?.FindPrimaryBox(first));
            var secondBox = Assert.NotNull(host.Layout?.FindPrimaryBox(second));

            // margin-top: -20px must pull "b" 20px above where normal flow would place it —
            // the old hand-rolled cascade used to clamp negative margins to zero.
            Assert.True(secondBox.BorderBox.Y < firstBox.BorderBox.Y + firstBox.BorderBox.Height);
            Assert.Equal(firstBox.BorderBox.Y + firstBox.BorderBox.Height - 20f, secondBox.BorderBox.Y);

            // An outer box-shadow is a rendered capability now — resolved onto ComputedStyle, not
            // just parsed-and-dropped.
            Assert.Equal(1, firstBox.Style.BoxShadows.Count);
            var shadow = firstBox.Style.BoxShadows[0];
            Assert.Equal(0f, shadow.OffsetX);
            Assert.Equal(2f, shadow.OffsetY);
            Assert.Equal(4f, shadow.BlurRadius);

            // An inset shadow still needs clipping to the box interior (not implemented), so it
            // must be excluded from what's painted and still reported as a gap, not silently dropped.
            var third = Assert.NotNull(host.Document?.GetElementById("c"));
            var thirdBox = Assert.NotNull(host.Layout?.FindPrimaryBox(third));
            Assert.Equal(0, thirdBox.Style.BoxShadows.Count);
            Assert.True(reported.Exists(static d => d.Code == "AUI-CSS-100" && d.Property == "box-shadow"));
        }
        finally
        {
            CssDiagnostics.Reported -= OnReported;
        }
    }

    private static void CshtmlCompilerGeneratesCSharp()
    {
        var view = """
            @namespace Demo.Views
            @class DemoView
            @using System
            @code {
                private string Name { get; set; } = "Ada";
                private void OnClick(UiEvent e) => StateHasChanged();
            }
            <button id="send" value="@Name" onclick="@OnClick">@(Name.ToUpperInvariant())</button>
            """;
        var compiler = new ViewCompiler("Views/DemoView.cshtml", SourceText.From(view), "Demo");
        var result = compiler.Compile();
        Assert.Equal(0, result.Diagnostics.Length);
        var source = Assert.NotNull(result.Source);
        Assert.True(source.Contains("class DemoView", StringComparison.Ordinal));
        Assert.True(source.Contains("writer.WriteAttribute(\"value\", Name)", StringComparison.Ordinal));
        Assert.True(source.Contains("writer.WriteEventAttribute(\"onclick\", OnClick)", StringComparison.Ordinal));
        Assert.True(source.Contains("writer.Write(Name.ToUpperInvariant())", StringComparison.Ordinal));
        AssertGeneratedSourceCompiles(source);
    }

    private static void ModernViewCompilerGeneratesCSharp()
    {
        const string markup = """
            <button onclick="OpenWindow">Open</button>
            <span>Count: {{ Count }}</span>
            <template a:if="{{ ShowDetails }}"><p>{{ Title }}</p></template>
            <template a:for="page in Pages"><button onclick="Open(page)">{{ page }}</button></template>
            """;
        const string style = ".status { color: #38bdf8; }";
        var compiler = new ViewCompiler("Views/Main.view.html", SourceText.From(markup), "Demo", ViewSyntax.Modern, SourceText.From(style));
        var result = compiler.Compile();
        Assert.Equal(0, result.Diagnostics.Length);
        var source = Assert.NotNull(result.Source);
        Assert.True(source.Contains("public sealed partial class Main", StringComparison.Ordinal));
        Assert.True(source.Contains(": global::AetherUI.Runtime.UiComponent", StringComparison.Ordinal));
        Assert.True(source.Contains("writer.WriteLiteral(\"<style>.status", StringComparison.Ordinal));
        Assert.True(source.Contains("writer.WriteEventAttribute(\"onclick\", OpenWindow)", StringComparison.Ordinal));
        Assert.True(source.Contains("writer.WriteEventAttribute(\"onclick\", _ => Open(page))", StringComparison.Ordinal));
        Assert.True(source.Contains("if (ShowDetails)", StringComparison.Ordinal));
        Assert.True(source.Contains("foreach (var page in Pages)", StringComparison.Ordinal));

        const string companion = """
            namespace Demo
            {
                public sealed partial class Main : global::AetherUI.Runtime.UiComponent
                {
                    private int Count => 1;
                    private bool ShowDetails => true;
                    private string Title => "Details";
                    private global::System.Collections.Generic.IEnumerable<string> Pages => new[] { "One" };
                    private void OpenWindow() { }
                    private void Open(string page) { }
                }
            }
            """;
        AssertGeneratedSourceCompiles(source, companion);
    }

    private static void StyleBindingInsideRawTextEmitsLiteral()
    {
        // Regression: a binding inside a <style> element (e.g. the launcher's
        // <style>{{ IconStyles }}</style>) must be emitted verbatim (WriteLiteral).
        // The old path used the HTML-encoding writer.Write, which turned the quotes in
        // url('file:///...') into &#39; — raw-text style content is never HTML-decoded,
        // so the image source arrived corrupted and the icon stayed transparent.
        const string markup = """
            <body>
              <style>#icon { background: url('{{ IconSource }}') center / cover no-repeat; }</style>
              <span>Count: {{ Count }}</span>
            </body>
            """;
        var compiler = new ViewCompiler("Views/Main.view.html", SourceText.From(markup), "Demo", ViewSyntax.Modern, null);
        var result = compiler.Compile();
        Assert.Equal(0, result.Diagnostics.Length);
        var source = Assert.NotNull(result.Source);
        Assert.True(source.Contains("writer.WriteLiteral(IconSource)", StringComparison.Ordinal),
            "a binding inside <style> must be emitted verbatim (WriteLiteral)");
        Assert.True(!source.Contains("writer.Write(IconSource)", StringComparison.Ordinal),
            "the style binding must not use the HTML-encoding Write path");
        Assert.True(source.Contains("writer.Write(Count)", StringComparison.Ordinal),
            "a binding in normal text content must still use the HTML-encoding Write path");

        const string companion = """
            namespace Demo
            {
                public sealed partial class Main : global::AetherUI.Runtime.UiComponent
                {
                    private string IconSource => "file:///C:/icons/app.png";
                    private int Count => 1;
                }
            }
            """;
        AssertGeneratedSourceCompiles(source, companion);
    }

    private static void PseudoAfterTextUsesOwnFontSize()
    {
        // Regression: weather_widget's degree symbol comes from .temp-main::after with
        // font-size:40px inside an 80px host. The text fragment's element was the HOST
        // (style_origin), so the paint re-resolved the host's font-size (80) — the symbol
        // rendered BIGGER and out of place. The fragment must use the pseudo's own style.
        const string markup = """
            <style>
              body { margin: 0; }
              .temp-main { font-size: 80px; font-weight: 800; margin: 20px 0; position: relative; }
              .temp-main::after { content: "°"; position: absolute; font-size: 40px; top: 10px; }
            </style>
            <body><div class="temp-main">24</div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var commands = renderer.LastDisplayList?.Commands.OfType<DrawTextCommand>().ToArray()
            ?? throw new InvalidOperationException("no text commands");
        var digits = Assert.NotNull(commands.SingleOrDefault(c => c.Text == "24"));
        var degree = Assert.NotNull(commands.SingleOrDefault(c => c.Text == "\u00B0"));
        Assert.True(degree.FontSize < digits.FontSize,
            $"the ::after degree must use its own 40px font, not the host's {digits.FontSize}px (got {degree.FontSize}px)");
        Assert.True(degree.Rectangle.X >= digits.Rectangle.Right - 1f,
            "the absolute degree must sit to the right of the digits");
        Assert.True(degree.Rectangle.Top > digits.Rectangle.Top,
            $"the degree must honor top:10 (offset from the box top) — got top={degree.Rectangle.Top} vs digits top={digits.Rectangle.Top}");
    }

    private static void AssertGeneratedSourceCompiles(string source, string? companionSource = null)
    {
        var trustedAssemblies = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string
            ?? throw new InvalidOperationException("No se pudo obtener TRUSTED_PLATFORM_ASSEMBLIES.");
        var references = trustedAssemblies
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
            .Select(static path => MetadataReference.CreateFromFile(path))
            .ToList();
        references.Add(MetadataReference.CreateFromFile(typeof(UiComponent).Assembly.Location));

        var syntaxTrees = new List<SyntaxTree>
        {
            CSharpSyntaxTree.ParseText(
                source,
                new CSharpParseOptions(LanguageVersion.Latest),
                path: "DemoView.AetherUI.g.cs"),
        };
        if (companionSource is not null)
        {
            syntaxTrees.Add(CSharpSyntaxTree.ParseText(
                companionSource,
                new CSharpParseOptions(LanguageVersion.Latest),
                path: "Main.cs"));
        }
        var compilation = CSharpCompilation.Create(
            "AetherUI.GeneratedView.Tests",
            syntaxTrees,
            references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        using var assembly = new MemoryStream();
        var emit = compilation.Emit(assembly);
        if (!emit.Success)
        {
            var diagnostics = string.Join(
                Environment.NewLine,
                emit.Diagnostics.Where(static diagnostic => diagnostic.Severity == DiagnosticSeverity.Error));
            throw new InvalidOperationException($"El C# generado no compiló:{Environment.NewLine}{diagnostics}");
        }
    }

    private static void Click(UiDocumentHost host, string id)
    {
        host.Render();
        var element = Assert.NotNull(host.Document?.GetElementById(id));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(element));
        var point = new PointF(box.BorderBox.X + box.BorderBox.Width / 2, box.BorderBox.Y + box.BorderBox.Height / 2);
        host.PointerPressed(point);
        host.PointerReleased(point);
    }

    private sealed class ButtonComponent : UiComponent
    {
        public string Label { get; init; } = string.Empty;
        public int ClickCount { get; private set; }

        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<body><button id=\"button\"");
            writer.WriteEventAttribute("onclick", _ => ClickCount++);
            writer.WriteLiteral(">");
            writer.Write(Label);
            writer.WriteLiteral("</button></body>");
        }
    }

    private sealed class FormComponent : UiComponent
    {
        public int SubmitCount { get; private set; }

        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<body><form id=\"form\"");
            writer.WriteEventAttribute("onsubmit", OnSubmit);
            writer.WriteLiteral("><input id=\"name\" required><button id=\"submit\" type=\"submit\">Enviar</button></form></body>");
        }

        private void OnSubmit(UiEvent e)
        {
            e.PreventDefault();
            SubmitCount++;
        }
    }

    private static void GradientTextClipAppliesToTextRun()
    {
        var reported = new List<CssStyleDiagnostic>();
        void OnReported(CssStyleDiagnostic diagnostic) => reported.Add(diagnostic);
        CssDiagnostics.Reported += OnReported;
        try
        {
            var component = new GradientLogoComponent();
            using var renderer = new RecordingRenderer();
            using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
            host.SetViewport(640, 480, 1);
            host.Render();

            var logo = Assert.NotNull(host.Document?.GetElementById("logo"));
            var box = Assert.NotNull(host.Layout?.FindPrimaryBox(logo));

            Assert.True(box.Style.TextGradient is not null);
            Assert.True(box.Style.BackgroundGradient is null);
            Assert.Equal(0f, box.Style.BackgroundColor.A);
            Assert.True(!reported.Exists(static d => d.Property is "-webkit-background-clip" or "-webkit-text-fill-color"));
        }
        finally
        {
            CssDiagnostics.Reported -= OnReported;
        }
    }

    private static void NativeAbiContractRejectsIncompatibleMetadata()
    {
        Assert.True(NativeAbiContract.EnsureCompatible() >= NativePaintCommandLayout.MinimumBufferSize);
        Assert.Equal(NativePaintCommandLayout.MinimumBufferSize,
            NativeAbiContract.Validate(NativeAbiContract.ExpectedVersion,
                NativeAbiContract.RequiredCapabilities,
                (nuint)NativePaintCommandLayout.MinimumBufferSize));
        Assert.Throws<InvalidOperationException>(() => NativeAbiContract.Validate(
            NativeAbiContract.ExpectedVersion + 1, NativeAbiContract.RequiredCapabilities,
            (nuint)NativePaintCommandLayout.MinimumBufferSize));
        Assert.Throws<InvalidOperationException>(() => NativeAbiContract.Validate(
            NativeAbiContract.ExpectedVersion, NativeAbiContract.RequiredCapabilities & ~1UL,
            (nuint)NativePaintCommandLayout.MinimumBufferSize));
        Assert.Throws<InvalidOperationException>(() => NativeAbiContract.Validate(
            NativeAbiContract.ExpectedVersion, NativeAbiContract.RequiredCapabilities,
            (nuint)(NativePaintCommandLayout.MinimumBufferSize - 1)));
        NativeAbiContract.ValidateComputedStyleSize(512, 512);
        Assert.Throws<InvalidOperationException>(() => NativeAbiContract.ValidateComputedStyleSize(512, 511));
    }

    private static void NativeStyleRuntimeRebuildReplacesDom()
    {
        var component = new RebuildComponent();
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();
        Assert.NotNull(host.Document?.GetElementById("before"));

        component.Toggle();
        host.Render();

        Assert.True(host.Document?.GetElementById("before") is null);
        Assert.NotNull(host.Document?.GetElementById("after"));
    }

    private static void NativeStyleRuntimePaintsBodyShorthandGradient()
    {
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new BodyGradientComponent(), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var body = Assert.NotNull(host.Document?.Body);
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(body));
        Assert.True(box.Style.BackgroundGradient is { });
        var gradient = box.Style.BackgroundGradient.GetValueOrDefault();
        Assert.Equal(3, gradient.Stops.Count);
        Assert.True(gradient.Stops[0].Color.B > gradient.Stops[0].Color.R);
    }

    private static void NativeStyleRuntimeEmitsBodySolidBackground()
    {
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new BodySolidBackgroundComponent(), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var body = Assert.NotNull(host.Document?.Body);
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(body));
        Assert.True(box.BorderBox.Height >= 480);
        Assert.True(box.Style.BackgroundColor.B > box.Style.BackgroundColor.R);
        Assert.True(renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
            .Any(fill => fill.Rectangle == box.BorderBox && fill.Color.B > fill.Color.R && fill.Color.A >= .99f) == true);
    }

    private static void NativePipelineRendersDashboardFixture()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "Fixtures", "glass_dashboard.html");
        if (!File.Exists(path))
        {
            throw new FileNotFoundException("The checked-in dashboard regression fixture was not copied to the test output.", path);
        }
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(File.ReadAllText(path)), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(900, 900, 1);
        host.Render();
        Assert.True(renderer.LastDisplayList?.Count > 0);
        var textCommands = renderer.LastDisplayList?.Commands
            .Select(command => command switch
            {
                DrawTextCommand text => text.Text,
                DrawTextGradientCommand gradientText => gradientText.Text,
                _ => null,
            })
            .OfType<string>()
            .ToArray() ?? [];
        foreach (var expected in new[] { "Admin", "User", "GPU", "USAGE" })
        {
            if (!textCommands.Contains(expected))
            {
                throw new InvalidOperationException($"Native display list is missing '{expected}': {string.Join(" | ", textCommands)}");
            }
        }

        var title = Assert.NotNull(host.Document?.GetElementsByTagName("h1").SingleOrDefault());
        var titleBox = Assert.NotNull(host.Layout?.FindPrimaryBox(title));
        if (!textCommands.Contains("System") || !textCommands.Contains("Overview"))
        {
            var treeTitle = host.Layout!.Root.DescendantsAndSelf().First(box => box.Element == title);
            var main = host.Document!.GetElementsByTagName("div").Single(element => element.GetAttribute("class") == "main-content");
            var header = host.Document.GetElementsByTagName("div").Single(element => element.GetAttribute("class") == "header");
            var treeMain = host.Layout.Root.DescendantsAndSelf().First(box => box.Element == main);
            var treeHeader = host.Layout.Root.DescendantsAndSelf().First(box => box.Element == header);
            throw new InvalidOperationException($"Title was not painted: primary={titleBox.BorderBox.Width}x{titleBox.BorderBox.Height}, tree={treeTitle.BorderBox.Width}x{treeTitle.BorderBox.Height}, header={treeHeader.BorderBox.Width}x{treeHeader.BorderBox.Height}, main={treeMain.BorderBox.Width}x{treeMain.BorderBox.Height}, visible={treeTitle.Style.Visible}, children={treeTitle.Children.Count}.");
        }
        if (!(titleBox.BorderBox.Width > 200 && titleBox.BorderBox.Height >= 40))
        {
            throw new InvalidOperationException($"Unexpected title box {titleBox.BorderBox.Width}x{titleBox.BorderBox.Height}.");
        }
    }

    private static void NativeStyleRuntimeEmitsNestedElementBackground()
    {
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new NestedSolidBackgroundComponent(), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var box = Assert.NotNull(host.Document?.GetElementById("box"));
        var layoutBox = Assert.NotNull(host.Layout?.FindPrimaryBox(box));
        Assert.True(layoutBox.Style.BackgroundColor.B > layoutBox.Style.BackgroundColor.R);
        Assert.True(renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
            .Any(fill => fill.Rectangle == layoutBox.BorderBox && fill.Color.B > fill.Color.R && fill.Color.A >= .99f) == true);
    }

    private static void NativeBoxShadowListsPreservePaintOrder()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #window {
                width: 180px; height: 100px; background: white;
                box-shadow: 1px 2px 3px #ff0000, inset 4px 5px 6px #00ff00, 7px 8px 9px #0000ff;
              }
            </style>
            <body><div id="window"></div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var window = Assert.NotNull(host.Document?.GetElementById("window"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(window));
        Assert.Equal(2, box.Style.BoxShadows.Count);

        var shadows = renderer.LastDisplayList?.Commands.OfType<DrawBoxShadowCommand>().ToArray()
            ?? throw new InvalidOperationException("Native display list did not emit box shadows.");
        Assert.Equal(2, shadows.Length);
        // CSS's first declared shadow is closest to the box, so the painter must
        // emit the last outer shadow first and the first one last.
        Assert.Equal(7f, shadows[0].Shadow.OffsetX);
        Assert.Equal(8f, shadows[0].Shadow.OffsetY);
        Assert.Equal(1f, shadows[1].Shadow.OffsetX);
        Assert.Equal(2f, shadows[1].Shadow.OffsetY);
    }

    private static void NativeBackdropFilterEmitsOrderedDeclarativeCommands()
    {
        const string markup = """
            <style>
              body { display:block; margin:0; background:#123456; }
              #panel {
                display:block; width:180px; height:100px;
                background:rgba(255,255,255,.35); border-radius:12px;
                backdrop-filter:blur(3px) blur(5px);
              }
            </style>
            <body><div id="panel"></div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var commands = renderer.LastDisplayList?.Commands.ToArray()
            ?? throw new InvalidOperationException("Native renderer did not produce a display list.");
        var index = Array.FindIndex(commands, command => command is ApplyBackdropFilterCommand);
        Assert.True(index >= 0);
        var filter = (ApplyBackdropFilterCommand)commands[index];
        Assert.Equal(2, filter.Filters.Count);
        Assert.Equal(BackdropFilterKind.Blur, filter.Filters[0].Kind);
        Assert.Equal(3f, filter.Filters[0].LengthAmount.Value);
        Assert.Equal(5f, filter.Filters[1].LengthAmount.Value);
        Assert.True(filter.Radii.TopLeftX > 0);
        Assert.True(index + 1 < commands.Length && commands[index + 1] is FillRoundedRectangleCommand);
    }

    private static void ZIndexReordersPaintAboveDomOrder()
    {
        // #high is first in DOM but carries the higher z-index. z-index must
        // beat DOM order: #low (z-index 1) paints first, #high (5) last.
        const string reorderMarkup = """
            <style>
              body { margin: 0; }
              #high { position: absolute; width: 100px; height: 100px; background-color: #0000ff; z-index: 5; }
              #low { position: absolute; width: 100px; height: 100px; background-color: #ff0000; z-index: 1; }
            </style>
            <body><div id="high"></div><div id="low"></div></body>
            """;
        using (var renderer = new RecordingRenderer())
        using (var host = new UiDocumentHost(new RawMarkupComponent(reorderMarkup), new ApproximateTextMeasurer(), renderer, new UiHostOptions()))
        {
            host.SetViewport(320, 200, 1);
            host.Render();
            var fills = ZIndexFillCommands(renderer);
            Assert.Equal(2, fills.Length);
            Assert.True(IsRedFill(fills[0]) && IsBlueFill(fills[1]),
                $"z-index must paint #low (red, 1) before #high (blue, 5); got {DescribeFills(fills)}");
        }

        // Control: without z-index the same document keeps exact DOM order,
        // so #high (blue) paints first.
        const string controlMarkup = """
            <style>
              body { margin: 0; }
              #high { position: absolute; width: 100px; height: 100px; background-color: #0000ff; }
              #low { position: absolute; width: 100px; height: 100px; background-color: #ff0000; }
            </style>
            <body><div id="high"></div><div id="low"></div></body>
            """;
        using (var renderer = new RecordingRenderer())
        using (var host = new UiDocumentHost(new RawMarkupComponent(controlMarkup), new ApproximateTextMeasurer(), renderer, new UiHostOptions()))
        {
            host.SetViewport(320, 200, 1);
            host.Render();
            var fills = ZIndexFillCommands(renderer);
            Assert.Equal(2, fills.Length);
            Assert.True(IsBlueFill(fills[0]) && IsRedFill(fills[1]),
                $"no z-index must keep DOM order (#high blue first, #low red last); got {DescribeFills(fills)}");
        }
    }

    private static FillRoundedRectangleCommand[] ZIndexFillCommands(RecordingRenderer renderer)
    {
        var commands = renderer.LastDisplayList?.Commands
            ?? throw new InvalidOperationException("Native renderer did not produce a display list.");
        return commands.OfType<FillRoundedRectangleCommand>()
            .Where(fill => fill.Color.A >= .99f && (fill.Color.R > .5f || fill.Color.B > .5f))
            .ToArray();
    }

    private static bool IsRedFill(FillRoundedRectangleCommand fill) => fill.Color.R > .5f && fill.Color.B < .5f;

    private static bool IsBlueFill(FillRoundedRectangleCommand fill) => fill.Color.B > .5f && fill.Color.R < .5f;

    private static string DescribeFills(FillRoundedRectangleCommand[] fills) =>
        string.Join(", ", fills.Select(fill => $"({fill.Color.R:F2},{fill.Color.G:F2},{fill.Color.B:F2})"));

    private static void NativeTextShadowInheritsAndEmitsPaintCommands()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #parent { color: #224466; text-shadow: 1px 2px 3px currentColor, 4px 5px #0000ff; }
              #label { display: block; width: 200px; font-size: 20px; }
            </style>
            <body><div id="parent"><span id="label">ShadowText</span></div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var label = Assert.NotNull(host.Document?.GetElementById("label"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(label));
        Assert.Equal(2, box.Style.TextShadows.Count);
        Assert.Equal(1f, box.Style.TextShadows[0].OffsetX);
        Assert.Equal(0x22 / 255f, box.Style.TextShadows[0].Color.R);

        var commands = renderer.LastDisplayList?.Commands
            ?? throw new InvalidOperationException("Native display list was not captured.");
        var shadows = commands.OfType<DrawTextShadowCommand>()
            .Where(static shadow => shadow.Text == "ShadowText").ToArray();
        Assert.Equal(2, shadows.Length);
        // The last declared shadow is painter's first; the first declared CSS
        // shadow remains closest to the glyph and therefore paints on top.
        Assert.Equal(4f, shadows[0].Shadow.OffsetX);
        Assert.Equal(1f, shadows[1].Shadow.OffsetX);
        var primaryTextIndex = commands.ToList().FindIndex(static command => command is DrawTextCommand text && text.Text == "ShadowText");
        Assert.True(primaryTextIndex >= 2);
        Assert.True(commands[primaryTextIndex - 1] is DrawTextShadowCommand);
        Assert.True(commands[primaryTextIndex - 2] is DrawTextShadowCommand);
    }

    private static void NativeTransformCascadePreservesTypedOperations()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #item {
                display: block; width: 120px; height: 80px;
                transform: translate(25%, 10px) rotate(.25turn) scale(2, .5) skewX(45deg);
                transform-origin: bottom left;
              }
            </style>
            <body><div id="item">Typed transform</div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var item = Assert.NotNull(host.Document?.GetElementById("item"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(item));
        Assert.Equal(4, box.Style.Transforms.Count);
        Assert.Equal(TransformOperationKind.Translate, box.Style.Transforms[0].Kind);
        Assert.Equal(CssUnit.Percent, box.Style.Transforms[0].X.Unit);
        Assert.Equal(25f, box.Style.Transforms[0].X.Value);
        Assert.Equal(CssUnit.Pixels, box.Style.Transforms[0].Y.Unit);
        Assert.Equal(10f, box.Style.Transforms[0].Y.Value);
        Assert.Equal(TransformOperationKind.Rotate, box.Style.Transforms[1].Kind);
        Assert.True(MathF.Abs(box.Style.Transforms[1].AngleRadians - MathF.PI / 2f) < .001f);
        Assert.Equal(TransformOperationKind.Scale, box.Style.Transforms[2].Kind);
        Assert.Equal(2f, box.Style.Transforms[2].ScalarX);
        Assert.Equal(.5f, box.Style.Transforms[2].ScalarY);
        Assert.Equal(TransformOperationKind.SkewX, box.Style.Transforms[3].Kind);
        Assert.Equal(0f, box.Style.TransformOrigin.X.Value);
        Assert.Equal(100f, box.Style.TransformOrigin.Y.Value);

        // Transform is deliberately post-layout. Until the next cut emits
        // brackets, its data must not change the element's flow dimensions.
        Assert.Equal(120f, box.BorderBox.Width);
        Assert.Equal(80f, box.BorderBox.Height);

        var commands = renderer.LastDisplayList?.Commands.ToArray()
            ?? throw new InvalidOperationException("Native transform fixture did not produce a display list.");
        var pushIndex = Array.FindIndex(commands, static command => command is PushTransformCommand);
        var popIndex = Array.FindLastIndex(commands, static command => command is PopTransformCommand);
        Assert.True(pushIndex >= 0 && popIndex > pushIndex, "Decoded display list must contain a balanced transform bracket.");
        var push = (PushTransformCommand)commands[pushIndex];
        // translate(25%, 10px) is resolved against the 120x80 border box;
        // the non-identity matrix proves the native structural command was decoded.
        Assert.True(MathF.Abs(push.Matrix.Dx) > .01f || MathF.Abs(push.Matrix.Dy) > .01f,
            "Decoded transform matrix must retain the resolved translation/origin component.");
    }

    private static void NativeRotatedBorderTransformMatchesBorderBoxOrigin()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #ring {
                display: block; width: 180px; height: 180px;
                border: 15px solid #222; border-top-color: #fa114f;
                border-radius: 50%; transform: rotate(45deg);
              }
            </style>
            <body><div id="ring"></div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(400, 300, 1);
        host.Render();

        var ring = Assert.NotNull(host.Document?.GetElementById("ring"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(ring));
        var commands = renderer.LastDisplayList?.Commands.ToArray()
            ?? throw new InvalidOperationException("Rotated border fixture did not produce a display list.");
        var borderIndex = Array.FindIndex(commands, command => command is FillBorderSideCommand side && side.OuterRectangle == box.BorderBox);
        Assert.True(borderIndex > 0 && commands[borderIndex - 1] is PushTransformCommand,
            "The rotated border must be immediately scoped by its decoded transform command.");

        var matrix = ((PushTransformCommand)commands[borderIndex - 1]).Matrix;
        var cosine = MathF.Sqrt(.5f);
        var centerX = box.BorderBox.X + box.BorderBox.Width / 2f;
        var centerY = box.BorderBox.Y + box.BorderBox.Height / 2f;
        Assert.True(MathF.Abs(matrix.M11 - cosine) < .001f && MathF.Abs(matrix.M12 - cosine) < .001f &&
                    MathF.Abs(matrix.M21 + cosine) < .001f && MathF.Abs(matrix.M22 - cosine) < .001f &&
                    MathF.Abs(matrix.Dx - (centerX - centerX * cosine + centerY * cosine)) < .001f &&
                    MathF.Abs(matrix.Dy - (centerY - centerX * cosine - centerY * cosine)) < .001f,
            $"The decoded rotation matrix must preserve the border-box center. Actual: {matrix}; box: {box.BorderBox}.");
    }

    private static void NativeAbsoluteFlexStaticPositionsCenterRingBoxes()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #container { position: relative; width: 200px; height: 200px; display: flex; align-items: center; justify-content: center; }
              .ring { position: absolute; border: 15px solid #222; border-radius: 50%; }
              #outer { width: 180px; height: 180px; }
              #middle { width: 140px; height: 140px; }
              #inner { width: 100px; height: 100px; }
            </style>
            <body><div id="container"><div id="outer" class="ring"></div><div id="middle" class="ring"></div><div id="inner" class="ring"></div></div></body>
            """;
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(400, 300, 1);
        host.Render();

        var container = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("container"))));
        var expectedCenterX = container.BorderBox.X + container.BorderBox.Width / 2f;
        var expectedCenterY = container.BorderBox.Y + container.BorderBox.Height / 2f;
        foreach (var id in new[] { "outer", "middle", "inner" })
        {
            var ring = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById(id))));
            var centerX = ring.BorderBox.X + ring.BorderBox.Width / 2f;
            var centerY = ring.BorderBox.Y + ring.BorderBox.Height / 2f;
            Assert.True(MathF.Abs(centerX - expectedCenterX) < .01f && MathF.Abs(centerY - expectedCenterY) < .01f,
                $"Absolute flex static positioning must center #{id}. Container center=({expectedCenterX}, {expectedCenterY}); ring center=({centerX}, {centerY}).");
        }
    }

    private static void NativeFlexLayoutPreservesNestedContentWidth()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #header { display: flex; justify-content: space-between; padding: 20px; }
              #title { font-size: 28px; }
              #open { width: 72px; height: 36px; }
            </style>
            <body>
              <div id="header">
                <div id="copy"><h1 id="title">Validation workspace</h1><p>Nested flex content</p></div>
                <button id="open">Open</button>
              </div>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(900, 480, 1);
        host.Render();

        var copy = Assert.NotNull(host.Document?.GetElementById("copy"));
        var title = Assert.NotNull(host.Document?.GetElementById("title"));
        var copyBox = Assert.NotNull(host.Layout?.FindPrimaryBox(copy));
        var titleBox = Assert.NotNull(host.Layout?.FindPrimaryBox(title));
        Assert.True(copyBox.BorderBox.Width > 150);
        Assert.True(titleBox.BorderBox.Width > 150);
        Assert.True(renderer.LastDisplayList?.Commands.OfType<DrawTextCommand>()
            .Any(command => command.Text.Contains("Validation", StringComparison.Ordinal)) == true);
    }

    private static void NativeRootScrollsOverflowingContent()
    {
        const string markup = """
            <style>
              body { margin: 0; height: 100%; overflow-y: auto; }
              .row { height: 80px; border-bottom: 1px solid #334155; }
            </style>
            <body>
              <div class="row">One</div><div class="row">Two</div><div class="row">Three</div>
              <div class="row">Four</div><div class="row">Five</div><div class="row">Six</div>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 240, 1);
        host.Render();

        var body = Assert.NotNull(host.Document?.Body);
        var initial = Assert.NotNull(host.Layout?.FindPrimaryBox(body));
        Assert.True(initial.ScrollableOverflowY > 0);

        host.ResetMetrics();
        host.MouseWheelScrolled(new PointF(12, 12), -2);
        host.Render();

        var scrolled = host.GetScrollOffset(body);
        Assert.True(scrolled.Y > 0);
        var metrics = host.Metrics;
        Assert.Equal(0L, metrics.StyleResolutions);
        Assert.Equal(0L, metrics.LayoutBuilds);
        Assert.Equal(0L, metrics.DisplayListBuilds);
        Assert.Equal(1L, metrics.ScrollCompositions);
        Assert.True(renderer.LastDisplayList?.Commands.OfType<PushScrollOffsetCommand>().Any() == true);
        Assert.True(renderer.LastDisplayList?.Commands.OfType<DrawScrollbarChromeCommand>().Any() == true);
    }

    private static void NativeVisibleOverflowDoesNotBecomeScrollContainer()
    {
        const string markup = """
            <style>
              body { margin: 0; overflow: visible; }
              .box { height: 80px; overflow: visible; background: #111827; }
              .content { height: 240px; background: #38bdf8; }
            </style>
            <body><div id="box" class="box"><div class="content"></div></div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(240, 120, 1);
        host.Render();

        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("box"))));
        var body = Assert.NotNull(host.Document?.Body);
        Assert.Equal(OverflowKind.Visible, box.Style.OverflowY);
        // Viewport propagation applies ONLY to the root: an ordinary overflow:visible div is not a
        // scroll container, while the page root becomes scrollable (visible on the viewport -> auto).
        Assert.Equal(OverflowKind.Auto, host.Layout!.ViewportOverflowY);

        host.ResetMetrics();
        host.MouseWheelScrolled(new PointF(box.BorderBox.X + 10, box.BorderBox.Y + 10), -3);
        host.Render();
        // The wheel chains past the non-scrollable div to the page root instead of scrolling the div.
        Assert.Equal(default, host.GetScrollOffset(box.Element));
        Assert.True(host.GetScrollOffset(body).Y > 0,
            "A visible div must not swallow the wheel; the page root scrolls by default.");
        Assert.Equal(1L, host.Metrics.ScrollCompositions);
    }

    private static void PageScrollsByDefaultWithoutOverflowRule()
    {
        const string markup = """
            <style>
              * { box-sizing: border-box; }
              body { margin: 0; }
              .row { height: 120px; border-bottom: 1px solid #334155; }
            </style>
            <body>
              <div class="row">One</div><div class="row">Two</div><div class="row">Three</div>
              <div class="row">Four</div><div class="row">Five</div><div class="row">Six</div>
              <div class="row">Seven</div><div class="row">Eight</div>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 240, 1);
        host.Render();

        var body = Assert.NotNull(host.Document?.Body);
        var bodyBox = Assert.NotNull(host.Layout?.FindPrimaryBox(body));
        Assert.Equal(OverflowKind.Visible, bodyBox.Style.OverflowY);
        Assert.Equal(OverflowKind.Auto, host.Layout!.ViewportOverflowY);
        Assert.True(bodyBox.ScrollableOverflowY > 0,
            "A page without any overflow rule must expose scrollable overflow on the root.");

        host.ResetMetrics();
        host.MouseWheelScrolled(new PointF(12, 12), -2);
        host.Render();

        var scrolled = host.GetScrollOffset(body);
        Assert.True(scrolled.Y > 0, "Wheel input must move the page root when no overflow rule exists.");
        Assert.Equal(1L, host.Metrics.ScrollCompositions);
        Assert.True(renderer.LastDisplayList?.Commands.OfType<PushScrollOffsetCommand>().Any() == true);
    }

    private static void DivHiddenOverflowIsProgrammaticScrollContainer()
    {
        const string markup = """
            <style>
              body { margin: 0; height: 100%; overflow: hidden; }
              .box { height: 100px; overflow: hidden; }
              .content { height: 400px; }
            </style>
            <body><div id="box" class="box"><div class="content"></div></div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 200, 1);
        host.Render();

        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("box"))));
        Assert.Equal(OverflowKind.Hidden, box.Style.OverflowY);
        Assert.True(box.ScrollableOverflowY > 0, "overflow:hidden must be a scroll container with a range.");
        Assert.True(!box.Scrollbar.VerticalVisible, "overflow:hidden must never show user scrollbars.");

        // Programmatic scroll is allowed on a hidden container.
        var boxElement = Assert.NotNull(host.Document?.GetElementById("box"));
        host.SetScrollOffset(boxElement, new ScrollOffset(0, 150));
        host.Render();
        Assert.Equal(150f, host.GetScrollOffset(boxElement).Y);

        // The wheel must NOT scroll it (chains past to the equally-hidden page root).
        host.SetScrollOffset(boxElement, default);
        host.Render();
        host.ResetMetrics();
        host.MouseWheelScrolled(new PointF(box.BorderBox.X + 10, box.BorderBox.Y + 10), -2);
        Assert.True(host.GetScrollOffset(boxElement).Y == 0f, "Wheel input must not scroll a hidden container.");
        Assert.False(host.NeedsRender);
        Assert.Equal(0L, host.Metrics.ScrollCompositions);
    }

    private static void DivClipIsNeverScrollContainer()
    {
        const string markup = """
            <style>
              body { margin: 0; height: 100%; overflow: hidden; }
              .box { height: 100px; overflow: clip; }
              .content { height: 400px; }
            </style>
            <body><div id="box" class="box"><div class="content"></div></div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 200, 1);
        host.Render();

        var boxElement = Assert.NotNull(host.Document?.GetElementById("box"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(boxElement));
        Assert.Equal(OverflowKind.Clip, box.Style.OverflowY);
        Assert.True(box.ScrollableOverflowY == 0f, "overflow:clip must never accumulate a scroll range.");
        Assert.False(box.Scrollbar.VerticalVisible);

        // Neither programmatic nor wheel scroll can move a clip container.
        host.SetScrollOffset(boxElement, new ScrollOffset(0, 150));
        host.Render();
        Assert.True(host.GetScrollOffset(boxElement).Y == 0f, "overflow:clip must reject programmatic scroll.");
        host.ResetMetrics();
        host.MouseWheelScrolled(new PointF(box.BorderBox.X + 10, box.BorderBox.Y + 10), -2);
        Assert.True(host.GetScrollOffset(boxElement).Y == 0f, "Wheel input must not scroll a clip container.");
        Assert.Equal(0L, host.Metrics.ScrollCompositions);
    }

    private static void OverflowScrollAlwaysShowsChrome()
    {
        const string markup = """
            <style>
              body { margin: 0; overflow: hidden; }
              .box { width: 150px; height: 100px; overflow: scroll; }
              .content { width: 50px; height: 20px; }
            </style>
            <body><div id="box" class="box"><div class="content"></div></div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 200, 1);
        host.Render();

        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("box"))));
        Assert.Equal(0f, box.ScrollableOverflowX);
        Assert.Equal(0f, box.ScrollableOverflowY);
        Assert.True(box.Scrollbar.HorizontalVisible && box.Scrollbar.VerticalVisible,
            "overflow:scroll must show both scrollbar geometries even with no scrollable overflow.");
        Assert.True(renderer.LastDisplayList?.Commands.OfType<DrawScrollbarChromeCommand>().Any() == true,
            "overflow:scroll must emit scrollbar chrome even when the content fits.");
    }

    private static void OneAxisVisibleComputesAuto()
    {
        const string markup = """
            <style>
              body { margin: 0; overflow: hidden; }
              .box { width: 150px; height: 100px; overflow-x: auto; }
              .content { width: 300px; height: 300px; }
            </style>
            <body><div id="box" class="box"><div class="content"></div></div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 200, 1);
        host.Render();

        var boxElement = Assert.NotNull(host.Document?.GetElementById("box"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(boxElement));
        // overflow-y: visible next to overflow-x: auto must compute to auto (CSS Overflow 3.1).
        Assert.Equal(OverflowKind.Auto, box.Style.OverflowX);
        Assert.Equal(OverflowKind.Auto, box.Style.OverflowY);
        Assert.True(box.ScrollableOverflowY > 0, "The normalized auto axis must expose a vertical scroll range.");

        host.ResetMetrics();
        host.MouseWheelScrolled(new PointF(box.BorderBox.X + 10, box.BorderBox.Y + 10), -2);
        Assert.True(host.GetScrollOffset(boxElement).Y > 0, "Wheel input must scroll the normalized auto axis.");
        host.SetScrollOffset(boxElement, default);
        host.Render();
        host.MouseHorizontalWheelScrolled(new PointF(box.BorderBox.X + 10, box.BorderBox.Y + 10), 100);
        Assert.True(host.GetScrollOffset(boxElement).X > 0, "The horizontal wheel must scroll the authored auto axis.");
    }


    private static void NativeScrollMetricsAreBidirectionalAndResizeAware()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              .scroller { height: 100px; overflow: auto; }
              .content { width: 600px; height: 240px; }
            </style>
            <body><div id="scroller" class="scroller"><div class="content"></div></div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 160, 1);
        host.Render();
        var narrow = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("scroller"))));
        Assert.True(narrow.ScrollableOverflowX > 0);
        Assert.True(narrow.ScrollableOverflowY > 0);
        Assert.Equal(0f, narrow.ScrollOffsetX);
        Assert.Equal(0f, narrow.ScrollOffsetY);

        host.SetViewport(480, 160, 1);
        host.Render();
        var wide = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("scroller"))));
        Assert.True(wide.ScrollableOverflowX < narrow.ScrollableOverflowX);
        Assert.Equal(narrow.ScrollableOverflowY, wide.ScrollableOverflowY);

        // 480 physical pixels at 1.5 DPI remains the same 320 logical-pixel viewport.
        host.SetViewport(480, 240, 1.5f);
        host.Render();
        var dpiAdjusted = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("scroller"))));
        Assert.True(dpiAdjusted.ScrollableOverflowX > wide.ScrollableOverflowX);
        Assert.Equal(wide.ScrollableOverflowY, dpiAdjusted.ScrollableOverflowY);
    }

    private static void NativeScrollbarChromeSupportsTwoAxisInteractionAndChaining()
    {
        var component = new ScrollbarInteractionComponent();
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(360, 220, 1);
        host.Render();

        LayoutBox Box(string id) => Assert.NotNull(host.Layout?.FindPrimaryBox(
            Assert.NotNull(host.Document?.GetElementById(id))));
        var inner = Box("inner");
        var outer = Box("outer");
        Assert.True(inner.Scrollbar.HorizontalVisible && inner.Scrollbar.VerticalVisible,
            "A two-axis scroll container must expose interactive thumb geometry.");
        Assert.True(outer.Scrollbar.HorizontalVisible,
            "The nested ancestor must retain an independent horizontal range for chaining.");

        var thumb = inner.Scrollbar.HorizontalThumb;
        host.PointerPressed(new PointF(thumb.X + thumb.Width / 2, thumb.Y + thumb.Height / 2));
        host.PointerMoved(new PointF(inner.Scrollbar.HorizontalTrack.Right - 1, thumb.Y + thumb.Height / 2));
        host.PointerReleased(new PointF(inner.Scrollbar.HorizontalTrack.Right - 1, thumb.Y + thumb.Height / 2));
        host.Render();
        Assert.True(host.GetScrollOffset(inner.Element).X > 0, "Dragging a horizontal thumb must update only its horizontal offset.");
        Assert.Equal(0, component.ClickCount);

        var verticalTrack = inner.Scrollbar.VerticalTrack;
        var verticalThumb = inner.Scrollbar.VerticalThumb;
        host.PointerPressed(new PointF(verticalTrack.X + verticalTrack.Width / 2, verticalThumb.Bottom + 1));
        host.PointerReleased(new PointF(verticalTrack.X + verticalTrack.Width / 2, verticalThumb.Bottom + 1));
        host.Render();
        Assert.True(host.GetScrollOffset(inner.Element).Y > 0, "Clicking a vertical track must move by one viewport page.");
        Assert.Equal(0, component.ClickCount);

        var contentPoint = new PointF(inner.BorderBox.X + 10, inner.BorderBox.Y + 10);
        host.MouseHorizontalWheelScrolled(contentPoint, 100);
        host.Render();
        outer = Box("outer");
        Assert.True(host.GetScrollOffset(outer.Element).X > 0,
            "When the nested scroller reaches its horizontal limit, wheel input must chain to its ancestor.");

        inner = Box("inner");
        thumb = inner.Scrollbar.HorizontalThumb;
        host.PointerPressed(new PointF(thumb.X + 1, thumb.Y + 1));
        host.SetViewport(360, 220, 1);
        host.PointerReleased(new PointF(thumb.X + 1, thumb.Y + 1));
        Assert.Equal(0, component.ClickCount);
    }

    private static void NativeHoverPaintImpactSkipsLayout()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #card { width: 120px; height: 80px; background-color: #111827; color: white; }
              #card:hover { background-color: #ef4444; }
            </style>
            <body><div id="card">Hover target</div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(240, 160, 1);
        host.Render();

        var card = Assert.NotNull(host.Document?.GetElementById("card"));
        var box = Assert.NotNull(host.Layout?.FindPrimaryBox(card));
        var inside = new PointF(box.BorderBox.X + 10, box.BorderBox.Y + 10);

        host.ResetMetrics();
        host.PointerMoved(inside);
        var beforePaint = host.Metrics;
        Assert.Equal(0L, beforePaint.StyleResolutions);
        Assert.Equal(0L, beforePaint.LayoutBuilds);
        Assert.Equal(0L, beforePaint.DisplayListBuilds);
        Assert.Equal(0L, beforePaint.RenderCalls);
        _ = host.CursorAt(inside);
        beforePaint = host.Metrics;
        Assert.Equal(0L, beforePaint.StyleResolutions);
        Assert.Equal(0L, beforePaint.LayoutBuilds);
        Assert.Equal(0L, beforePaint.DisplayListBuilds);
        Assert.Equal(0L, beforePaint.RenderCalls);

        host.Render();
        var hovered = host.Metrics;
        Assert.Equal(1L, hovered.StyleResolutions);
        Assert.Equal(1L, hovered.StyleUpdatedElements);
        Assert.Equal(0L, hovered.LayoutBuilds);
        Assert.Equal(0L, hovered.DisplayListBuilds);
        Assert.Equal(0L, hovered.VisualStyleMapBuilds);
        Assert.Equal(1L, hovered.VisualStyleMapUpdates);
        Assert.Equal(1L, hovered.PaintChunkBuilds);
        Assert.Equal(1L, hovered.RenderCalls);
        Assert.True(hovered.DisplayListBuildMilliseconds >= 0d);
        Assert.True(hovered.RenderMilliseconds >= 0d);

        host.ResetMetrics();
        host.PointerMoved(new PointF(inside.X + 4, inside.Y + 4));
        Assert.False(host.NeedsRender);
        var unchangedTarget = host.Metrics;
        Assert.Equal(0L, unchangedTarget.StyleResolutions);
        Assert.Equal(0L, unchangedTarget.StyleUpdatedElements);
        Assert.Equal(0L, unchangedTarget.LayoutBuilds);
        Assert.Equal(0L, unchangedTarget.DisplayListBuilds);
        Assert.Equal(0L, unchangedTarget.VisualStyleMapBuilds);
        Assert.Equal(0L, unchangedTarget.VisualStyleMapUpdates);
        Assert.Equal(0L, unchangedTarget.PaintChunkBuilds);
        Assert.Equal(0L, unchangedTarget.RenderCalls);
    }

    private static void NativeCatalogHoverPaintImpactSkipsLayout()
    {
        const string markup = """
            <style>
              body { margin: 0; background: #0b1324; font-family: "Segoe UI", Arial, sans-serif; }
              .open {
                padding: 10px 16px;
                background: #38bdf8;
                color: #06233a;
                border: 0;
                border-radius: 9px;
                font-weight: 700;
              }
              .open:hover { background: #7dd3fc; }
              .page-card {
                display: flex;
                justify-content: space-between;
                align-items: center;
                margin-bottom: 11px;
                padding: 16px 18px;
                background: rgba(19, 31, 57, 0.94);
                border: 1px solid #263d65;
                border-radius: 13px;
              }
              .page-card:hover {
                background: #1a2b4d;
                border-color: #4a78b9;
              }
              .page-name { color: #f4f8ff; font-size: 16px; font-weight: 700; }
            </style>
            <body>
              <article id="card" class="page-card">
                <div><div class="page-name">vitalis_health</div></div>
                <button id="open" class="open">Open</button>
              </article>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(420, 140, 1);
        host.Render();

        var card = Assert.NotNull(host.Document?.GetElementById("card"));
        var open = Assert.NotNull(host.Document?.GetElementById("open"));
        var cardBox = Assert.NotNull(host.Layout?.FindPrimaryBox(card));
        var openBox = Assert.NotNull(host.Layout?.FindPrimaryBox(open));
        var cardOnlyPoint = new PointF(cardBox.BorderBox.X + 8, cardBox.BorderBox.Y + 8);
        var openPoint = new PointF(openBox.BorderBox.X + openBox.BorderBox.Width / 2, openBox.BorderBox.Y + openBox.BorderBox.Height / 2);

        host.ResetMetrics();
        host.PointerMoved(cardOnlyPoint);
        host.Render();
        var cardHover = host.Metrics;
        Assert.Equal(1L, cardHover.StyleResolutions);
        Assert.Equal(1L, cardHover.StyleUpdatedElements);
        Assert.Equal(NativeStyleImpact.Paint, cardHover.LastStyleImpact);
        AssertNoLayoutBuild("catalog card hover", cardHover);
        Assert.Equal(0L, cardHover.DisplayListBuilds);
        Assert.Equal(0L, cardHover.VisualStyleMapBuilds);
        Assert.Equal(1L, cardHover.VisualStyleMapUpdates);
        Assert.Equal(1L, cardHover.PaintChunkBuilds);

        host.ResetMetrics();
        host.PointerMoved(openPoint);
        host.Render();
        var buttonHover = host.Metrics;
        Assert.Equal(1L, buttonHover.StyleResolutions);
        Assert.Equal(1L, buttonHover.StyleUpdatedElements);
        Assert.Equal(NativeStyleImpact.Paint, buttonHover.LastStyleImpact);
        AssertNoLayoutBuild("catalog button hover", buttonHover);
        Assert.Equal(0L, buttonHover.DisplayListBuilds);
        Assert.Equal(0L, buttonHover.VisualStyleMapBuilds);
        Assert.Equal(1L, buttonHover.VisualStyleMapUpdates);
        Assert.True(buttonHover.PaintChunkBuilds >= 1);
    }

    private static void NativeNestedHoverKeepsAncestorsActive()
    {
        const string markup = """
            <style>
              body { margin: 0; background: #0b1324; }
              .page-card {
                display: flex;
                width: 260px;
                height: 72px;
                padding: 12px;
                background: #132039;
                border: 1px solid #263d65;
              }
              .page-card:hover {
                background: #1a2b4d;
                border-color: #4a78b9;
              }
              .page-name { color: white; font-size: 16px; }
            </style>
            <body>
              <article id="card" class="page-card">
                <div><div id="name" class="page-name">vitalis_health</div></div>
              </article>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 120, 1);
        host.Render();

        var card = Assert.NotNull(host.Document?.GetElementById("card"));
        var name = Assert.NotNull(host.Document?.GetElementById("name"));
        var cardBox = Assert.NotNull(host.Layout?.FindPrimaryBox(card));
        var nameBox = Assert.NotNull(host.Layout?.FindPrimaryBox(name));

        host.ResetMetrics();
        host.PointerMoved(new PointF(cardBox.BorderBox.X + 4, cardBox.BorderBox.Y + 4));
        host.Render();
        var cardHover = host.Metrics;
        Assert.Equal(1L, cardHover.StyleUpdatedElements);
        Assert.Equal(1L, cardHover.PaintChunkBuilds);
        Assert.Equal(1L, cardHover.RenderCalls);

        host.ResetMetrics();
        host.PointerMoved(new PointF(nameBox.BorderBox.X + 2, nameBox.BorderBox.Y + 2));
        host.Render();
        var childHover = host.Metrics;
        Assert.Equal(1L, childHover.StyleResolutions);
        Assert.Equal(0L, childHover.StyleUpdatedElements);
        Assert.Equal(0L, childHover.LayoutBuilds);
        Assert.Equal(0L, childHover.DisplayListBuilds);
        Assert.Equal(0L, childHover.PaintChunkBuilds);
        Assert.Equal(0L, childHover.RenderCalls);
    }

    private static void NativeCatalogHoverStormStaysIncremental()
    {
        var cards = string.Concat(Enumerable.Range(0, 18).Select(i =>
            $"""
            <article id="card-{i}" class="page-card">
              <div><div class="page-name">page_{i:00}</div><div class="page-file">page_{i:00}.html</div></div>
              <button id="open-{i}" class="open">Open</button>
            </article>
            """));
        var markup = $$"""
            <style>
              body { margin: 0; background: #0b1324; font-family: "Segoe UI", Arial, sans-serif; }
              .page-card {
                display: flex;
                justify-content: space-between;
                align-items: center;
                margin-bottom: 11px;
                padding: 16px 18px;
                background: rgba(19, 31, 57, 0.94);
                border: 1px solid #263d65;
                border-radius: 13px;
              }
              .page-card:hover {
                background: #1a2b4d;
                border-color: #4a78b9;
              }
              .page-name { color: #f4f8ff; font-size: 16px; font-weight: 700; }
              .page-file { margin-top: 5px; color: #8fa6c8; font-size: 12px; }
              .open {
                padding: 10px 16px;
                background: #38bdf8;
                color: #06233a;
                border: 0;
                border-radius: 9px;
                font-weight: 700;
              }
              .open:hover { background: #7dd3fc; }
            </style>
            <body>{{cards}}</body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(520, 1500, 1);
        host.Render();

        var points = Enumerable.Range(0, 18).Select(i =>
        {
            var element = Assert.NotNull(host.Document?.GetElementById($"card-{i}"));
            var box = Assert.NotNull(host.Layout?.FindPrimaryBox(element));
            return new PointF(box.BorderBox.X + 10, box.BorderBox.Y + 10);
        }).ToArray();

        host.ResetMetrics();
        foreach (var point in points)
        {
            host.PointerMoved(point);
            Assert.True(host.NeedsRender);
            host.Render();

            host.PointerMoved(new PointF(point.X + 2, point.Y + 2));
            Assert.False(host.NeedsRender);
        }

        var metrics = host.Metrics;
        Assert.Equal(points.Length, metrics.StyleResolutions);
        Assert.Equal(0L, metrics.LayoutBuilds);
        Assert.Equal(0L, metrics.DisplayListBuilds);
        Assert.Equal(0L, metrics.VisualStyleMapBuilds);
        Assert.Equal(points.Length, metrics.VisualStyleMapUpdates);
        Assert.True(metrics.PaintChunkBuilds >= points.Length);
        Assert.True(metrics.PaintChunkBuilds <= (points.Length * 2) - 1);
    }

    private static void NativeScrollAfterHoverPatchRebuildsCanonicalDisplayList()
    {
        var cards = string.Concat(Enumerable.Range(0, 10).Select(i =>
            $"""
            <article id="card-{i}" class="page-card">
              <div><div class="page-name">page_{i:00}</div><div class="page-file">page_{i:00}.html</div></div>
              <button class="open">Open</button>
            </article>
            """));
        var markup = $$"""
            <style>
              body { margin: 0; height: 100%; overflow-y: auto; background: #0b1324; font-family: "Segoe UI", Arial, sans-serif; }
              .page-card {
                display: flex;
                justify-content: space-between;
                align-items: center;
                margin-bottom: 11px;
                padding: 16px 18px;
                background: rgba(19, 31, 57, 0.94);
                border: 1px solid #263d65;
                border-radius: 13px;
              }
              .page-card:hover { background: #1a2b4d; border-color: #4a78b9; }
              .page-name { color: #f4f8ff; font-size: 16px; font-weight: 700; }
              .page-file { margin-top: 5px; color: #8fa6c8; font-size: 12px; }
              .open { padding: 10px 16px; background: #38bdf8; color: #06233a; border: 0; border-radius: 9px; }
              .open:hover { background: #7dd3fc; }
            </style>
            <body>{{cards}}</body>
            """;

        using var renderer = new ControlledIncrementalRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(520, 180, 1);
        host.Render();
        renderer.ResetCounts();

        var firstCard = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("card-0"))));
        host.ResetMetrics();
        host.PointerMoved(new PointF(firstCard.BorderBox.X + 10, firstCard.BorderBox.Y + 10));
        host.Render();
        Assert.True(host.Metrics.PaintChunkBuilds > 0);
        Assert.Equal(0L, host.Metrics.DisplayListBuilds);
        Assert.True(renderer.IncrementalAttempts > 0, "paint-only hover should remain eligible for incremental rendering");

        renderer.ResetCounts();
        host.ResetMetrics();
        host.MouseWheelScrolled(new PointF(firstCard.BorderBox.X + 10, firstCard.BorderBox.Y + 10), -4);
        host.Render();
        Assert.Equal(1L, host.Metrics.DisplayListBuilds);
        Assert.Equal(0L, host.Metrics.LayoutBuilds);
        Assert.Equal(0, renderer.IncrementalAttempts);
        Assert.Equal(1, renderer.RenderCount);
        Assert.False(renderer.LastScrollCompositionMode);

        renderer.ResetCounts();
        host.ResetMetrics();
        host.MouseWheelScrolled(new PointF(firstCard.BorderBox.X + 10, firstCard.BorderBox.Y + 10), -1);
        host.Render();
        Assert.Equal(0L, host.Metrics.DisplayListBuilds);
        Assert.Equal(1L, host.Metrics.ScrollCompositions);
        Assert.Equal(0, renderer.IncrementalAttempts);
        Assert.Equal(1, renderer.RenderCount);
        Assert.True(renderer.LastScrollCompositionMode);
    }

    private static void AssertNoLayoutBuild(string phase, UiDocumentHostMetrics metrics)
    {
        if (metrics.LayoutBuilds == 0)
        {
            return;
        }

        throw new InvalidOperationException(
            $"{phase}: expected paint-only hover to skip layout, but LayoutBuilds={metrics.LayoutBuilds}. " +
            $"Metrics: StyleResolutions={metrics.StyleResolutions}, StyleUpdatedElements={metrics.StyleUpdatedElements}, DisplayListBuilds={metrics.DisplayListBuilds}, PaintChunkBuilds={metrics.PaintChunkBuilds}, " +
            $"RenderCalls={metrics.RenderCalls}, IncrementalRenderCalls={metrics.IncrementalRenderCalls}, " +
            $"LastStyleImpact={metrics.LastStyleImpact}. " +
            DescribeLoadedNativeRuntime());
    }

    private static string DescribeLoadedNativeRuntime()
    {
        try
        {
            using var process = Process.GetCurrentProcess();
            var modules = process.Modules.Cast<ProcessModule>()
                .Where(static module => string.Equals(module.ModuleName, "aether_core.dll", StringComparison.OrdinalIgnoreCase))
                .Select(static module =>
                {
                    var file = module.FileName ?? "<unknown>";
                    var hash = File.Exists(file)
                        ? Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file)))
                        : "<missing>";
                    return $"{file} sha256={hash}";
                })
                .ToArray();
            return modules.Length == 0
                ? "Loaded native runtime: <aether_core.dll not found in process module list>."
                : "Loaded native runtime: " + string.Join("; ", modules) + ".";
        }
        catch (Exception ex)
        {
            return $"Loaded native runtime: <unable to inspect: {ex.GetType().Name}: {ex.Message}>.";
        }
    }

    private static void NativeHoverCoalescesPointerMovesBeforePaint()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #a, #b { position: absolute; top: 0; width: 50px; height: 50px; background-color: #111827; }
              #a { left: 0; }
              #b { left: 70px; }
              #a:hover { background-color: #ef4444; }
              #b:hover { background-color: #22c55e; }
            </style>
            <body><div id="a"></div><div id="b"></div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(160, 80, 1);
        host.Render();

        var a = Assert.NotNull(host.Document?.GetElementById("a"));
        var b = Assert.NotNull(host.Document?.GetElementById("b"));
        var aBox = Assert.NotNull(host.Layout?.FindPrimaryBox(a));
        var bBox = Assert.NotNull(host.Layout?.FindPrimaryBox(b));
        var pointA = new PointF(aBox.BorderBox.X + 10, aBox.BorderBox.Y + 10);
        var pointB = new PointF(bBox.BorderBox.X + 10, bBox.BorderBox.Y + 10);

        host.ResetMetrics();
        host.PointerMoved(pointA);
        host.PointerMoved(pointB);
        var beforePaint = host.Metrics;
        Assert.Equal(0L, beforePaint.StyleResolutions);
        Assert.Equal(0L, beforePaint.LayoutBuilds);
        Assert.Equal(0L, beforePaint.DisplayListBuilds);
        Assert.Equal(0L, beforePaint.RenderCalls);

        host.Render();

        Assert.True((a.State & ElementState.Hover) == 0);
        Assert.True((b.State & ElementState.Hover) != 0);
        var metrics = host.Metrics;
        Assert.Equal(1L, metrics.StyleResolutions);
        Assert.Equal(0L, metrics.LayoutBuilds);
        Assert.Equal(0L, metrics.DisplayListBuilds);
        Assert.Equal(0L, metrics.VisualStyleMapBuilds);
        Assert.Equal(1L, metrics.VisualStyleMapUpdates);
        Assert.Equal(1L, metrics.PaintChunkBuilds);
        Assert.Equal(1L, metrics.RenderCalls);
        Assert.True(metrics.DisplayListBuildMilliseconds >= 0d);
        Assert.True(metrics.RenderMilliseconds >= 0d);
    }

    private static void NativePointerEventsHoverUpdatesHitTestingWithoutLayout()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #bottom, #top { position: absolute; left: 0; top: 0; width: 100px; height: 100px; }
              #bottom { background: #22c55e; color: white; }
              #top { background: #ef4444; color: white; }
              #top:hover { pointer-events: none; }
            </style>
            <body><div id="bottom">bottom</div><div id="top">top</div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(160, 120, 1);
        host.Render();

        var top = Assert.NotNull(host.Document?.GetElementById("top"));
        var bottom = Assert.NotNull(host.Document?.GetElementById("bottom"));
        var point = new PointF(50, 50);

        host.PointerMoved(point);
        host.Render();
        Assert.True((top.State & ElementState.Hover) != 0);

        host.ResetMetrics();
        host.PointerMoved(point);
        host.Render();

        Assert.True((top.State & ElementState.Hover) == 0);
        Assert.True((bottom.State & ElementState.Hover) != 0);
        var metrics = host.Metrics;
        Assert.True(metrics.StyleResolutions >= 1);
        Assert.Equal(0L, metrics.LayoutBuilds);
        Assert.Equal(0L, metrics.DisplayListBuilds);
        Assert.Equal(0L, metrics.PaintChunkBuilds);
    }

    private static void NativeLayoutDisposesBeforeBorrowedStyleMap()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #tabs { display: flex; gap: 8px; width: 220px; overflow: hidden; }
              .tab { display: flex; align-items: center; gap: 4px; padding: 8px; }
              .close { width: 12px; height: 12px; }
            </style>
            <body>
              <div id="tabs">
                <div class="tab">Renderer.cs <span class="close">x</span></div>
                <div class="tab">Engine.cs <span class="close">x</span></div>
              </div>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(300, 160, 1);
        host.Render();

        // A viewport change invalidates layout while reusing the resolved native style map.
        // The previous layout owns anonymous flex item cleanup that borrows that map, so it
        // must be destroyed before the map is replaced.
        host.SetViewport(360, 160, 1);
        host.Render();

        Assert.True(host.Layout?.Root.DescendantsAndSelf().Count() > 1);
    }

    private static void NativeRuntimeRendersGeneratedPseudoElements()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              .weather > #temperature { position: relative; font-size: 80px; }
              .weather > #temperature::after {
                content: "°";
                position: absolute;
                font-size: 40px;
                top: 10px;
              }
            </style>
            <body><div class="weather"><div id="temperature">24</div></div></body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var commands = renderer.LastDisplayList?.Commands.OfType<DrawTextCommand>().ToArray()
            ?? throw new InvalidOperationException("Native renderer did not produce text commands.");
        var temperature = commands.Single(command => command.Text == "24");
        var degree = commands.Single(command => command.Text == "\u00B0");
        Assert.True(degree.Rectangle.X >= temperature.Rectangle.Right - 1f);
    }

    private static void NativeCssTypographySideBordersAndColumnFlexGrow()
    {
        const string markup = """
            <style>
              body { margin: 0; color: #1e293b; font-family: 'Segoe UI', sans-serif; }
              #indicator { width: 100px; height: 30px; border-left: 4px solid #0ea5e9; }
              #rounded-indicator { width: 100px; height: 30px; border-radius: 12px; border-left: 4px solid #0ea5e9; }
              #alpha-gradient { width: 100px; height: 30px; background: linear-gradient(90deg, rgba(255, 215, 0, 0.1), transparent); }
              #offset-gradient { width: 100px; height: 30px; background: linear-gradient(to bottom left, red 70%, #00ff00, green 20%, blue); }
              #edge-matrix { width: 100px; height: 40px; border-top: 4px dotted #ff0000; border-right: 4px dashed #00ff00; border-bottom: 6px double #0000ff; border-left: 6px groove #ff00ff; }
              #hidden-border { width: 30px; height: 20px; border: 8px hidden #ff00ff; }
              #current-color-border { width: 30px; height: 20px; color: #123456; border: 2px solid; }
              #metric { font-size: 28px; font-weight: 800; }
              #metric .unit { font-size: 14px; font-weight: 400; }
              #label { display: block; text-transform: uppercase; }
              #transformed-input { text-transform: uppercase; }
              #column { display: flex; flex-direction: column; height: 200px; gap: 10px; }
              #header { height: 20px; }
              #filler { flex: 1; }
              #record { display: flex; flex-direction: column; height: 600px; padding: 40px; box-sizing: border-box; gap: 30px; }
              #record-header { height: 83px; }
              #record-grid { display: flex; gap: 20px; }
              .record-card { flex: 1; padding: 25px; border: 1px solid white; }
              #record-history { flex: 1; padding: 30px; }
              #margin-collapse { padding: 10px; }
              #margin-first { display: block; height: 20px; margin-bottom: 25px; }
              #margin-second { display: block; height: 20px; margin-top: 20px; }
              #collapse-parent { display: block; margin-top: 10px; margin-bottom: 15px; }
              #collapse-child { display: block; height: 10px; margin-top: 30px; margin-bottom: 25px; }
              #collapse-after { display: block; height: 10px; margin-top: 20px; }
              #empty-collapse { display: block; margin-top: 20px; margin-bottom: 30px; }
              #empty-collapse-after { display: block; height: 10px; margin-top: 10px; }
              #whitespace-collapse-parent { display: block; margin-top: 10px; }
              #whitespace-collapse-child { display: block; height: 10px; margin-top: 30px; }
              #padded-collapse-parent { display: block; padding-top: 1px; margin-top: 10px; }
              #padded-collapse-child { display: block; height: 10px; margin-top: 30px; }
              #bfc-collapse-parent { display: block; overflow: hidden; margin-top: 10px; }
              #bfc-collapse-child { display: block; height: 10px; margin-top: 30px; }
              #grid { display: flex; gap: 20px; }
              .card { flex: 1; padding: 25px; border: 1px solid white; }
              .card-label { display: block; font-size: 13px; margin-bottom: 10px; }
            </style>
            <body>
              <div id="indicator"></div>
              <div id="rounded-indicator"></div>
              <div id="alpha-gradient"></div>
              <div id="offset-gradient"></div>
              <div id="edge-matrix"></div><div id="hidden-border"></div><div id="current-color-border"></div>
              <span id="label">Heart Rate</span>
              <div id="metric">72 <span class="unit">bpm</span></div>
              <button id="edit" style="background: white; border: 1px solid #e2e8f0;">Edit Profile</button>
              <input id="transformed-input" value="find patient">
              <div id="column"><div id="header"></div><div id="filler"></div></div>
              <div id="record"><div id="record-header"></div><div id="record-grid"><div id="record-card-a" class="record-card">Heart Rate<div>72 bpm</div><div style="margin-top: 15px;">Normal</div></div><div class="record-card">Blood Pressure<div>120/80</div><div style="margin-top: 15px;">Optimal</div></div><div class="record-card">Body Temp<div>36.6 °C</div><div style="margin-top: 15px;">Normal</div></div></div><div id="record-history">Medical History</div></div>
              <div id="margin-collapse"><div id="margin-first"></div><div id="margin-second"></div></div>
              <div id="collapse-parent"><div id="collapse-child"></div></div><div id="collapse-after"></div>
              <div id="empty-collapse"></div><div id="empty-collapse-after"></div>
              <div id="whitespace-collapse-parent">
                <div id="whitespace-collapse-child"></div>
              </div>
              <div id="padded-collapse-parent"><div id="padded-collapse-child"></div></div>
              <div id="bfc-collapse-parent"><div id="bfc-collapse-child"></div></div>
              <div id="grid">
                <div id="card-a" class="card"><span class="card-label">One</span><div id="metric-a">72 <span class="unit">bpm</span></div><div style="margin-top: 15px;">Normal</div></div>
                <div class="card"><span class="card-label">Two</span><div>120/80</div><div style="margin-top: 15px;">Optimal</div></div>
                <div class="card"><span class="card-label">Three</span><div>36.6</div><div style="margin-top: 15px;">Normal</div></div>
              </div>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var commands = renderer.LastDisplayList?.Commands ?? throw new InvalidOperationException("Native renderer did not produce a display list.");
        var text = commands.OfType<DrawTextCommand>().ToArray();
        var labelRuns = text.Where(command => command.Text is "HEART" or "RATE").ToArray();
        Assert.Equal(2, labelRuns.Length);
        Assert.True(labelRuns.All(command => command.FontFamily == "Segoe UI"));

        var value = Assert.NotNull(text.FirstOrDefault(command => command.Text == "72"));
        var unit = Assert.NotNull(text.FirstOrDefault(command => command.Text == "bpm"));
        Assert.True(MathF.Abs((value.Rectangle.Y + value.FontSize * .8f) - (unit.Rectangle.Y + unit.FontSize * .8f)) < .01f);

        var edit = Assert.NotNull(text.SingleOrDefault(command => command.Text == "Edit"));
        var profile = Assert.NotNull(text.SingleOrDefault(command => command.Text == "Profile"));
        Assert.True(edit.Color.R < .2f && edit.Color.B < .3f);
        Assert.Equal(edit.Color, profile.Color);
        Assert.NotNull(text.SingleOrDefault(command => command.Text == "FIND PATIENT"));

        var borderSides = commands.OfType<FillBorderSideCommand>().ToArray();
        Assert.True(borderSides.Any(command => command.Side == 3 && command.Color.B > command.Color.R), "left border side was not emitted");
        Assert.True(borderSides.Any(command => command.Side == 3 && MathF.Abs(command.OuterRadii.TopLeftX - 12f) < .01f &&
            MathF.Abs(command.InnerRadii.TopLeftX - 8f) < .01f), "rounded left border did not carry outer/inner radii");
        var alphaGradient = Assert.NotNull(commands.OfType<FillRoundedRectangleGradientCommand>().FirstOrDefault(command =>
            MathF.Abs(command.Rectangle.Width - 100f) < .01f && MathF.Abs(command.Rectangle.Height - 30f) < .01f));
        Assert.Equal(2, alphaGradient.Gradient.Stops.Count);
        Assert.True(MathF.Abs(alphaGradient.Gradient.Stops[0].Color.A - .1f) < .01f);
        Assert.Equal(0f, alphaGradient.Gradient.Stops[1].Color.A);
        var offsetGradient = Assert.NotNull(commands.OfType<FillRoundedRectangleGradientCommand>().FirstOrDefault(command =>
            command.Gradient.DirectionX < 0f && command.Gradient.DirectionY > 0f));
        var offsetStops = offsetGradient.Gradient.Stops;
        Assert.Equal(4, offsetStops.Count);
        Assert.Equal(.7f, offsetStops[0].Offset);
        Assert.Equal(.7f, offsetStops[1].Offset);
        Assert.Equal(.7f, offsetStops[2].Offset);
        Assert.Equal(1f, offsetStops[3].Offset);

        var edgeMatrix = Assert.NotNull(host.Document?.GetElementById("edge-matrix"));
        var edgeMatrixBox = Assert.NotNull(host.Layout?.FindPrimaryBox(edgeMatrix));
        Assert.Equal(110f, edgeMatrixBox.BorderBox.Width);
        Assert.Equal(50f, edgeMatrixBox.BorderBox.Height);

        Assert.True(borderSides.Any(command => command.Side == 0 && command.Style == (int)BorderStyle.Dotted && command.Color.R > .99f), "dotted border geometry must preserve its style and side color");
        Assert.True(borderSides.Any(command => command.Side == 1 && command.Style == (int)BorderStyle.Dashed && command.Color.G > .99f), "dashed border geometry must preserve its style and side color");
        Assert.True(borderSides.Any(command => command.Side == 2 && command.Style == (int)BorderStyle.Double && command.Color.B > .99f), "double border geometry must preserve its style and side color");
        var currentColor = new ColorRgba(0x12 / 255f, 0x34 / 255f, 0x56 / 255f, 1);
        Assert.True(commands.OfType<StrokeRoundedRectangleCommand>().Any(command => command.Color == currentColor),
            "currentColor must resolve from the element color");

        var hidden = Assert.NotNull(host.Document?.GetElementById("hidden-border"));
        var hiddenBox = Assert.NotNull(host.Layout?.FindPrimaryBox(hidden));
        Assert.Equal(30f, hiddenBox.BorderBox.Width);
        Assert.Equal(20f, hiddenBox.BorderBox.Height);

        var filler = Assert.NotNull(host.Document?.GetElementById("filler"));
        var fillerBox = Assert.NotNull(host.Layout?.FindPrimaryBox(filler));
        Assert.Equal(170f, fillerBox.BorderBox.Height);

        var collapseParent = Assert.NotNull(host.Document?.GetElementById("collapse-parent"));
        var collapseChild = Assert.NotNull(host.Document?.GetElementById("collapse-child"));
        var collapseAfter = Assert.NotNull(host.Document?.GetElementById("collapse-after"));
        var collapseParentBox = Assert.NotNull(host.Layout?.FindPrimaryBox(collapseParent));
        var collapseChildBox = Assert.NotNull(host.Layout?.FindPrimaryBox(collapseChild));
        var collapseAfterBox = Assert.NotNull(host.Layout?.FindPrimaryBox(collapseAfter));
        Assert.Equal(collapseParentBox.BorderBox.Y, collapseChildBox.BorderBox.Y);
        Assert.Equal(10f, collapseParentBox.BorderBox.Height);
        Assert.Equal(collapseParentBox.BorderBox.Bottom + 25f, collapseAfterBox.BorderBox.Y);

        var emptyCollapse = Assert.NotNull(host.Document?.GetElementById("empty-collapse"));
        var emptyCollapseAfter = Assert.NotNull(host.Document?.GetElementById("empty-collapse-after"));
        var emptyCollapseBox = Assert.NotNull(host.Layout?.FindPrimaryBox(emptyCollapse));
        var emptyCollapseAfterBox = Assert.NotNull(host.Layout?.FindPrimaryBox(emptyCollapseAfter));
        Assert.Equal(emptyCollapseBox.BorderBox.Y + 10f, emptyCollapseAfterBox.BorderBox.Y);

        var whitespaceCollapseParent = Assert.NotNull(host.Document?.GetElementById("whitespace-collapse-parent"));
        var whitespaceCollapseChild = Assert.NotNull(host.Document?.GetElementById("whitespace-collapse-child"));
        var whitespaceCollapseParentBox = Assert.NotNull(host.Layout?.FindPrimaryBox(whitespaceCollapseParent));
        var whitespaceCollapseChildBox = Assert.NotNull(host.Layout?.FindPrimaryBox(whitespaceCollapseChild));
        Assert.Equal(whitespaceCollapseParentBox.BorderBox.Y, whitespaceCollapseChildBox.BorderBox.Y);

        var paddedCollapseParent = Assert.NotNull(host.Document?.GetElementById("padded-collapse-parent"));
        var paddedCollapseChild = Assert.NotNull(host.Document?.GetElementById("padded-collapse-child"));
        var paddedCollapseParentBox = Assert.NotNull(host.Layout?.FindPrimaryBox(paddedCollapseParent));
        var paddedCollapseChildBox = Assert.NotNull(host.Layout?.FindPrimaryBox(paddedCollapseChild));
        Assert.Equal(31f, paddedCollapseChildBox.BorderBox.Y - paddedCollapseParentBox.BorderBox.Y);

        var bfcCollapseParent = Assert.NotNull(host.Document?.GetElementById("bfc-collapse-parent"));
        var bfcCollapseChild = Assert.NotNull(host.Document?.GetElementById("bfc-collapse-child"));
        var bfcCollapseParentBox = Assert.NotNull(host.Layout?.FindPrimaryBox(bfcCollapseParent));
        var bfcCollapseChildBox = Assert.NotNull(host.Layout?.FindPrimaryBox(bfcCollapseChild));
        Assert.Equal(30f, bfcCollapseChildBox.BorderBox.Y - bfcCollapseParentBox.BorderBox.Y);

        var recordGrid = Assert.NotNull(host.Document?.GetElementById("record-grid"));
        var recordCard = Assert.NotNull(host.Document?.GetElementById("record-card-a"));
        var recordHistory = Assert.NotNull(host.Document?.GetElementById("record-history"));
        var recordGridBox = Assert.NotNull(host.Layout?.FindPrimaryBox(recordGrid));
        var recordCardBox = Assert.NotNull(host.Layout?.FindPrimaryBox(recordCard));
        var recordHistoryBox = Assert.NotNull(host.Layout?.FindPrimaryBox(recordHistory));
        Assert.Equal(recordCardBox.BorderBox.Height, recordGridBox.BorderBox.Height);
        Assert.Equal(30f, recordHistoryBox.BorderBox.Y - recordGridBox.BorderBox.Bottom);

        var marginFirst = Assert.NotNull(host.Document?.GetElementById("margin-first"));
        var marginSecond = Assert.NotNull(host.Document?.GetElementById("margin-second"));
        var marginFirstBox = Assert.NotNull(host.Layout?.FindPrimaryBox(marginFirst));
        var marginSecondBox = Assert.NotNull(host.Layout?.FindPrimaryBox(marginSecond));
        Assert.Equal(25f, marginSecondBox.BorderBox.Y - marginFirstBox.BorderBox.Bottom);

        var grid = Assert.NotNull(host.Document?.GetElementById("grid"));
        var card = Assert.NotNull(host.Document?.GetElementById("card-a"));
        var gridBox = Assert.NotNull(host.Layout?.FindPrimaryBox(grid));
        var cardBox = Assert.NotNull(host.Layout?.FindPrimaryBox(card));
        Assert.Equal(cardBox.BorderBox.Height, gridBox.BorderBox.Height);

    }

    private static void NativeLayoutPositionsAbsoluteChildrenAndAnonymousFlexText()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              #card { position: relative; width: 320px; padding: 20px; }
              #badge { position: absolute; top: 20px; left: 20px; padding: 6px 12px; }
              #image { width: 320px; height: 200px; display: flex; justify-content: center; align-items: center; }
            </style>
            <body>
              <div id="card"><div id="badge">NEW</div><div id="image">★</div></div>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var card = Assert.NotNull(host.Document?.GetElementById("card"));
        var badge = Assert.NotNull(host.Document?.GetElementById("badge"));
        var image = Assert.NotNull(host.Document?.GetElementById("image"));
        var cardBox = Assert.NotNull(host.Layout?.FindPrimaryBox(card));
        var badgeBox = Assert.NotNull(host.Layout?.FindPrimaryBox(badge));
        var imageBox = Assert.NotNull(host.Layout?.FindPrimaryBox(image));

        Assert.Equal(cardBox.BorderBox.X + 20f, badgeBox.BorderBox.X);
        Assert.Equal(cardBox.BorderBox.Y + 20f, badgeBox.BorderBox.Y);
        if (badgeBox.BorderBox.Width >= 100f)
        {
            throw new InvalidOperationException("An auto-sized absolute box must shrink to its content, not fill its parent.");
        }

        var star = Assert.NotNull(renderer.LastDisplayList?.Commands
            .OfType<DrawTextCommand>()
            .SingleOrDefault(command => command.Text == "★"));
        var textCenter = star.Rectangle.X + star.Rectangle.Width / 2f;
        var imageCenter = imageBox.BorderBox.X + imageBox.BorderBox.Width / 2f;
        if (MathF.Abs(textCenter - imageCenter) >= 1f)
        {
            throw new InvalidOperationException(
                $"Anonymous flex text was not centered: text={textCenter}, container={imageCenter}.");
        }
    }

    private static void NativeTextAlignmentPreservesSemanticsAndControlOverrides()
    {
        const string markup = """
            <style>
              body { margin: 0; }
              .alignment { display: block; width: 120px; }
              button { width: 120px; }
              #button-right { text-align: right; }
            </style>
            <body>
              <div id="align-start" class="alignment" style="text-align: start;">start</div>
              <div id="align-end" class="alignment" style="text-align: end;">end</div>
              <div id="align-left" class="alignment" style="text-align: left;">left</div>
              <div id="align-right" class="alignment" style="text-align: right;">right</div>
              <div id="align-center" class="alignment" style="text-align: center;">center</div>
              <div id="align-justify" class="alignment" style="text-align: justify;">justify</div>
              <button id="button-default">Default</button>
              <button id="button-right">Right</button>
            </body>
            """;

        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(640, 480, 1);
        host.Render();

        var expected = new Dictionary<string, TextAlign>
        {
            ["align-start"] = TextAlign.Start,
            ["align-end"] = TextAlign.End,
            ["align-left"] = TextAlign.Left,
            ["align-right"] = TextAlign.Right,
            ["align-center"] = TextAlign.Center,
            ["align-justify"] = TextAlign.Justify,
        };
        foreach (var (id, alignment) in expected)
        {
            var element = Assert.NotNull(host.Document?.GetElementById(id));
            var box = Assert.NotNull(host.Layout?.FindPrimaryBox(element));
            Assert.Equal(alignment, box.Style.TextAlign);
        }

        var textCommands = renderer.LastDisplayList?.Commands.OfType<DrawTextCommand>().ToArray()
            ?? throw new InvalidOperationException("Native renderer did not produce text commands.");
        var defaultButton = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("button-default"))));
        var rightButton = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("button-right"))));
        var defaultText = Assert.NotNull(textCommands.SingleOrDefault(command => command.Text == "Default"));
        var rightText = Assert.NotNull(textCommands.SingleOrDefault(command => command.Text == "Right"));
        Assert.True(MathF.Abs((defaultText.Rectangle.X + defaultText.Rectangle.Width / 2f) -
            (defaultButton.ContentBox.X + defaultButton.ContentBox.Width / 2f)) < .01f,
            "flow-backed button text must be positioned by the centered fragment line, not a control-only painter override");
        Assert.True(MathF.Abs(rightText.Rectangle.Right - rightButton.ContentBox.Right) < .01f,
            "right-aligned flow-backed button text must use the fragment line position selected by layout");
    }

    private static void ImageResolverDeduplicatesResourcesAndReportsLifecycle()
    {
        const string source = "https://example.test/assets/hero.png";
        var loader = new ControlledImageResourceLoader();
        using var resolver = new ImageResourceResolver(loader);
        var ready = new TaskCompletionSource<ImageResourceChangedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        resolver.Changed += (_, change) => ready.TrySetResult(change);

        resolver.Request(source);
        resolver.Request(source);
        Assert.Equal(1, loader.LoadCount);
        Assert.False(resolver.TryGetDimensions(source, out _));

        loader.Complete(new ImageResourceResult(new ImageDimensions(640, 360), new byte[] { 1, 2, 3 }));
        var readyChange = WaitFor(ready.Task, "image resolver did not publish ready state");
        Assert.Equal(source, readyChange.Source);
        Assert.Equal(ImageResourceState.Ready, readyChange.State);
        Assert.Equal<ImageResourceFailure?>(null, readyChange.Failure);
        Assert.True(resolver.TryGetDimensions(source, out var dimensions));
        Assert.Equal(new ImageDimensions(640, 360), dimensions);

        AssertImageResolverFailure(new UnauthorizedAccessException(), ImageResourceFailure.Policy);
        AssertImageResolverFailure(new InvalidDataException(), ImageResourceFailure.Decode);
        AssertImageResolverFailure(new HttpRequestException(), ImageResourceFailure.Network);
    }

    private static void AssertImageResolverFailure(Exception exception, ImageResourceFailure expectedFailure)
    {
        const string source = "https://example.test/assets/failure.png";
        var loader = new ControlledImageResourceLoader();
        using var resolver = new ImageResourceResolver(loader);
        var failed = new TaskCompletionSource<ImageResourceChangedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        resolver.Changed += (_, change) => failed.TrySetResult(change);

        resolver.Request(source);
        Assert.Equal(1, loader.LoadCount);
        loader.Fail(exception);
        var change = WaitFor(failed.Task, "image resolver did not publish failed state");
        Assert.Equal(ImageResourceState.Failed, change.State);
        Assert.Equal<ImageResourceFailure?>(expectedFailure, change.Failure);
        Assert.False(resolver.TryGetDimensions(source, out _));

        // Failure remains cached so a renderer cannot create an unbounded retry loop every frame.
        resolver.Request(source);
        Assert.Equal(1, loader.LoadCount);
    }

    private static void NativeImageGeometryRebuildsWhenResourceReady()
    {
        const string source = "https://example.test/assets/pattern.png";
        var loader = new ControlledImageResourceLoader();
        using var resolver = new ImageResourceResolver(loader);
        using var renderer = new RecordingRenderer();
        var markup = "<style>body { display:block; margin:0; } #image { display:block; width:200px; height:100px; " +
            "background:url('" + source + "') center / cover no-repeat; }</style><body><div id=\"image\"></div></body>";
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer,
            new UiHostOptions { ImageResolver = resolver });
        host.SetViewport(320, 200, 1);
        host.Render();
        var pending = Assert.NotNull(renderer.LastDisplayList?.Commands.OfType<DrawBackgroundImageCommand>().SingleOrDefault());
        Assert.False(pending.HasResolvedGeometry);
        Assert.Equal(1, loader.LoadCount);

        var ready = new TaskCompletionSource<ImageResourceChangedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        resolver.Changed += (_, change) => ready.TrySetResult(change);
        loader.Complete(new ImageResourceResult(new ImageDimensions(100, 100), new byte[100 * 100 * 4]));
        WaitFor(ready.Task, "image resource did not complete");
        host.Render();
        var resolved = Assert.NotNull(renderer.LastDisplayList?.Commands.OfType<DrawBackgroundImageCommand>().SingleOrDefault());
        Assert.True(resolved.HasResolvedGeometry);
        Assert.NotNull(resolved.Resource);
        Assert.Equal(200f, resolved.DestinationRectangle.Width);
        Assert.Equal(100f, resolved.DestinationRectangle.Height);
    }

    private static void ImageCommandSurvivesHoverRerender()
    {
        // Regression: hovering the launcher tile (a :hover background-color change) rebuilds the
        // display list; the icon's DrawBackgroundImageCommand must keep its resolved geometry and
        // attached resource, otherwise the incremental/full repaint drops the image (gray box).
        const string source = "file:///C:/fake/icon.png";
        var loader = new ControlledImageResourceLoader();
        using var resolver = new ImageResourceResolver(loader);
        using var renderer = new RecordingRenderer();
        var markup = "<style>body { margin: 0; } .app-tile { width: 96px; height: 96px; cursor: pointer; } " +
            ".app-tile:hover { background-color: #eef2f7; } " +
            ".app-icon { width: 56px; height: 56px; background: url('" + source + "') center / cover no-repeat; }" +
            "</style><body><div class=\"app-tile\" id=\"tile\"><div class=\"app-icon\" id=\"icon\"></div></div></body>";
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), new ApproximateTextMeasurer(), renderer,
            new UiHostOptions { ImageResolver = resolver });
        host.SetViewport(320, 200, 1);
        host.Render();
        Assert.Equal(1, loader.LoadCount);

        var ready = new TaskCompletionSource<ImageResourceChangedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        resolver.Changed += (_, change) => ready.TrySetResult(change);
        loader.Complete(new ImageResourceResult(new ImageDimensions(56, 56), new byte[56 * 56 * 4]));
        WaitFor(ready.Task, "image resource did not complete");
        host.Render();

        var before = renderer.LastDisplayList?.Commands.OfType<DrawBackgroundImageCommand>().SingleOrDefault()
            ?? throw new InvalidOperationException("before hover: no DrawBackgroundImageCommand in display list");
        Assert.True(before.HasResolvedGeometry);
        Assert.NotNull(before.Resource);

        var tile = Assert.NotNull(host.Layout?.FindPrimaryBox(Assert.NotNull(host.Document?.GetElementById("tile"))));
        var center = new PointF(tile.BorderBox.X + tile.BorderBox.Width / 2, tile.BorderBox.Y + tile.BorderBox.Height / 2);
        host.PointerMoved(center);
        host.Render();

        var after = renderer.LastDisplayList?.Commands.OfType<DrawBackgroundImageCommand>().SingleOrDefault()
            ?? throw new InvalidOperationException(
                "after hover: no DrawBackgroundImageCommand in display list. Commands=" +
                string.Join("|", (renderer.LastDisplayList?.Commands ?? []).Select(c => c.GetType().Name)) +
                $" metrics=[styleRes={host.Metrics.StyleResolutions} dlBuilds={host.Metrics.DisplayListBuilds} pcBuilds={host.Metrics.PaintChunkBuilds}]");
        Assert.True(after.HasResolvedGeometry, "image command lost resolved geometry after hover re-render");
        Assert.NotNull(after.Resource);
        Assert.Equal(before.DestinationRectangle, after.DestinationRectangle);
    }

    private static T WaitFor<T>(Task<T> task, string message)
    {
        if (!task.Wait(TimeSpan.FromSeconds(2)))
        {
            throw new InvalidOperationException(message);
        }
        return task.GetAwaiter().GetResult();
    }

    // Minimal 1x1 opaque red PNG (RGBA 255,0,0,255). Same literal as the native
    // aether.image-decode test; verified against an independent decoder.
    private static readonly byte[] RedPngBytes =
    [
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54,
        0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F,
        0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
        0xAE, 0x42, 0x60, 0x82,
    ];

    private static void FileImageResourceLoader_DecodesFilePng()
    {
        var path = Path.Combine(Path.GetTempPath(), "aetherui-red-" + Guid.NewGuid().ToString("N") + ".png");
        File.WriteAllBytes(path, RedPngBytes);
        try
        {
            var result = new FileImageResourceLoader()
                .LoadAsync("file:///" + path.Replace('\\', '/'), CancellationToken.None).AsTask().GetAwaiter().GetResult();
            Assert.Equal(new ImageDimensions(1, 1), result.Dimensions);
            AssertPixel(result, 0, 0, 255, 255);
        }
        finally
        {
            File.Delete(path);
        }
    }

    private static void FileImageResourceLoader_RejectsHttp()
    {
        var loader = new FileImageResourceLoader();
        AssertThrows<UnauthorizedAccessException>(() => loader.LoadAsync("http://example.test/x.png", CancellationToken.None).AsTask());
    }

    private static void DataImageResourceLoader_DecodesDataUri()
    {
        var dataUri = "data:image/png;base64," + Convert.ToBase64String(RedPngBytes);
        var result = new DataImageResourceLoader().LoadAsync(dataUri, CancellationToken.None).AsTask().GetAwaiter().GetResult();
        Assert.Equal(new ImageDimensions(1, 1), result.Dimensions);
        AssertPixel(result, 0, 0, 255, 255);
    }

    private static void HttpImageResourceLoader_RejectsFilePolicy()
    {
        var loader = new HttpImageResourceLoader();
        AssertThrows<UnauthorizedAccessException>(() => loader.LoadAsync("file:///C:/x.png", CancellationToken.None).AsTask());
    }

    private static void CompositeImageResourceLoader_FallsThroughSchemes()
    {
        var path = Path.Combine(Path.GetTempPath(), "aetherui-red-" + Guid.NewGuid().ToString("N") + ".png");
        File.WriteAllBytes(path, RedPngBytes);
        try
        {
            var composite = new CompositeImageResourceLoader(new FileImageResourceLoader(), new DataImageResourceLoader());

            var fileResult = composite.LoadAsync("file:///" + path.Replace('\\', '/'), CancellationToken.None).AsTask().GetAwaiter().GetResult();
            Assert.Equal(new ImageDimensions(1, 1), fileResult.Dimensions);
            AssertPixel(fileResult, 0, 0, 255, 255);

            var dataResult = composite.LoadAsync("data:image/png;base64," + Convert.ToBase64String(RedPngBytes), CancellationToken.None).AsTask().GetAwaiter().GetResult();
            Assert.Equal(new ImageDimensions(1, 1), dataResult.Dimensions);
            AssertPixel(dataResult, 0, 0, 255, 255);

            AssertThrows<UnauthorizedAccessException>(() => composite.LoadAsync("http://example.test/x.png", CancellationToken.None).AsTask());
        }
        finally
        {
            File.Delete(path);
        }
    }

    private static void NativeImageDecode_RejectsGarbage()
    {
        Assert.False(NativeImageDecode.TryDecode(new byte[] { 1, 2, 3, 4 }, out _, out _, out _));
    }

    private static void AssertPixel(ImageResourceResult result, byte blue, byte green, byte red, byte alpha)
    {
        Assert.Equal(4, result.Pixels.Length);
        Assert.Equal(blue, result.Pixels.Span[0]);
        Assert.Equal(green, result.Pixels.Span[1]);
        Assert.Equal(red, result.Pixels.Span[2]);
        Assert.Equal(alpha, result.Pixels.Span[3]);
    }

    private static void AssertThrows<TException>(Func<Task> action) where TException : Exception
    {
        try { action().GetAwaiter().GetResult(); }
        catch (TException) { return; }
        throw new InvalidOperationException($"Se esperaba {typeof(TException).Name}.");
    }

    private static void TransitionsAnimateOpacityOnStateChange()
    {
        // A state-triggered class change (button -> StateHasChanged rebuild) must start an opacity
        // transition when the style declares `transition: opacity .3s linear`, animate through the
        // display list's fill alpha, and paint the exact end value on completion.
        var component = new TransitionToggleComponent(withTransition: true);
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var box = Assert.NotNull(host.Document?.GetElementById("box"));
        var boxRect = Assert.NotNull(host.Layout?.FindPrimaryBox(box)).BorderBox;
        Assert.True(BoxFillAlpha(renderer, boxRect) >= .99f, "initial render must be fully opaque");

        var beforeMs = Environment.TickCount64;
        component.Toggle();
        host.Render();

        Assert.True(host.HasActiveTransitions, "observe must start an opacity transition after the .hidden toggle");

        host.TickTransitions(beforeMs + 150);
        host.Render();
        var halfAlpha = BoxFillAlpha(renderer, boxRect);
        Assert.True(MathF.Abs(halfAlpha - .5f) < .15f,
            $"linear opacity at 150ms/300ms should be ~0.5, got {halfAlpha:F2}");
        Assert.True(host.HasActiveTransitions, "transition must still be active halfway through");

        host.TickTransitions(beforeMs + 400);
        host.Render();
        Assert.True(!host.HasActiveTransitions, "transition must finish once the elapsed time exceeds the duration");
        Assert.True(!HasVisibleBoxFill(renderer, boxRect),
            "completed transition must paint the end value (opacity 0): the box fill must be fully transparent and omitted from the display list");
    }

    private static void TransitionsDoNotAnimateWithoutTransitionProperty()
    {
        // Same class toggle without any `transition` declaration must be instant: the runtime never
        // observes an active transition and the new opacity reaches the paint immediately.
        var component = new TransitionToggleComponent(withTransition: false);
        using var renderer = new RecordingRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
        host.SetViewport(320, 200, 1);
        host.Render();

        var box = Assert.NotNull(host.Document?.GetElementById("box"));
        var boxRect = Assert.NotNull(host.Layout?.FindPrimaryBox(box)).BorderBox;
        Assert.True(BoxFillAlpha(renderer, boxRect) >= .99f);

        component.Toggle();
        host.Render();

        Assert.True(!host.HasActiveTransitions, "a style without transition-property/duration must not start a transition");
        Assert.True(!HasVisibleBoxFill(renderer, boxRect), "the opacity change must be applied instantly (box fill fully transparent)");
    }

    private static float BoxFillAlpha(RecordingRenderer renderer, RectF rect)
    {
        var fills = renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
                .Where(fill => fill.Rectangle == rect && fill.Color.R > .5f && fill.Color.G < .5f && fill.Color.B < .5f)
                .ToArray()
            ?? throw new InvalidOperationException("Native renderer did not produce a display list for the transition box.");
        if (fills.Length != 1)
        {
            throw new InvalidOperationException($"Expected exactly one red box fill, got {fills.Length}: " +
                string.Join("|", fills.Select(fill => $"({fill.Rectangle},{fill.Color.R:F2},{fill.Color.A:F2})")));
        }

        return fills[0].Color.A;
    }

    private static bool HasVisibleBoxFill(RecordingRenderer renderer, RectF rect)
    {
        var visible = renderer.LastDisplayList?.Commands.OfType<FillRoundedRectangleCommand>()
            .Where(fill => fill.Rectangle == rect && fill.Color.R > .5f && fill.Color.G < .5f && fill.Color.B < .5f && fill.Color.A >= .05f)
            .ToArray() ?? [];
        return visible.Length > 0;
    }

    private sealed class TransitionToggleComponent(bool withTransition) : UiComponent
    {
        public bool Hidden { get; private set; }

        public void Toggle()
        {
            Hidden = !Hidden;
            StateHasChanged();
        }

        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style>body { margin: 0; } #box { width: 100px; height: 100px; background: red; ");
            writer.WriteLiteral(withTransition ? "transition-property: opacity; transition-duration: .3s; transition-timing-function: linear; " : string.Empty);
            writer.WriteLiteral("} .hidden { opacity: 0; }</style><body><div id=\"box\" class=\"");
            writer.Write(Hidden ? "hidden" : string.Empty);
            writer.WriteLiteral("\"></div></body>");
        }
    }

    private sealed class NestedSolidBackgroundComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("""
                <style>
                  body { margin: 0; }
                  #box { width: 200px; height: 100px; background: #1e293b; }
                </style>
                <body>
                  <div id="box"></div>
                </body>
                """);
        }
    }

    private sealed class GradientLogoComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("""
                <style>
                  #logo {
                    background: linear-gradient(to right, #60a5fa, #a78bfa);
                    -webkit-background-clip: text;
                    -webkit-text-fill-color: transparent;
                  }
                </style>
                <body>
                  <div id="logo">AetherUI</div>
                </body>
                """);
        }
    }

    private sealed class RebuildComponent : UiComponent
    {
        private bool _toggled;

        public void Toggle()
        {
            _toggled = true;
            StateHasChanged();
        }

        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral(_toggled
                ? "<body><div id=\"after\">replacement</div></body>"
                : "<body><div id=\"before\">initial</div></body>");
        }
    }

    private sealed class BodyGradientComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral("""
            <html><head><style>body { margin: 0; background: linear-gradient(45deg, #0f172a, #1e293b, #334155); }</style></head>
            <body><div>gradient body</div></body></html>
            """);
    }

    private sealed class BodyVariableGradientComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral("""
            <html><head><style>
              :root { --bg-gradient: linear-gradient(135deg, #667eea 0%, #764ba2 100%); }
              body { margin: 0; background: var(--bg-gradient); }
            </style></head>
            <body><div>gradient body</div></body></html>
            """);
    }

    private sealed class BodySolidBackgroundComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral("""
            <html><head><style>body { margin: 0; min-height: 100%; background-color: #0f172a; }</style></head>
            <body><div>solid body</div></body></html>
            """);
    }

    private sealed class RawMarkupComponent(string markup) : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(markup);
    }

    private sealed class ScrollbarInteractionComponent : UiComponent
    {
        public int ClickCount { get; private set; }

        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("""
                <style>
                  body { margin: 0; }
                  #outer { width: 120px; height: 120px; overflow: auto; }
                  #inner { width: 300px; height: 100px; overflow: auto; }
                  #content { width: 600px; height: 300px; }
                </style>
                <body><div id="outer"><div id="inner"
                """);
            writer.WriteEventAttribute("onclick", _ => ClickCount++);
            writer.WriteLiteral("><div id=\"content\"></div></div></div></body>");
        }
    }

    private sealed class ControlledImageResourceLoader : IImageResourceLoader
    {
        private readonly TaskCompletionSource<ImageResourceResult> _completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public int LoadCount { get; private set; }

        public ValueTask<ImageResourceResult> LoadAsync(string source, CancellationToken cancellationToken)
        {
            LoadCount++;
            return new ValueTask<ImageResourceResult>(_completion.Task);
        }

        public void Complete(ImageResourceResult result) => _completion.TrySetResult(result);
        public void Fail(Exception exception) => _completion.TrySetException(exception);
    }

    private sealed class NegativeMarginComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("""
                <style>
                  body { margin: 0; }
                  #a { width: 100px; height: 50px; background: red; box-shadow: 0 2px 4px rgba(0,0,0,.2); }
                  #b { width: 100px; height: 50px; margin-top: -20px; background: blue; }
                  #c { width: 100px; height: 50px; box-shadow: inset 0 2px 4px rgba(0,0,0,.2); }
                </style>
                <body>
                  <div id="a"></div>
                  <div id="b"></div>
                  <div id="c"></div>
                </body>
                """);
        }
    }

    private sealed class CascadeTestComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("""
                <style>
                  button { color: red; background-color: white; }
                  #send:hover { color: blue; }
                </style>
                <body><button id="send">Enviar</button></body>
                """);
        }
    }

    private sealed class LayoutTestComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("""
                <style>body { margin: 0; } button { width: 180px; height: 42px; }</style>
                <body><button id="send">Enviar</button></body>
                """);
        }
    }

    private sealed class RecordingRenderer : IRenderBackend
    {
        public int RenderCount { get; private set; }
        public DisplayList? LastDisplayList { get; private set; }

        public void Resize(int pixelWidth, int pixelHeight, float dpiScale)
        {
        }

        public void Render(DisplayList displayList, ColorRgba clearColor)
        {
            ArgumentNullException.ThrowIfNull(displayList);
            RenderCount++;
            LastDisplayList = displayList;
        }

        public void Dispose()
        {
        }
    }

    private sealed class ControlledIncrementalRenderer : IRenderBackend, IIncrementalRenderBackend
    {
        private bool _hasFullFrame;

        public int RenderCount { get; private set; }
        public int IncrementalAttempts { get; private set; }
        public bool LastScrollCompositionMode { get; private set; }
        public DisplayList? LastDisplayList { get; private set; }

        public void Resize(int pixelWidth, int pixelHeight, float dpiScale)
        {
        }

        public void Render(DisplayList displayList, ColorRgba clearColor)
        {
            ArgumentNullException.ThrowIfNull(displayList);
            RenderCount++;
            LastDisplayList = displayList;
            _hasFullFrame = true;
        }

        public bool TryRenderIncremental(DisplayList displayList, ColorRgba clearColor)
        {
            ArgumentNullException.ThrowIfNull(displayList);
            IncrementalAttempts++;
            if (!_hasFullFrame)
            {
                return false;
            }

            LastDisplayList = displayList;
            return true;
        }

        public void SetScrollCompositionMode(bool enabled) => LastScrollCompositionMode = enabled;

        public void ResetCounts()
        {
            RenderCount = 0;
            IncrementalAttempts = 0;
        }

        public void Dispose()
        {
        }
    }

    private static class Assert
    {
        public static void True(bool condition, string? message = null)
        {
            if (!condition)
            {
                throw new InvalidOperationException(message ?? "Se esperaba true.");
            }
        }

        public static void False(bool condition) => True(!condition);

        public static void Equal<T>(T expected, T actual)
        {
            if (!EqualityComparer<T>.Default.Equals(expected, actual))
            {
                throw new InvalidOperationException($"Esperado: {expected}; actual: {actual}.");
            }
        }

        public static T NotNull<T>(T? value) where T : class =>
            value ?? throw new InvalidOperationException("Se esperaba un valor no nulo.");

        public static void Same(object expected, object? actual)
        {
            if (!ReferenceEquals(expected, actual))
            {
                throw new InvalidOperationException("Se esperaba la misma referencia.");
            }
        }

        public static void Throws<TException>(Action action) where TException : Exception
        {
            try { action(); }
            catch (TException) { return; }
            throw new InvalidOperationException($"Se esperaba {typeof(TException).Name}.");
        }
    }
}
