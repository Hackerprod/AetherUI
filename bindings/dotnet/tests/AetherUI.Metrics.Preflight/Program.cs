using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using AetherUI.Core.Native;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

namespace AetherUI.Metrics.Preflight;

internal static class Program
{
    // Public, fixed ASCII input. No resources, interaction, animation or pseudo rules.
    // The layout root is body (or the first document element), so both boxes need a common root.
    private const string Markup = "<body>"
        + "<div id=\"first\" style=\"width:120px;height:40px;background:#123456\">Alpha</div>"
        + "<div id=\"second\" style=\"width:160px;height:50px;background:#654321\">Beta</div>"
        + "</body>";
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    private static int Main(string[] args)
    {
        if (args.Length != 2 || args[0] != "--output")
        {
            Console.Error.WriteLine("Expected --output <preflight.json>.");
            return 2;
        }

        var checks = new List<object>();
        object result;
        var exitCode = 0;
        try
        {
            result = Run(checks);
        }
        catch (Exception error)
        {
            // Only a type and our fixed check labels enter the public artifact.
            // The complete exception belongs to this fixture's Actions stderr.
            result = new { schema = "managed-preparation-v1", status = "failed", checks,
                exceptionType = error.GetType().FullName };
            Console.Error.WriteLine(error);
            exitCode = 1;
        }

        // An I/O failure must also leave a nonzero process exit, never a false PASS.
        File.WriteAllText(args[1], JsonSerializer.Serialize(result, JsonOptions) + "\n");
        Console.WriteLine(exitCode == 0 ? "Headless preflight completed." : "Headless preflight failed.");
        return exitCode;
    }

    private static object Run(List<object> checks)
    {
        void Check(string name, bool passed)
        {
            checks.Add(new { name, passed });
            if (!passed) throw new InvalidOperationException($"Preflight check failed: {name}.");
        }

        Check("windows-x64-process", OperatingSystem.IsWindows()
            && RuntimeInformation.ProcessArchitecture == Architecture.X64);
        var root = Path.GetFullPath(Directory.GetCurrentDirectory());
        using var manifest = JsonDocument.Parse(File.ReadAllText(
            Path.Combine(root, "artifacts/validator-metrics/evidence/manifest.json")));
        var source = manifest.RootElement.GetProperty("source").Clone();
        var build = manifest.RootElement.GetProperty("build").Clone();
        var toolchain = manifest.RootElement.GetProperty("toolchain").Clone();
        var expectedOutput = Path.GetFullPath(Path.Combine(root, "artifacts/validator-metrics/preflight-bin"));
        Check("expected-managed-output", Path.TrimEndingDirectorySeparator(AppContext.BaseDirectory)
            .Equals(expectedOutput, StringComparison.OrdinalIgnoreCase));

        using var component = new FixtureComponent();
        var renderer = new MemoryRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer,
            new UiHostOptions { Width = 640, Height = 480 });
        host.SetViewport(640, 480, 1);
        host.Render(); // Exactly one render. No reset, retries or candidate metrics.

        Check("document-present", host.Document is not null);
        Check("layout-present", host.Layout is not null);
        var boxes = new List<object>();
        foreach (var id in new[] { "first", "second" })
        {
            var element = host.Document!.GetElementById(id);
            Check($"{id}-present", element is not null);
            var box = host.Layout!.FindPrimaryBox(element!);
            Check($"{id}-primary-box", box is not null);
            var rect = box!.BorderBox;
            Check($"{id}-finite-nonnegative-box", float.IsFinite(rect.X) && rect.X >= 0
                && float.IsFinite(rect.Y) && rect.Y >= 0
                && float.IsFinite(rect.Width) && rect.Width >= 0
                && float.IsFinite(rect.Height) && rect.Height >= 0);
            boxes.Add(new { id, rect.X, rect.Y, rect.Width, rect.Height });
        }

        var metrics = host.Metrics;
        Check("one-layout-build", metrics.LayoutBuilds == 1);
        Check("one-host-render", metrics.RenderCalls == 1);
        Check("one-backend-render", renderer.RenderCalls == 1);
        Check("nonempty-display-list", renderer.Commands.Length > 0);
        Check("viewport-and-dpi", host.Layout!.Viewport == new SizeF(640, 480) && host.DpiScale == 1);

        // Inspect only the requested native module of this process, not a module inventory.
        using var process = Process.GetCurrentProcess();
        var modules = process.Modules.Cast<ProcessModule>().Where(module =>
            string.Equals(module.ModuleName, "aether_core.dll", StringComparison.OrdinalIgnoreCase)).ToArray();
        Check("one-loaded-aether-core", modules.Length == 1);
        var loadedPath = Path.GetFullPath(modules[0].FileName);
        var expectedNativePath = Path.Combine(expectedOutput, "aether_core.dll");
        Check("loaded-native-from-expected-output", loadedPath.Equals(expectedNativePath,
            StringComparison.OrdinalIgnoreCase));
        var installedPath = Path.Combine(root, "artifacts/native/win-x64/bin/aether_core.dll");
        var loadedHash = Sha256(loadedPath);
        var installedHash = Sha256(installedPath);
        Check("loaded-native-matches-installed", loadedHash == installedHash);
        Check("installed-native-matches-build", installedHash == build.GetProperty("native")
            .GetProperty("sha256").GetString());

        object FileIdentity(string path) => new { path = RelativePath(root, path), sha256 = Sha256(path) };
        return new
        {
            schema = "managed-preparation-v1",
            status = "passed",
            scope = "Baseline headless preparation smoke only; no UI, conformance or performance claim.",
            source, build, toolchain,
            input = new { markup = Markup, sha256 = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Markup))).ToLowerInvariant(),
                textMeasurer = "AetherUI.Layout.ApproximateTextMeasurer",
                textMeasurerSource = FileIdentity(Path.Combine(root, "bindings/dotnet/src/AetherUI.Core/Layout/ITextMeasurer.cs")),
                preflightSource = FileIdentity(Path.Combine(root, "bindings/dotnet/tests/AetherUI.Metrics.Preflight/Program.cs")) },
            viewport = new { width = 640, height = 480, dpi = 1 },
            counts = new { metrics.LayoutBuilds, metrics.RenderCalls, backendRenderCalls = renderer.RenderCalls,
                displayCommands = renderer.Commands.Length },
            boxes, checks,
            loadedNative = new { path = RelativePath(root, loadedPath), sha256 = loadedHash,
                installedPath = RelativePath(root, installedPath), installedSha256 = installedHash },
            assemblies = new[]
            {
                FileIdentity(typeof(Program).Assembly.Location),
                FileIdentity(typeof(UiDocumentHost).Assembly.Location),
                FileIdentity(typeof(NativeStyleImpact).Assembly.Location),
            },
        };
    }

    private static string Sha256(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static string RelativePath(string root, string path)
    {
        var relative = Path.GetRelativePath(root, path);
        if (Path.IsPathRooted(relative) || relative == ".."
            || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal))
            throw new InvalidOperationException("Evidence path is outside the checkout.");
        return relative.Replace('\\', '/');
    }

    private sealed class FixtureComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(Markup);
    }

    private sealed class MemoryRenderer : IRenderBackend
    {
        public int RenderCalls { get; private set; }
        public PaintCommand[] Commands { get; private set; } = [];
        public void Resize(int pixelWidth, int pixelHeight, float dpiScale) { }
        public void Render(DisplayList displayList, ColorRgba clearColor)
        {
            Commands = displayList.Commands.ToArray();
            RenderCalls++;
        }
        public void Dispose() { }
    }
}
