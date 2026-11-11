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
using AetherUI.Validator;

namespace AetherUI.Metrics.Tests;

internal static class Program
{
    private const string Markup = "<body>"
        + "<div id=\"first\" style=\"width:120px;height:40px;background:#123456\">Alpha</div>"
        + "<div id=\"second\" style=\"width:160px;height:50px;background:#654321\">Beta</div>"
        + "</body>";
    private static readonly List<object> Checks = [];
    private static readonly List<object> Logs = [];
    private static string _logRoot = "";

    private static int Main(string[] args)
    {
        if (args.Length != 2 || args[0] != "--output") return 2;
        _logRoot = Path.Combine(Path.GetTempPath(), "aether-metrics-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_logRoot);
        object? native = null;
        string? exceptionType = null;
        int exit = 0;
        try
        {
            T1();
            T2();
            native = T3();
            T6();
        }
        catch (Exception error)
        {
            exceptionType = error.GetType().FullName;
            Console.Error.WriteLine(error);
            exit = 1;
        }
        // T4 is the mandatory external common-exporter comparison. T5 is independent source review.
        // This executable never substitutes a runtime claim for either of those gates.
        var result = new { schema = "host-phase-tests-v1", status = exit == 0 ? "passed" : "failed",
            cases = new[] { "T1", "T2", "T3", "T6" }, checks = Checks, logs = Logs, native, exceptionType,
            externalRequired = new[] { "T4 common exporter baseline/candidate comparison", "T5 independent boundary review" },
            scope = "Focal headless tests only; measured is not application success; no performance or conformance claim.",
            fixtureSha256 = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Markup))).ToLowerInvariant() };
        File.WriteAllText(args[1], JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }) + "\n");
        Console.WriteLine(exit == 0 ? "T1/T2/T3/T6 passed." : "Focal tests failed.");
        return exit;
    }

    private static void Check(string name, bool passed)
    {
        Checks.Add(new { name, passed });
        if (!passed) throw new InvalidOperationException("Failed: " + name);
    }

    private static UiDocumentHostMetrics Prior() => new(1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        Stopwatch.Frequency, Stopwatch.Frequency * 2, Stopwatch.Frequency * 3, default);

    private static void T1()
    {
        var prior = Prior(); // Previous positional constructor, intentionally unchanged.
        Check("T1.new-defaults-zero", prior.LayoutInteropTimestampTicks == 0
            && prior.LayoutProjectionTimestampTicks == 0 && prior.LayoutProjections == 0
            && prior.LayoutInteropMilliseconds == 0 && prior.LayoutProjectionMilliseconds == 0);
        var (a, b, c, d, e, f, g, h, i, j, k, l, m, n) = prior;
        Check("T1.old-deconstruct", a == 1 && b == 2 && c == 3 && d == 4 && e == 5
            && f == 6 && g == 7 && h == 8 && i == 9 && j == 10 && k == Stopwatch.Frequency
            && l == Stopwatch.Frequency * 2 && m == Stopwatch.Frequency * 3 && n.Equals(default(NativeStyleImpact)));
        var interop = prior with { LayoutInteropTimestampTicks = Stopwatch.Frequency };
        var projection = prior with { LayoutProjectionTimestampTicks = Stopwatch.Frequency };
        Check("T1.independent-interop-conversion", interop.LayoutInteropMilliseconds == 1000
            && interop.LayoutProjectionMilliseconds == 0);
        Check("T1.independent-projection-conversion", projection.LayoutProjectionMilliseconds == 1000
            && projection.LayoutInteropMilliseconds == 0);
        var full = prior with { LayoutInteropTimestampTicks = Stopwatch.Frequency,
            LayoutProjectionTimestampTicks = Stopwatch.Frequency, LayoutProjections = 4 };
        var same = prior with { LayoutInteropTimestampTicks = Stopwatch.Frequency,
            LayoutProjectionTimestampTicks = Stopwatch.Frequency, LayoutProjections = 4 };
        Check("T1.equal-values-and-hash", full.Equals(same) && full == same && !(full != same)
            && full.GetHashCode() == same.GetHashCode());
        var variants = new[] { full with { LayoutInteropTimestampTicks = full.LayoutInteropTimestampTicks + 1 },
            full with { LayoutProjectionTimestampTicks = full.LayoutProjectionTimestampTicks + 1 },
            full with { LayoutProjections = full.LayoutProjections + 1 } };
        for (int index = 0; index < variants.Length; index++)
            Check($"T1.new-property-equality-{index}", !full.Equals(variants[index])
                && !(full == variants[index]) && full != variants[index]);
    }

    private static List<JsonElement> Capture(string label, Action<ValidationReport, PerfReporter> write)
    {
        using var report = new ValidationReport(_logRoot);
        using var reporter = new PerfReporter(label, report);
        write(report, reporter);
        reporter.Dispose();
        reporter.Dispose();
        report.Dispose();
        string text = File.ReadAllText(Path.Combine(report.DirectoryPath, "report.log"));
        var events = Parse(text);
        Logs.Add(new { label, text }); // Entire synthetic public-fixture JSONL, including events after final.
        Check(label + ".single-final", events.Count(e => Kind(e) == "metrics-final") == 1);
        return events;
    }

    private static void T2()
    {
        var sample = Prior() with { LayoutInteropTimestampTicks = Stopwatch.Frequency,
            LayoutProjectionTimestampTicks = Stopwatch.Frequency * 2, LayoutProjections = 3 };
        var events = Capture("T2.fields", (_, reporter) => reporter.Report(sample));
        Check("T2.first-snapshot", events.Count(e => Kind(e) == "metrics-snapshot") == 1);
        foreach (var entry in events.Where(e => Kind(e) is "metrics-final" or "metrics-snapshot"))
        {
            var data = entry.GetProperty("data");
            Check("T2.schema-and-status-" + Kind(entry), data.GetProperty("timingSchema").GetString() == "host-phases-v2"
                && data.GetProperty("label").GetString() == "T2.fields"
                && data.GetProperty("status").GetString() == "measured" && data.GetProperty("samples").GetInt64() == 1);
            foreach (var (key, value) in new (string, double)[] { ("renders", 7), ("incrementalRenders", 8),
                ("styleResolutions", 1), ("styleUpdatedElements", 2), ("layoutBuilds", 3), ("displayListBuilds", 4),
                ("paintChunkBuilds", 5), ("scrollCompositions", 6), ("visualStyleMapBuilds", 9),
                ("visualStyleMapUpdates", 10), ("styleMs", 1000), ("displayListMs", 2000), ("renderMs", 3000),
                ("layoutInteropMs", 1000), ("layoutProjectionMs", 2000), ("layoutProjections", 3) })
                Check("T2.field-" + Kind(entry) + "-" + key, data.GetProperty(key).GetDouble() == value);
            Check("T2.old-resource-fields-" + Kind(entry), data.TryGetProperty("elapsedMs", out _)
                && data.TryGetProperty("processCpuMsSinceReporterStart", out _)
                && data.TryGetProperty("processAverageCoreEquivalent", out _)
                && data.GetProperty("process").GetProperty("Status").GetString() is "measured" or "unavailable");
            Check("T2.old-average-fields-" + Kind(entry), data.GetProperty("averageRenderMs").GetDouble() == 3000d / 7
                && data.GetProperty("averageDisplayListMs").GetDouble() == 500);
            Check("T2.old-scope-and-interpretation-" + Kind(entry),
                data.GetProperty("processScope").GetString() == "validator process including native STA; excludes WebView2 child processes and GPU memory"
                && data.GetProperty("interpretation").GetString() == "cumulative host operation counters and elapsed pipeline timings; process CPU reported separately; not FPS");
            CheckScopes(data);
        }
        var empty = Capture("T2.empty", (_, _) => { });
        Check("T2.not-run", empty.Single(e => Kind(e) == "metrics-final").GetProperty("data")
            .GetProperty("status").GetString() == "not-run"
            && empty.All(e => Kind(e) != "metrics-snapshot"));
        var emptyFinal = empty.Single(e => Kind(e) == "metrics-final").GetProperty("data");
        Check("T2.empty-schema-and-zero-values", emptyFinal.GetProperty("timingSchema").GetString() == "host-phases-v2"
            && emptyFinal.GetProperty("samples").GetInt64() == 0
            && emptyFinal.GetProperty("layoutInteropMs").GetDouble() == 0
            && emptyFinal.GetProperty("layoutProjectionMs").GetDouble() == 0
            && emptyFinal.GetProperty("layoutProjections").GetInt64() == 0
            && emptyFinal.GetProperty("averageRenderMs").ValueKind == JsonValueKind.Null
            && emptyFinal.GetProperty("averageDisplayListMs").ValueKind == JsonValueKind.Null);
        CheckScopes(emptyFinal);
        var decreased = new[] { sample with { LayoutInteropTimestampTicks = sample.LayoutInteropTimestampTicks - 1 },
            sample with { LayoutProjectionTimestampTicks = sample.LayoutProjectionTimestampTicks - 1 },
            sample with { LayoutProjections = 2 } };
        for (int index = 0; index < decreased.Length; index++)
        {
            var next = decreased[index];
            var reset = Capture("T2.reset-" + index, (_, reporter) => { reporter.Report(sample); reporter.Report(next); });
            int resetIndex = reset.FindIndex(e => Kind(e) == "metrics-reset");
            Check("T2.decrement-detected-" + index, resetIndex > 0 && reset.Count(e => Kind(e) == "metrics-reset") == 1);
            // No sleeps, no assumption that the throttle must emit a second snapshot.
            int finalIndex = reset.FindIndex(e => Kind(e) == "metrics-final");
            Check("T2.reset-before-new-final-" + index, resetIndex < finalIndex);
            Check("T2.reset-before-any-new-snapshot-" + index, !reset.Take(resetIndex)
                .Any(e => Kind(e) == "metrics-snapshot" && e.GetProperty("data").GetProperty("samples").GetInt64() == 2));
            foreach (var entry in reset.Skip(resetIndex + 1).Where(e => Kind(e) is "metrics-snapshot" or "metrics-final"))
            {
                var data = entry.GetProperty("data");
                Check("T2.new-values-after-reset-" + index + "-" + Kind(entry),
                    data.GetProperty("layoutInteropMs").GetDouble() == next.LayoutInteropMilliseconds
                    && data.GetProperty("layoutProjectionMs").GetDouble() == next.LayoutProjectionMilliseconds
                    && data.GetProperty("layoutProjections").GetInt64() == next.LayoutProjections);
            }
        }
        // Simulate a known reset followed by enough work to recover every sampled value.
        // The reporter only receives those equal endpoints; absence of an event cannot prove continuity.
        var invisible = Capture("T2.known-reset-recovered", (_, reporter) => { reporter.Report(sample); reporter.Report(sample); });
        Check("T2.reset-can-be-invisible", invisible.All(e => Kind(e) != "metrics-reset"));
    }

    private static void CheckScopes(JsonElement data)
    {
        var expected = new Dictionary<string, string>
        {
            ["accumulation"] = "cumulative partial wall-time regions; not an exhaustive decomposition or a sum-to-total contract",
            ["layoutInteropMs"] = "CreateNativeLayout wrapper/PInvoke/native algorithm and text/scroll callbacks; excludes prior style map, projection, reporting and drawing",
            ["layoutProjectionMs"] = "Project native reads, marshaling, conversion and managed allocation; excludes subsequent scroll index and drawing",
            ["styleMs"] = "ResolveStyles preparation, native calls, managed style conversion and map updates; not pure engine time",
            ["displayListMs"] = "RenderingInterop.Build including managed readback/decoding and AttachResolvedImages",
            ["renderMs"] = "backend TryRenderIncremental or Render after EnsureVisualTree; excludes parse/style/layout and is not presentation or GPU time",
            ["completion"] = "operations that returned without propagating; callbacks may catch errors; measured means samples, not run success",
            ["reset"] = "individual reads/resets are not atomic together; use serialized host access; only observed decrements emit metrics-reset",
            ["unmeasured"] = "DOM parse/projection, CSS loading, layout style-map preparation, scroll index, scheduling, first presentation, callback subphases, reporting and captures"
        };
        var scope = data.GetProperty("timingScope");
        Check("T2.scope-key-count", scope.EnumerateObject().Count() == expected.Count);
        foreach (var pair in expected)
            Check("T2.scope-" + pair.Key, scope.GetProperty(pair.Key).GetString() == pair.Value);
    }

    private static object T3()
    {
        Check("T3.windows-x64", OperatingSystem.IsWindows() && RuntimeInformation.ProcessArchitecture == Architecture.X64);
        using var component = new FixtureComponent();
        var renderer = new MemoryRenderer();
        using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer,
            new UiHostOptions { Width = 640, Height = 480 });
        host.SetViewport(640, 480, 1);
        host.Render();
        var first = host.Metrics;
        Check("T3.first-layout", first.LayoutBuilds == 1 && first.LayoutProjections == 1
            && first.LayoutInteropTimestampTicks >= 0 && first.LayoutProjectionTimestampTicks >= 0);
        Check("T3.real-output", renderer.Commands.Length > 0 && host.Layout is not null
            && host.Document!.GetElementById("first") is not null && host.Document.GetElementById("second") is not null);
        foreach (string id in new[] { "first", "second" })
            Check("T3.primary-box-" + id, host.Layout!.FindPrimaryBox(host.Document!.GetElementById(id)!) is not null);
        host.Render();
        var second = host.Metrics;
        Check("T3.valid-tree-no-layout", SameLayout(first, second));
        host.SetViewport(800, 480, 1);
        host.Render();
        var resized = host.Metrics;
        Check("T3.viewport-rebuild", resized.LayoutBuilds == second.LayoutBuilds + 1
            && resized.LayoutProjections == second.LayoutProjections + 1
            && resized.LayoutInteropTimestampTicks >= second.LayoutInteropTimestampTicks
            && resized.LayoutProjectionTimestampTicks >= second.LayoutProjectionTimestampTicks);
        host.ResetMetrics();
        var reset = host.Metrics;
        Check("T3.reset-all-counts-and-times", reset == default);
        host.Render();
        Check("T3.reset-valid-tree-no-layout", SameLayout(reset, host.Metrics));
        using var process = Process.GetCurrentProcess();
        var modules = process.Modules.Cast<ProcessModule>().Where(module =>
            string.Equals(module.ModuleName, "aether_core.dll", StringComparison.OrdinalIgnoreCase)).ToArray();
        Check("T3.one-native-module", modules.Length == 1);
        string loadedPath = Path.GetFullPath(modules[0].FileName);
        Check("T3.native-output-path", loadedPath.Equals(Path.Combine(AppContext.BaseDirectory, "aether_core.dll"), StringComparison.OrdinalIgnoreCase));
        string hash = Sha256(loadedPath);
        Check("T3.native-installed-hash", hash == Sha256(Path.Combine(Directory.GetCurrentDirectory(), "artifacts/native/win-x64/bin/aether_core.dll")));
        return new { loadedSha256 = hash, module = "aether_core.dll", assemblySha256 = Sha256(typeof(UiDocumentHost).Assembly.Location),
            sequence = new[] { first, second, resized, reset, host.Metrics } };
    }

    private static bool SameLayout(UiDocumentHostMetrics left, UiDocumentHostMetrics right) =>
        left.LayoutBuilds == right.LayoutBuilds && left.LayoutProjections == right.LayoutProjections
        && left.LayoutInteropTimestampTicks == right.LayoutInteropTimestampTicks
        && left.LayoutProjectionTimestampTicks == right.LayoutProjectionTimestampTicks;

    private static string Sha256(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
    private static string Kind(JsonElement entry) => entry.GetProperty("kind").GetString()!;
    private static List<JsonElement> Parse(string text) => text.Split('\n', StringSplitOptions.RemoveEmptyEntries)
        .Select(line => { using var doc = JsonDocument.Parse(line); return doc.RootElement.Clone(); }).ToList();

    // A consumer contract for complete metric capture, not the application's actual error wiring.
    private static bool Complete(string text, int exit, bool timeout)
    {
        if (exit != 0 || timeout || !text.EndsWith('\n')) return false;
        try
        {
            var events = Parse(text);
            if (events.Count < 3 || Kind(events[0]) != "session" || Kind(events[^1]) != "session-end") return false;
            if (events.Any(e => Kind(e) is "error" or "shutdown-timeout")) return false;
            var finals = events.Where(e => Kind(e) == "metrics-final").ToArray();
            return finals.Length == 1 && finals[0].GetProperty("data").GetProperty("status").GetString() == "measured";
        }
        catch (Exception error) when (error is JsonException or KeyNotFoundException or InvalidOperationException)
        {
            return false;
        }
    }

    private static void T6()
    {
        string Make(string label, string? before, string? after)
        {
            using var report = new ValidationReport(_logRoot);
            using var reporter = new PerfReporter(label, report);
            reporter.Report(Prior());
            if (before is not null) report.Write(before, new { reason = "synthetic-public-fixture" });
            reporter.Dispose();
            if (after is not null) report.Write(after, new { reason = "synthetic-public-fixture" });
            report.Dispose();
            string text = File.ReadAllText(Path.Combine(report.DirectoryPath, "report.log"));
            Logs.Add(new { label, text });
            Check(label + ".measured-final-exists", Parse(text).Single(e => Kind(e) == "metrics-final")
                .GetProperty("data").GetProperty("status").GetString() == "measured");
            return text;
        }
        string clean = Make("T6.clean", null, null);
        Check("T6.clean-capture-complete", Complete(clean, 0, false));
        Check("T6.error-after-final-incomplete", !Complete(Make("T6.error-after", null, "error"), 0, false));
        Check("T6.error-before-final-incomplete", !Complete(Make("T6.error-before", "error", null), 0, false));
        Check("T6.shutdown-timeout-incomplete", !Complete(Make("T6.shutdown", null, "shutdown-timeout"), 0, false));
        Check("T6.exit-nonzero-incomplete", !Complete(clean, 17, false));
        Check("T6.process-timeout-incomplete", !Complete(clean, 0, true));
        Check("T6.truncated-jsonl-incomplete", !Complete(clean + "{\"kind\":", 0, false));
        Check("T6.malformed-terminated-jsonl-incomplete", !Complete(clean + "{\"kind\":\n", 0, false));
        string noEnd = string.Join('\n', clean.Split('\n').Where(line => !line.Contains("\"kind\":\"session-end\"", StringComparison.Ordinal)));
        Check("T6.missing-session-end-incomplete", !Complete(noEnd, 0, false));
    }

    private sealed class FixtureComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(Markup);
    }
    private sealed class MemoryRenderer : IRenderBackend
    {
        public PaintCommand[] Commands { get; private set; } = [];
        public void Resize(int pixelWidth, int pixelHeight, float dpiScale) { }
        public void Render(DisplayList displayList, ColorRgba clearColor) => Commands = displayList.Commands.ToArray();
        public void Dispose() { }
    }
}
