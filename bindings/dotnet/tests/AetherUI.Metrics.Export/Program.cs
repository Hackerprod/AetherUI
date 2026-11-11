using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

namespace AetherUI.Metrics.Export;

// This exact source is compiled against BOTH real Core checkouts. Only the pre-v2 public API
// is used. Canonical output deliberately excludes wall time, process details and raw identities.
internal static class Program
{
    private const string Markup = "<body>"
        + "<div id=\"first\" style=\"width:120px;height:40px;background:#123456\">Alpha</div>"
        + "<div id=\"second\" style=\"width:160px;height:50px;background:#654321\">Beta</div>"
        + "</body>";
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static readonly List<object> SerializationChecks = [];

    private static int Main(string[] args)
    {
        if (args.Length != 4 || args[0] != "--output" || args[2] != "--native") return 2;
        try
        {
            var result = Run(args[3]);
            using var output = new FileStream(args[1], FileMode.CreateNew, FileAccess.Write);
            JsonSerializer.Serialize(output, result, JsonOptions);
            Console.WriteLine("Four canonical host states exported; no performance verdict.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    private static object Run(string expectedNative)
    {
        Require(OperatingSystem.IsWindows() && Environment.Is64BitProcess, "Windows x64 required");
        using var component = new FixtureComponent();
        var renderer = new MemoryRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer,
            new UiHostOptions { Width = 640, Height = 480 });
        host.SetViewport(640, 480, 1);
        var states = new List<JsonElement>();
        host.Render();
        states.Add(Snapshot("first", host, renderer));
        host.Render();
        states.Add(Snapshot("unchanged", host, renderer));
        host.SetViewport(800, 480, 1);
        host.Render();
        states.Add(Snapshot("resized", host, renderer));
        host.ResetMetrics(); // Serialized on this sole host owner thread; explicit epoch boundary.
        host.Render();
        states.Add(Snapshot("reset-valid-tree", host, renderer));

        using var process = Process.GetCurrentProcess();
        var modules = process.Modules.Cast<ProcessModule>().Where(module =>
            string.Equals(module.ModuleName, "aether_core.dll", StringComparison.OrdinalIgnoreCase)).ToArray();
        Require(modules.Length == 1, "one loaded native module");
        var loadedPath = Path.GetFullPath(modules[0].FileName);
        Require(loadedPath.Equals(Path.Combine(AppContext.BaseDirectory, "aether_core.dll"),
            StringComparison.OrdinalIgnoreCase), "native loaded from this arm output");
        var nativeHash = Sha256(loadedPath);
        Require(nativeHash == Sha256(expectedNative), "native matches freshly built shared engine");
        return new
        {
            schema = "managed-results-export-v1", status = "passed",
            serializationChecks = SerializationChecks,
            inputSha256 = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Markup))).ToLowerInvariant(),
            loadedNative = new { file = "aether_core.dll", sha256 = nativeHash },
            assemblies = new[] { Identity(typeof(Program).Assembly.Location),
                Identity(typeof(UiDocumentHost).Assembly.Location), Identity(typeof(NativeStyleImpact).Assembly.Location) },
            canonical = new { markup = Markup, textMeasurer = "ApproximateTextMeasurer", states },
        };
    }

    private static JsonElement Snapshot(string phase, UiDocumentHost host, MemoryRenderer renderer)
    {
        var document = host.Document ?? throw new InvalidOperationException("Missing document");
        var layout = host.Layout ?? throw new InvalidOperationException("Missing layout");
        Require(renderer.Commands.Length > 0, "nonempty actual backend output");
        foreach (var id in new[] { "first", "second" })
        {
            var element = document.GetElementById(id);
            Require(element is not null, "expected fixture element");
            Require(layout.FindPrimaryBox(element!) is not null, "expected fixture primary box");
        }
        var nodes = new[] { (Node)document }.Concat(document.Descendants()).ToArray();
        var nodeIndices = nodes.Select((node, index) => (node, index)).ToDictionary(x => x.node, x => x.index);
        var boxes = layout.Root.DescendantsAndSelf().ToArray();
        var boxIndices = boxes.Select((box, index) => (box, index)).ToDictionary(x => x.box, x => x.index);
        // Native style IDs are NOT exported. Stable first occurrence in the ordered chunk stream
        // defines a logical identity; repeated IDs preserve aliasing across chunks and commands.
        var styleIds = new Dictionary<long, int>();
        int LogicalStyle(long id)
        {
            if (!styleIds.TryGetValue(id, out var index)) styleIds.Add(id, index = styleIds.Count);
            return index;
        }
        var chunks = renderer.Chunks.Select(chunk => new { owner = LogicalStyle(chunk.StyleNodeId),
            chunk.Start, chunk.End, chunk.Bounds, chunk.Depth }).ToArray();
        object Command(PaintCommand command) => command switch
        {
            PushScrollOffsetCommand scroll => new { kind = nameof(PushScrollOffsetCommand), owner = LogicalStyle(scroll.ScrollContainerId) },
            DrawScrollbarChromeCommand scroll => new { kind = nameof(DrawScrollbarChromeCommand), owner = LogicalStyle(scroll.ScrollContainerId) },
            // The fixed fixture has no external resources. Reject unexpected resources instead
            // of serializing provider/handle-bearing data or silently dropping an active value.
            DrawBackgroundImageCommand image when image.Resource is not null =>
                throw new InvalidOperationException("Unexpected resource in public fixed fixture"),
            FillRoundedRectangleCommand or DrawBoxShadowCommand or FillRoundedRectangleGradientCommand or
            StrokeRoundedRectangleCommand or FillBorderSideCommand or DrawTextCommand or DrawTextGradientCommand or
            DrawTextShadowCommand or ApplyBackdropFilterCommand or DrawBackgroundImageCommand or
            PushClipCommand or PopClipCommand or PushTransformCommand or PopTransformCommand or PopScrollOffsetCommand =>
                new { kind = command.GetType().Name, values = JsonSerializer.SerializeToElement(command, command.GetType()) },
            _ => throw new InvalidOperationException("Unsupported active command type"),
        };
        var metrics = host.Metrics;
        var snapshot = JsonSerializer.SerializeToElement(new
        {
            phase, layout.Viewport, layout.ViewportOverflowX, layout.ViewportOverflowY, host.DpiScale,
            documentVersion = document.Version,
            dom = nodes.Select((node, index) => new { index, kind = node.NodeType,
                parent = node.Parent is null ? (int?)null : nodeIndices[node.Parent],
                children = node.Children.Select(child => nodeIndices[child]).ToArray(), node.TextContent,
                comment = (node as CommentNode)?.Text,
                element = node is ElementNode element ? new { element.TagName, element.Id,
                    attributes = element.Attributes.OrderBy(pair => pair.Key, StringComparer.Ordinal)
                        .Select(pair => new { pair.Key, pair.Value }).ToArray(), element.State, element.RuntimeValue,
                    primaryBox = layout.FindPrimaryBox(element) is { } primary ? (int?)boxIndices[primary] : null,
                    paintBoxes = layout.FindPaintBoxes(element).Select(box => boxIndices[box]).ToArray() } : null }).ToArray(),
            boxes = boxes.Select((box, index) => new { index, element = nodeIndices[box.Element], box.Kind,
                children = box.Children.Select(child => boxIndices[child]).ToArray(), box.Text,
                box.MarginBox, box.BorderBox, box.PaddingBox, box.ContentBox, box.IsAtomicControl,
                box.Scroll, box.Scrollbar, box.Style }).ToArray(),
            paintOrder = layout.PaintOrder.Select(box => boxIndices[box]).ToArray(),
            commands = renderer.Commands.Select(Command).ToArray(), chunks, renderer.ClearColor,
            counts = new { metrics.StyleResolutions, metrics.StyleUpdatedElements, metrics.LayoutBuilds,
                metrics.DisplayListBuilds, metrics.PaintChunkBuilds, metrics.ScrollCompositions, metrics.RenderCalls,
                metrics.IncrementalRenderCalls, metrics.VisualStyleMapBuilds, metrics.VisualStyleMapUpdates,
                metrics.LastStyleImpact, backendRenderCalls = renderer.RenderCalls },
        });
        // Check concrete serialized values against the real objects, before retaining this state.
        // This guards the exporter itself, not merely the downstream JSON comparator.
        var fills = 0;
        var texts = 0;
        var commandsJson = snapshot.GetProperty("commands");
        for (var index = 0; index < renderer.Commands.Length; index++)
        {
            if (renderer.Commands[index] is FillRoundedRectangleCommand fill)
            {
                var values = commandsJson[index].GetProperty("values");
                Require(values.GetProperty("Rectangle").GetProperty("Width").GetSingle() == fill.Rectangle.Width,
                    "serialized fill width equals active command value");
                Require(values.GetProperty("Color").GetProperty("R").GetSingle() == fill.Color.R,
                    "serialized fill red equals active command value");
                fills++;
            }
            if (renderer.Commands[index] is DrawTextCommand text)
            {
                var values = commandsJson[index].GetProperty("values");
                Require(values.GetProperty("Text").GetString() == text.Text,
                    "serialized text equals active command value");
                Require(values.GetProperty("Rectangle").GetProperty("Width").GetSingle() == text.Rectangle.Width,
                    "serialized text width equals active command value");
                texts++;
            }
        }
        Require(fills > 0 && texts > 0, "fixed fixture must exercise actual fill and text command values");
        var boxesJson = snapshot.GetProperty("boxes");
        for (var index = 0; index < boxes.Length; index++)
        {
            Require(boxesJson[index].GetProperty("BorderBox").GetProperty("Width").GetSingle() == boxes[index].BorderBox.Width
                && boxesJson[index].GetProperty("ContentBox").GetProperty("Height").GetSingle() == boxes[index].ContentBox.Height,
                "serialized layout geometry equals actual projected box values");
        }
        SerializationChecks.Add(new { phase, passed = true, fillCommandsVerified = fills,
            textCommandsVerified = texts, geometryBoxesVerified = boxes.Length });
        return snapshot;
    }

    private static void Require(bool condition, string name)
    {
        if (!condition) throw new InvalidOperationException(name);
    }
    private static object Identity(string path) => new { file = Path.GetFileName(path), sha256 = Sha256(path) };
    private static string Sha256(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }
    private sealed class FixtureComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(Markup);
    }
    private sealed class MemoryRenderer : IRenderBackend
    {
        public PaintCommand[] Commands { get; private set; } = [];
        public PaintChunk[] Chunks { get; private set; } = [];
        public ColorRgba ClearColor { get; private set; }
        public int RenderCalls { get; private set; }
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList list, ColorRgba clearColor)
        {
            Commands = list.Commands.ToArray();
            Chunks = list.PaintChunks.ToArray();
            ClearColor = clearColor;
            RenderCalls++;
        }
        public void Dispose() { }
    }
}
