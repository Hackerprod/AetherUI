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
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;

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
        ("Image resolver deduplicates resources and reports lifecycle", ImageResolverDeduplicatesResourcesAndReportsLifecycle),
        ("Native image geometry rebuilds when a resource becomes ready", NativeImageGeometryRebuildsWhenResourceReady),
        ("Native table layout shares automatic column widths across rows", NativeTableLayoutSharesAutomaticColumns),
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

        host.MouseWheelScrolled(new PointF(12, 12), -2);
        host.Render();

        var scrolled = Assert.NotNull(host.Layout?.FindPrimaryBox(body));
        Assert.True(scrolled.ScrollOffsetY > 0);
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
        var offsetGradient = Assert.NotNull(host.Document?.GetElementById("offset-gradient"));
        var offsetGradientBox = Assert.NotNull(host.Layout?.FindPrimaryBox(offsetGradient));
        Assert.True(offsetGradientBox.Style.BackgroundGradient is { }, "gradient shorthand must reach the computed style");
        var offsetStops = offsetGradientBox.Style.BackgroundGradient!.Value.Stops;
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

    private static T WaitFor<T>(Task<T> task, string message)
    {
        if (!task.Wait(TimeSpan.FromSeconds(2)))
        {
            throw new InvalidOperationException(message);
        }
        return task.GetAwaiter().GetResult();
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
