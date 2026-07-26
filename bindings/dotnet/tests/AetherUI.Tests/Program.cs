using System.Net;
using AetherUI.Css;
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
        ("Native style runtime replaces DOM on markup rebuild", NativeStyleRuntimeRebuildReplacesDom),
        ("Native style runtime paints body shorthand gradients", NativeStyleRuntimePaintsBodyShorthandGradient),
        ("Native style runtime emits body solid background", NativeStyleRuntimeEmitsBodySolidBackground),
        ("Native style runtime emits nested element background", NativeStyleRuntimeEmitsNestedElementBackground),
        ("Native pipeline renders dashboard fixture", NativePipelineRendersDashboardFixture),
        ("Native flex layout preserves nested content width", NativeFlexLayoutPreservesNestedContentWidth),
        ("Native root scrolls overflowing content", NativeRootScrollsOverflowingContent),
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
        public static void True(bool condition)
        {
            if (!condition)
            {
                throw new InvalidOperationException("Se esperaba true.");
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
    }
}
