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
        var document = HtmlParser.Parse("""
            <style>
              button { color: red; background-color: white; }
              #send:hover { color: blue; }
            </style>
            <body><button id="send">Enviar</button></body>
            """);
        var button = Assert.NotNull(document.GetElementById("send"));
        var normal = StyleResolver.Resolve(document)[button];
        Assert.True(normal.Color.R > normal.Color.B);

        button.State |= ElementState.Hover;
        var hovered = StyleResolver.Resolve(document)[button];
        Assert.True(hovered.Color.B > hovered.Color.R);
    }

    private static void LayoutProducesHitTargets()
    {
        var document = HtmlParser.Parse("""
            <style>body { margin: 0; } button { width: 180px; height: 42px; }</style>
            <body><button id="send">Enviar</button></body>
            """);
        var styles = StyleResolver.Resolve(document, 640, 480);
        var result = new LayoutEngine(new ApproximateTextMeasurer()).Layout(document, styles, new SizeF(640, 480));
        var button = Assert.NotNull(document.GetElementById("send"));
        var box = Assert.NotNull(result.FindPrimaryBox(button));
        var center = new PointF(box.BorderBox.X + box.BorderBox.Width / 2, box.BorderBox.Y + box.BorderBox.Height / 2);
        Assert.Same(button, result.HitTest(center));
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
        Click(host, "submit");
        Assert.Equal(1, component.SubmitCount);
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

    private static void AssertGeneratedSourceCompiles(string source)
    {
        var trustedAssemblies = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string
            ?? throw new InvalidOperationException("No se pudo obtener TRUSTED_PLATFORM_ASSEMBLIES.");
        var references = trustedAssemblies
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
            .Select(static path => MetadataReference.CreateFromFile(path))
            .ToList();
        references.Add(MetadataReference.CreateFromFile(typeof(UiComponent).Assembly.Location));

        var syntaxTree = CSharpSyntaxTree.ParseText(
            source,
            new CSharpParseOptions(LanguageVersion.Latest),
            path: "DemoView.AetherUI.g.cs");
        var compilation = CSharpCompilation.Create(
            "AetherUI.GeneratedView.Tests",
            [syntaxTree],
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

    private sealed class RecordingRenderer : IRenderBackend
    {
        public int RenderCount { get; private set; }

        public void Resize(int pixelWidth, int pixelHeight, float dpiScale)
        {
        }

        public void Render(DisplayList displayList, ColorRgba clearColor)
        {
            ArgumentNullException.ThrowIfNull(displayList);
            RenderCount++;
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
