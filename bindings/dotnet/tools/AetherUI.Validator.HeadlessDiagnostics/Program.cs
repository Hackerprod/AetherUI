using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

namespace AetherUI.Validator.HeadlessDiagnostics;

internal sealed class ContractFailure(string code) : Exception(code)
{
    public string Code { get; } = code;
    public string? Domain { get; init; }
    public long? Bytes { get; init; }
}

internal static class Program
{
    internal const string FixtureHash = "eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837";
    internal static readonly UTF8Encoding Utf8 = new(false, true);
    private static string session = "invalid";
    private static int sequence;
    internal static void Require(bool condition, string code) { if (!condition) throw new ContractFailure(code); }
    internal static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    internal static string FileHash(string path) { using var s = File.OpenRead(path); return Convert.ToHexString(SHA256.HashData(s)).ToLowerInvariant(); }
    private static void Event(string name, object data) => Console.WriteLine(JsonSerializer.Serialize(new { schema = "headless-events-v1", @event = name, session, sequence = sequence++, data }));
    private static void Phase(string name) => Event("phase-start", new { phase = name });
    internal static void WriteJson(string path, object value, int limit)
    {
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(value);
        Require(bytes.Length <= limit, "result-byte-limit");
        using var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        stream.Write(bytes); stream.Flush(true);
    }
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 2 && args[0] == "--vectors") { ContractTests.Run(args[1]); return 0; }
            if (args.Length == 2 && args[0] == "--contract") return Synthetic(args[1]);
            Require(args.Length == 8, "arguments");
            var options = new Dictionary<string, string>(StringComparer.Ordinal);
            for (int i = 0; i < args.Length; i += 2) Require(options.TryAdd(args[i], args[i + 1]), "arguments");
            Require(options.Keys.Order(StringComparer.Ordinal).SequenceEqual(new[] { "--fixture", "--manifest", "--output", "--session" }), "arguments");
            session = options["--session"];
            Require(session == "warmup" || Enumerable.Range(1, 5).Any(i => session == "sample-" + i), "session");
            Run(options["--fixture"], options["--manifest"], options["--output"]);
            return 0;
        }
        catch (Exception e)
        {
            Event("error", new { code = e is ContractFailure f ? f.Code : "unhandled-error", exceptionType = e.GetType().Name, lastDomain = (e as ContractFailure)?.Domain, canonicalBytes = (e as ContractFailure)?.Bytes });
            return 1;
        }
    }
    private static Dictionary<string, string> ReadManifest(string path, out string commit, out string tree)
    {
        Require(new FileInfo(path).Length is > 0 and <= 65536, "manifest-size");
        using var json = JsonDocument.Parse(File.ReadAllBytes(path));
        var root = json.RootElement;
        Require(root.EnumerateObject().Select(p => p.Name).Order(StringComparer.Ordinal).SequenceEqual(new[] { "files", "schema", "sourceCommit", "sourceTree" }), "manifest-fields");
        Require(root.GetProperty("schema").GetString() == "headless-input-v1", "manifest-schema");
        commit = root.GetProperty("sourceCommit").GetString()!; tree = root.GetProperty("sourceTree").GetString()!;
        Require(IsHex(commit, 40) && IsHex(tree, 40), "source-identity");
        var files = root.GetProperty("files").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!, StringComparer.Ordinal);
        Require(files.Keys.Order(StringComparer.Ordinal).SequenceEqual(new[] { "AetherUI.Core.Native.Interop.dll", "AetherUI.Core.dll", "AetherUI.Validator.HeadlessDiagnostics.dll", "aether_core.dll" }), "manifest-files");
        foreach (var (name, hash) in files) Require(IsHex(hash, 64) && FileHash(Path.Combine(AppContext.BaseDirectory, name)) == hash, "binary-before");
        return files;
    }
    private static bool IsHex(string? s, int n) => s is not null && s.Length == n && s.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
    private const int FixtureSize = 474495;
    private sealed record FixtureRead(string Markup, double ReadDecodeMs, double ChecksumMs);

    // The real route has no size/hash overrides: only the immutable approved fixture is accepted.
    private static FixtureRead ReadFixture(string path) => ReadFixtureCore(path, FixtureHash);

    // Private hash seam for controlled synthetic files below; never populated by CLI or manifest.
    // Size remains fixed even in tests. Both routes use this same bounded read, hash and decoder.
    private static FixtureRead ReadFixtureCore(string path, string expectedHash)
    {
        Require(IsHex(expectedHash, 64), "fixture-expectation");
        var watch = Stopwatch.StartNew();
        byte[] bytes = new byte[FixtureSize];
        using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            Require(stream.Length == FixtureSize, "fixture-size");
            try { stream.ReadExactly(bytes); }
            catch (EndOfStreamException) { throw new ContractFailure("fixture-size"); }
            Require(stream.ReadByte() == -1, "fixture-size");
        }
        double readMs = watch.Elapsed.TotalMilliseconds;
        watch.Restart(); Require(Hash(bytes) == expectedHash, "fixture-hash");
        double checksumMs = watch.Elapsed.TotalMilliseconds;
        watch.Restart();
        string markup;
        try { markup = Utf8.GetString(bytes); }
        catch (DecoderFallbackException) { throw new ContractFailure("fixture-utf8"); }
        return new FixtureRead(markup, readMs + watch.Elapsed.TotalMilliseconds, checksumMs);
    }

    internal static void CheckFixtureContracts(List<string> checks)
    {
        // No fixture override can escape this test: inputs are local synthetic bytes, not an
        // external path supplied by the user, and the host/native runtime is never constructed.
        string directory = Path.Combine(Path.GetTempPath(), "aether-ffd-fixture-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        string validPath = Path.Combine(directory, "valid.bin");
        string truncatedPath = Path.Combine(directory, "truncated.bin");
        string invalidPath = Path.Combine(directory, "invalid-utf8.bin");
        void MustReject(string name, string expectedCode, Action action)
        {
            string? actual = null;
            try { action(); } catch (ContractFailure error) { actual = error.Code; }
            Require(actual == expectedCode, "fixture-test-" + name);
            checks.Add(name);
        }
        try
        {
            byte[] valid = new byte[FixtureSize]; Array.Fill(valid, (byte)' ');
            byte[] prefix = Utf8.GetBytes("<body>é\r\n</body>"); prefix.CopyTo(valid, 0);
            string validHash = Hash(valid);
            Require(validHash != FixtureHash, "fixture-test-distinct-input");
            File.WriteAllBytes(validPath, valid);
            File.WriteAllBytes(truncatedPath, valid.AsSpan(0, FixtureSize - 1).ToArray());
            byte[] invalid = (byte[])valid.Clone(); invalid[20] = 0xff;
            File.WriteAllBytes(invalidPath, invalid);
            var positive = ReadFixtureCore(validPath, validHash);
            Require(positive.Markup == "<body>é\r\n</body>" + new string(' ', FixtureSize - prefix.Length), "fixture-test-exact-text");
            Require(double.IsFinite(positive.ReadDecodeMs) && positive.ReadDecodeMs >= 0 && double.IsFinite(positive.ChecksumMs) && positive.ChecksumMs >= 0, "fixture-test-timing");
            checks.Add("fixture-pipeline-valid-exact-utf8-crlf");
            MustReject("fixture-pipeline-truncated", "fixture-size", () => ReadFixtureCore(truncatedPath, validHash));
            MustReject("fixture-pipeline-wrong-hash", "fixture-hash", () => ReadFixtureCore(validPath, new string('0', 64)));
            MustReject("fixture-pipeline-invalid-utf8", "fixture-utf8", () => ReadFixtureCore(invalidPath, Hash(invalid)));
            MustReject("fixture-pipeline-hash-before-decode", "fixture-hash", () => ReadFixtureCore(invalidPath, validHash));
            MustReject("fixture-real-wrapper-fixed-hash", "fixture-hash", () => ReadFixture(validPath));
            MustReject("fixture-real-wrapper-fixed-size", "fixture-size", () => ReadFixture(truncatedPath));
        }
        finally
        {
            // Only the three files created by this test; no recursive deletion or ignored failure.
            File.Delete(validPath); File.Delete(truncatedPath); File.Delete(invalidPath);
            Directory.Delete(directory, false);
        }
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void Run(string fixture, string manifest, string output)
    {
        Require(OperatingSystem.IsWindows() && RuntimeInformation.ProcessArchitecture == Architecture.X64, "platform");
        Event("session", new { schema = "headless-full-fixture-v1" });
        Phase("read");
        var files = ReadManifest(manifest, out var commit, out var tree);
        var input = ReadFixture(fixture);
        string markup = input.Markup;
        double readDecodeMs = input.ReadDecodeMs;
        double checksumMs = input.ChecksumMs;
        var checksumWatch = new Stopwatch();
        var configuration = new { width = 1440, height = 900, dpiScale = 1, textMeasurer = "ApproximateTextMeasurer", imageResourcePolicy = "unresolved", svgReferences = 3, decodedImages = 0, projectedBoxesOnly = true };
        Event("input-config", new { inputSha256 = FixtureHash, sourceCommit = commit, sourceTree = tree, files, configuration });
        using var process = Process.GetCurrentProcess();
        var cpuBefore = process.TotalProcessorTime.TotalMilliseconds;
        var gcBefore = Enumerable.Range(0, 3).Select(GC.CollectionCount).ToArray();
        var measurer = new CountingMeasurer();
        var renderer = new MemoryRenderer();
        var component = new FixtureComponent(markup);
        bool componentOwned = false;
        UiDocumentHost? host = null;
        Snapshot snapshot;
        double constructorMs, firstRenderWallMs, verificationMs, disposeMs;
        Dictionary<string, object> counts;
        Dictionary<string, double> metrics;
        object[] loadedIdentities; long[] timestampTicks;
        try
        {
            Phase("constructor"); var watch = Stopwatch.StartNew();
            host = new UiDocumentHost(component, measurer, renderer, new UiHostOptions { Width = 1440, Height = 900, ImageResolver = null, MetricsSink = null });
            componentOwned = true;
            constructorMs = watch.Elapsed.TotalMilliseconds;
            host.SetViewport(1440, 900, 1);
            AssertBefore(host.Metrics, renderer, measurer);
            Phase("render"); watch.Restart();
            host.Render();
            firstRenderWallMs = watch.Elapsed.TotalMilliseconds;
            var m = host.Metrics;
            timestampTicks = Ticks(m);
            counts = Counts(m, renderer.RenderCalls, measurer.Calls, measurer.Failures, host.NeedsRender, host.HasActiveTransitions);
            metrics = new Dictionary<string, double>(StringComparer.Ordinal) {
                ["constructorMs"] = constructorMs, ["firstRenderWallMs"] = firstRenderWallMs,
                ["styleResolutionMs"] = m.StyleResolutionMilliseconds, ["layoutInteropMs"] = m.LayoutInteropMilliseconds,
                ["layoutProjectionMs"] = m.LayoutProjectionMilliseconds, ["displayListBuildMs"] = m.DisplayListBuildMilliseconds,
                ["renderMs"] = m.RenderMilliseconds, ["readDecodeMs"] = readDecodeMs
            };
            Event("metrics-snapshot", new { counts, metrics, timestampTicks = Ticks(m) });
            Phase("verification"); watch.Restart();
            AssertAfter(m, renderer, measurer, host.NeedsRender);
            snapshot = Snapshot.Create(host.Document ?? throw new ContractFailure("missing-dom"), host.Layout ?? throw new ContractFailure("missing-layout"), renderer, counts, host.DpiScale);
            loadedIdentities = VerifyLoaded(files);
            checksumWatch.Restart();
            Require(FileHash(fixture) == FixtureHash, "fixture-after");
            checksumMs += checksumWatch.Elapsed.TotalMilliseconds;
            verificationMs = watch.Elapsed.TotalMilliseconds;
            Event("result", new { status = "measured" });
            Event("metrics-final", new { status = "measured" });
            Phase("dispose"); watch.Restart();
            var closingHost = host; host = null; closingHost.Dispose();
            disposeMs = watch.Elapsed.TotalMilliseconds;
        }
        finally { try { host?.Dispose(); } finally { if (!componentOwned) { component.Dispose(); renderer.Dispose(); } } }
        metrics["verificationMs"] = verificationMs; metrics["disposeMs"] = disposeMs;
        metrics["checksumMs"] = checksumMs; metrics["canonicalMs"] = snapshot.CanonicalMs;
        Require(metrics.Values.All(v => double.IsFinite(v) && v >= 0), "invalid-duration");
        foreach (var (name, hash) in files) Require(FileHash(Path.Combine(AppContext.BaseDirectory, name)) == hash, "binary-after");
        process.Refresh();
        var resources = new { processCpuMs = process.TotalProcessorTime.TotalMilliseconds, observedCpuMs = process.TotalProcessorTime.TotalMilliseconds - cpuBefore,
            peakWorkingSetBytes = process.PeakWorkingSet64, privateBytes = process.PrivateMemorySize64, managedBytes = GC.GetTotalMemory(false),
            gcBefore, gcAfter = Enumerable.Range(0, 3).Select(GC.CollectionCount).ToArray(),
            logicalCpuCount = Environment.ProcessorCount, os = RuntimeInformation.OSDescription, architecture = "X64", runtime = RuntimeInformation.FrameworkDescription,
            cpuModel = (string?)null, cpuModelAvailability = "not-collected", scope = "process peak sampled after verification and event reporting, before final file write; no external process measurements" };
        var reportingWatch = Stopwatch.StartNew();
        WriteJson(output, new { schema = "headless-full-fixture-v1", status = "completed", session, sourceCommit = commit, sourceTree = tree,
            inputSha256 = FixtureHash, files, loadedIdentities, timestampTicks, configuration, metrics, counts, resources, digest = snapshot.Digest, witnesses = snapshot.Witnesses,
            limitations = new[] { "Approximate text with counted callbacks; images unresolved", "Interop includes wrapper and callbacks; projection includes native getters", "Spans are not an exhaustive partition", "Projected boxes exclude native-only boxes", "No pixels, visual conformance, animation conformance or comparison", "Full stream not retained; only vectors and witnesses can be rehashed" } }, 2 * 1024 * 1024);
        Event("session-end", new { status = "completed", reportingMs = reportingWatch.Elapsed.TotalMilliseconds });
    }
    private static object[] VerifyLoaded(Dictionary<string, string> files)
    {
        var identities = new List<object>();
        foreach (Assembly a in new[] { typeof(Program).Assembly, typeof(UiDocumentHost).Assembly, typeof(NativeStyleImpact).Assembly })
        {
            var name = Path.GetFileName(a.Location);
            Require(files.ContainsKey(name) && Path.GetFullPath(a.Location).Equals(Path.Combine(AppContext.BaseDirectory, name), StringComparison.OrdinalIgnoreCase) && FileHash(a.Location) == files[name], "loaded-assembly");
            identities.Add(new { file = name, sha256 = files[name], fullName = a.FullName, mvid = a.ManifestModule.ModuleVersionId.ToString("D"), native = false });
        }
        using var process = Process.GetCurrentProcess();
        var modules = process.Modules.Cast<ProcessModule>().Where(m => string.Equals(m.ModuleName, "aether_core.dll", StringComparison.OrdinalIgnoreCase)).ToArray();
        Require(modules.Length == 1, "native-module-count");
        Require(Path.GetFullPath(modules[0].FileName).Equals(Path.Combine(AppContext.BaseDirectory, "aether_core.dll"), StringComparison.OrdinalIgnoreCase) && FileHash(modules[0].FileName) == files["aether_core.dll"], "loaded-native");
        identities.Add(new { file = "aether_core.dll", sha256 = files["aether_core.dll"], fullName = (string?)null, mvid = (string?)null, native = true });
        return identities.ToArray();
    }
    internal static long[] Ticks(UiDocumentHostMetrics m) => [m.StyleResolutionTimestampTicks, m.LayoutInteropTimestampTicks, m.LayoutProjectionTimestampTicks, m.DisplayListBuildTimestampTicks, m.RenderTimestampTicks];
    private static void AssertBefore(UiDocumentHostMetrics m, MemoryRenderer b, CountingMeasurer t)
    {
        Require(new[] { m.StyleResolutions, m.StyleUpdatedElements, m.LayoutBuilds, m.LayoutProjections, m.DisplayListBuilds, m.PaintChunkBuilds, m.ScrollCompositions, m.RenderCalls, m.IncrementalRenderCalls, m.VisualStyleMapBuilds, m.VisualStyleMapUpdates }.All(x => x == 0) && Ticks(m).All(x => x == 0) && b.RenderCalls == 0 && t.Calls == 0 && t.Failures == 0, "work-before-render");
    }
    internal static void AssertAfter(UiDocumentHostMetrics m, MemoryRenderer b, CountingMeasurer t, bool pending)
    {
        Require(m.RenderCalls == 1 && b.RenderCalls == 1 && m.LayoutBuilds == 1 && m.LayoutProjections == 1 && m.StyleResolutions == 1 && m.DisplayListBuilds == 1 && m.VisualStyleMapBuilds == 1, "unexpected-multipass");
        Require(m.IncrementalRenderCalls == 0 && m.PaintChunkBuilds == 0 && m.ScrollCompositions == 0 && m.VisualStyleMapUpdates == 0 && m.StyleUpdatedElements > 0 && Ticks(m).All(x => x >= 0), "unexpected-work");
        Require((long)m.LastStyleImpact >= 0 && ((long)m.LastStyleImpact & ~127L) == 0, "style-impact-flags");
        Require(t.Calls > 0 && t.Failures == 0, "text-callback-failed");
        Require(!pending, "pending-work");
        Require(b.Commands.Any(c => c is DrawTextCommand or DrawTextGradientCommand or DrawTextShadowCommand) && b.Commands.Any(c => c is FillRoundedRectangleCommand or FillRoundedRectangleGradientCommand or FillBorderSideCommand), "missing-fill-or-text");
    }
    internal static Dictionary<string, object> Counts(UiDocumentHostMetrics m, int backend, long callbacks, long failures, bool pending, bool transitions) => new(StringComparer.Ordinal) {
        ["StyleResolutions"] = m.StyleResolutions, ["StyleUpdatedElements"] = m.StyleUpdatedElements, ["LayoutBuilds"] = m.LayoutBuilds,
        ["LayoutProjections"] = m.LayoutProjections, ["DisplayListBuilds"] = m.DisplayListBuilds, ["PaintChunkBuilds"] = m.PaintChunkBuilds,
        ["ScrollCompositions"] = m.ScrollCompositions, ["RenderCalls"] = m.RenderCalls, ["IncrementalRenderCalls"] = m.IncrementalRenderCalls,
        ["VisualStyleMapBuilds"] = m.VisualStyleMapBuilds, ["VisualStyleMapUpdates"] = m.VisualStyleMapUpdates, ["LastStyleImpact"] = (long)m.LastStyleImpact,
        ["backendRenderCalls"] = backend, ["constructorCalls"] = 1, ["setViewportCalls"] = 1, ["publicRenderCalls"] = 1,
        ["textCallbacks"] = callbacks, ["callbackFailures"] = failures, ["NeedsRender"] = pending, ["HasActiveTransitions"] = transitions
    };
    private sealed class FixtureComponent(string markup) : UiComponent { protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(markup); }
    internal sealed class CountingMeasurer : ITextMeasurer
    {
        private readonly ApproximateTextMeasurer inner = new();
        public long Calls { get; private set; }
        public long Failures { get; private set; }
        public TextMetrics MeasureText(string text, ComputedStyle style, float maxWidth = float.PositiveInfinity)
        {
            Calls++;
            try { var r = inner.MeasureText(text, style, maxWidth); Require(float.IsFinite(r.Width) && float.IsFinite(r.Height) && float.IsFinite(r.Baseline), "callback-nonfinite"); return r; }
            catch { Failures++; throw; }
        }
    }
    internal sealed class MemoryRenderer : IRenderBackend
    {
        public PaintCommand[] Commands { get; private set; } = [];
        public PaintChunk[] Chunks { get; private set; } = [];
        public int RenderCalls { get; private set; }
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList list, ColorRgba clearColor) { RenderCalls++; Commands = list.Commands.ToArray(); Chunks = list.PaintChunks.ToArray(); }
        public void Dispose() { }
    }
    private static int Synthetic(string mode)
    {
        session = "synthetic";
        Require(new[] { "normal", "hang", "excess-stdout", "excess-stderr", "malformed", "error-before-final", "error-after-final", "duplicate-final", "missing-end", "nonzero" }.Contains(mode, StringComparer.Ordinal), "synthetic-mode");
        if (mode == "hang") { Thread.Sleep(Timeout.Infinite); return 1; }
        if (mode == "excess-stdout" || mode == "excess-stderr") { (mode == "excess-stdout" ? Console.Out : Console.Error).Write(new string('x', 1048577)); return 0; }
        if (mode == "malformed") { Console.WriteLine("{"); return 0; }
        Event("session", new { schema = "headless-full-fixture-v1" });
        Phase("read"); Event("input-config", new { synthetic = true });
        Phase("constructor"); Phase("render"); Event("metrics-snapshot", new { synthetic = true });
        Phase("verification");
        if (mode == "error-before-final") Event("error", new { code = "synthetic-error", exceptionType = "ContractFailure" });
        Event("result", new { status = "measured" }); Event("metrics-final", new { status = "measured" });
        if (mode == "duplicate-final") Event("metrics-final", new { status = "measured" });
        if (mode == "error-after-final") Event("error", new { code = "synthetic-error", exceptionType = "ContractFailure" });
        Phase("dispose");
        if (mode != "missing-end") Event("session-end", new { status = "completed", reportingMs = 0d });
        return mode == "nonzero" ? 7 : 0;
    }
}

// A record of explicitly enumerated fields. No reflection or runtime-object serialization.
internal sealed record RecordValue(string Type, (string Name, object? Value)[] Fields);
internal readonly record struct EnumValue(string Type, long Value);
internal static class Values
{
    internal static RecordValue R(string type, params (string, object?)[] fields) => new(type, fields);
    internal static EnumValue E(string type, long value, long min, long max) { Program.Require(value >= min && value <= max, "enum-value"); return new(type, value); }
    internal static EnumValue Flags(string type, long value, long mask) { Program.Require(value >= 0 && (value & ~mask) == 0, "enum-flags"); return new(type, value); }
    internal static EnumValue Weight(FontWeight value) { long n = (long)value; Program.Require(n >= 100 && n <= 900 && n % 100 == 0, "enum-font-weight"); return new("AetherUI.Css.FontWeight", n); }
}

// Encoder buffers at most 32 KiB. Whole real streams are never materialized or retained.
internal sealed class Canon : IDisposable
{
    private readonly IncrementalHash hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
    private readonly byte[] buffer = new byte[32768];
    private readonly Stopwatch watch = Stopwatch.StartNew();
    private readonly MemoryStream? retained;
    private readonly long maxBytes;
    private int depth;
    private bool finished;
    internal long Bytes { get; private set; }
    internal string Domain { get; set; } = "prefix";
    internal Dictionary<string, long> FieldCounts { get; } = new(StringComparer.Ordinal);
    internal Canon(bool retain = false, long limit = 64L * 1024 * 1024) { maxBytes = limit; retained = retain ? new MemoryStream() : null; Put("FFD-CANON-1\0"u8); }
    private void Put(ReadOnlySpan<byte> bytes)
    {
        if (watch.Elapsed.TotalSeconds > 5) throw new ContractFailure("canonical-time-limit") { Domain = Domain, Bytes = Bytes };
        if (Bytes + bytes.Length > maxBytes) throw new ContractFailure("canonical-byte-limit") { Domain = Domain, Bytes = Bytes };
        Bytes += bytes.Length; hash.AppendData(bytes); retained?.Write(bytes);
    }
    private void Tag(byte tag) { Span<byte> b = stackalloc byte[1]; b[0] = tag; Put(b); }
    private void Count(int count) { Program.Require(count >= 0, "canonical-count"); Span<byte> b = stackalloc byte[4]; BinaryPrimitives.WriteUInt32LittleEndian(b, (uint)count); Put(b); }
    internal void Int(long n) { Tag(3); Span<byte> b = stackalloc byte[8]; BinaryPrimitives.WriteInt64LittleEndian(b, n); Put(b); }
    internal void String(string? s)
    {
        if (s is null) { Tag(0); return; }
        int size = Program.Utf8.GetByteCount(s); Program.Require(size <= 1048576, "canonical-string-limit"); Tag(6); Count(size);
        var encoder = Program.Utf8.GetEncoder(); ReadOnlySpan<char> remaining = s.AsSpan(); bool complete;
        do { encoder.Convert(remaining, buffer, true, out int used, out int written, out complete); Put(buffer.AsSpan(0, written)); remaining = remaining[used..]; } while (!complete);
    }
    internal void Array(int count, Action<int> emit)
    {
        Enter(); Tag(7); Count(count); for (int i = 0; i < count; i++) emit(i); depth--;
    }
    internal void Record(string type, params (string Name, Action Emit)[] fields)
    {
        Enter(); Tag(8); Count(fields.Length);
        string? previous = null;
        foreach (var f in fields.OrderBy(f => f.Name, StringComparer.Ordinal))
        {
            Program.Require(f.Name.Length > 0 && f.Name.All(c => c < 128) && previous != f.Name, "canonical-field"); previous = f.Name;
            String(f.Name); string key = type + "." + f.Name; FieldCounts[key] = FieldCounts.GetValueOrDefault(key) + 1; f.Emit();
        }
        depth--;
    }
    private void Enter() { depth++; Program.Require(depth <= 1024, "canonical-depth"); }
    internal void Value(object? value)
    {
        switch (value)
        {
            case null: Tag(0); break;
            case bool b: Tag(b ? (byte)2 : (byte)1); break;
            case int n: Int(n); break;
            case long n: Int(n); break;
            case float f:
                Program.Require(float.IsFinite(f), "canonical-nonfinite"); Tag(4); Span<byte> fb = stackalloc byte[4]; BinaryPrimitives.WriteInt32LittleEndian(fb, BitConverter.SingleToInt32Bits(f)); Put(fb); break;
            case double d:
                Program.Require(double.IsFinite(d), "canonical-nonfinite"); Tag(5); Span<byte> db = stackalloc byte[8]; BinaryPrimitives.WriteInt64LittleEndian(db, BitConverter.DoubleToInt64Bits(d)); Put(db); break;
            case string s: String(s); break;
            case EnumValue e:
                Program.Require(e.Type.Length > 0 && e.Type.All(c => c < 128), "canonical-enum-type"); Tag(9); String(e.Type); Span<byte> eb = stackalloc byte[8]; BinaryPrimitives.WriteInt64LittleEndian(eb, e.Value); Put(eb); break;
            case RecordValue r: Record(r.Type, r.Fields.Select(f => (f.Name, (Action)(() => Value(f.Value)))).ToArray()); break;
            case object?[] a: Array(a.Length, i => Value(a[i])); break;
            default: Value(Shapes.Convert(value)); break;
        }
    }
    internal (string Hash, byte[]? Retained) Finish()
    {
        Program.Require(!finished && depth == 0, "canonical-incomplete"); finished = true;
        return (Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant(), retained?.ToArray());
    }
    public void Dispose() { hash.Dispose(); retained?.Dispose(); }
}

internal static class Shapes
{
    internal static readonly IReadOnlyDictionary<string, string[]> PrimaryFields = new Dictionary<string, string[]>(StringComparer.Ordinal)
    {
        [nameof(FillRoundedRectangleCommand)] = ["Color", "Radii", "Rectangle"],
        [nameof(DrawBoxShadowCommand)] = ["BorderBoxRectangle", "Radii", "Shadow"],
        [nameof(FillRoundedRectangleGradientCommand)] = ["Gradient", "GradientRectangle", "Opacity", "Radii", "Rectangle"],
        [nameof(StrokeRoundedRectangleCommand)] = ["Color", "Radii", "Rectangle", "StrokeWidth"],
        [nameof(FillBorderSideCommand)] = ["Color", "InnerRadii", "InnerRectangle", "OuterRadii", "OuterRectangle", "Side", "Style"],
        [nameof(DrawTextCommand)] = ["Alignment", "Color", "FontFamily", "FontSize", "FontWeight", "Rectangle", "Text", "Wrap"],
        [nameof(DrawTextGradientCommand)] = ["Alignment", "FontFamily", "FontSize", "FontWeight", "Gradient", "Opacity", "Rectangle", "Text"],
        [nameof(DrawTextShadowCommand)] = ["Alignment", "FontFamily", "FontSize", "FontWeight", "Rectangle", "Shadow", "Text", "Wrap"],
        [nameof(ApplyBackdropFilterCommand)] = ["Filters", "Radii", "Rectangle"],
        [nameof(DrawBackgroundImageCommand)] = ["DestinationRectangle", "HasResolvedGeometry", "Opacity", "PaintRectangle", "PositionX", "PositionY", "RepeatX", "RepeatY", "Resource", "SizeKind", "SizeX", "SizeY", "Source", "SourceRectangle"],
        [nameof(PushClipCommand)] = ["Radii", "Rectangle"],
        [nameof(PopClipCommand)] = [],
        [nameof(PushTransformCommand)] = ["Matrix"],
        [nameof(PopTransformCommand)] = [],
        [nameof(PushScrollOffsetCommand)] = ["owner"],
        [nameof(PopScrollOffsetCommand)] = [],
        [nameof(DrawScrollbarChromeCommand)] = ["owner"],
    };

    internal static object Convert(object value) => value switch
    {
        RectF v => Values.R("RectF", ("X", v.X), ("Y", v.Y), ("Width", v.Width), ("Height", v.Height)),
        SizeF v => Values.R("SizeF", ("Width", v.Width), ("Height", v.Height)),
        ColorRgba v => Values.R("ColorRgba", ("R", v.R), ("G", v.G), ("B", v.B), ("A", v.A)),
        CornerRadii v => Values.R("CornerRadii", ("TopLeftX", v.TopLeftX), ("TopLeftY", v.TopLeftY), ("TopRightX", v.TopRightX), ("TopRightY", v.TopRightY), ("BottomRightX", v.BottomRightX), ("BottomRightY", v.BottomRightY), ("BottomLeftX", v.BottomLeftX), ("BottomLeftY", v.BottomLeftY)),
        TransformMatrix v => Values.R("TransformMatrix", ("M11", v.M11), ("M12", v.M12), ("M21", v.M21), ("M22", v.M22), ("Dx", v.Dx), ("Dy", v.Dy)),
        ScrollMetrics v => Values.R("ScrollMetrics", ("ScrollableOverflowX", v.ScrollableOverflowX), ("ScrollableOverflowY", v.ScrollableOverflowY), ("ScrollOffsetX", v.ScrollOffsetX), ("ScrollOffsetY", v.ScrollOffsetY)),
        ScrollbarGeometry v => Values.R("ScrollbarGeometry", ("HorizontalVisible", v.HorizontalVisible), ("VerticalVisible", v.VerticalVisible), ("HorizontalTrack", v.HorizontalTrack), ("HorizontalThumb", v.HorizontalThumb), ("VerticalTrack", v.VerticalTrack), ("VerticalThumb", v.VerticalThumb), ("Corner", v.Corner)),
        BoxShadow v => Values.R("BoxShadow", ("OffsetX", v.OffsetX), ("OffsetY", v.OffsetY), ("BlurRadius", v.BlurRadius), ("SpreadRadius", v.SpreadRadius), ("Color", v.Color), ("Inset", v.Inset)),
        LinearGradient v => Values.R("LinearGradient", ("DirectionX", v.DirectionX), ("DirectionY", v.DirectionY), ("Stops", v.Stops.Select(s => (object?)s).ToArray())),
        GradientStop v => Values.R("GradientStop", ("Color", v.Color), ("Offset", v.Offset)),
        CssLength v => Values.R("CssLength", ("Value", v.Value), ("Unit", Values.E("AetherUI.Css.CssUnit", (long)v.Unit, 0, 7)), ("LinearPercent", v.LinearPercent), ("LinearEm", v.LinearEm), ("LinearRem", v.LinearRem), ("LinearViewportWidth", v.LinearViewportWidth), ("LinearViewportHeight", v.LinearViewportHeight)),
        BackdropFilter v => Values.R("BackdropFilter", ("Kind", Values.E("AetherUI.Rendering.BackdropFilterKind", (long)v.Kind, 0, 3)), ("LengthAmount", v.LengthAmount), ("ScalarAmount", v.ScalarAmount)),
        _ => throw new ContractFailure("unsupported-value")
    };
    internal static RecordValue Command(PaintCommand command, Func<long, int> logical)
    {
        RecordValue v = command switch
        {
            FillRoundedRectangleCommand c => Values.R(nameof(FillRoundedRectangleCommand), ("Rectangle", c.Rectangle), ("Radii", c.Radii), ("Color", c.Color)),
            DrawBoxShadowCommand c => Values.R(nameof(DrawBoxShadowCommand), ("BorderBoxRectangle", c.BorderBoxRectangle), ("Radii", c.Radii), ("Shadow", c.Shadow)),
            FillRoundedRectangleGradientCommand c => Values.R(nameof(FillRoundedRectangleGradientCommand), ("Rectangle", c.Rectangle), ("Radii", c.Radii), ("GradientRectangle", c.GradientRectangle), ("Gradient", c.Gradient), ("Opacity", c.Opacity)),
            StrokeRoundedRectangleCommand c => Values.R(nameof(StrokeRoundedRectangleCommand), ("Rectangle", c.Rectangle), ("Radii", c.Radii), ("StrokeWidth", c.StrokeWidth), ("Color", c.Color)),
            FillBorderSideCommand c => Values.R(nameof(FillBorderSideCommand), ("OuterRectangle", c.OuterRectangle), ("OuterRadii", c.OuterRadii), ("InnerRectangle", c.InnerRectangle), ("InnerRadii", c.InnerRadii), ("Side", c.Side), ("Style", c.Style), ("Color", c.Color)),
            DrawTextCommand c => Values.R(nameof(DrawTextCommand), ("Text", c.Text), ("Rectangle", c.Rectangle), ("FontFamily", c.FontFamily), ("FontSize", c.FontSize), ("FontWeight", Values.Weight(c.FontWeight)), ("Alignment", Values.E("AetherUI.Css.TextAlign", (long)c.Alignment, 0, 5)), ("Color", c.Color), ("Wrap", c.Wrap)),
            DrawTextGradientCommand c => Values.R(nameof(DrawTextGradientCommand), ("Text", c.Text), ("Rectangle", c.Rectangle), ("FontFamily", c.FontFamily), ("FontSize", c.FontSize), ("FontWeight", Values.Weight(c.FontWeight)), ("Alignment", Values.E("AetherUI.Css.TextAlign", (long)c.Alignment, 0, 5)), ("Gradient", c.Gradient), ("Opacity", c.Opacity)),
            DrawTextShadowCommand c => Values.R(nameof(DrawTextShadowCommand), ("Text", c.Text), ("Rectangle", c.Rectangle), ("FontFamily", c.FontFamily), ("FontSize", c.FontSize), ("FontWeight", Values.Weight(c.FontWeight)), ("Alignment", Values.E("AetherUI.Css.TextAlign", (long)c.Alignment, 0, 5)), ("Shadow", c.Shadow), ("Wrap", c.Wrap)),
            ApplyBackdropFilterCommand c => Values.R(nameof(ApplyBackdropFilterCommand), ("Rectangle", c.Rectangle), ("Radii", c.Radii), ("Filters", c.Filters.Select(f => (object?)f).ToArray())),
            DrawBackgroundImageCommand c when c.Resource is null => Values.R(nameof(DrawBackgroundImageCommand), ("Source", c.Source), ("PaintRectangle", c.PaintRectangle), ("PositionX", c.PositionX), ("PositionY", c.PositionY), ("SizeKind", Values.E("AetherUI.Rendering.BackgroundImageSizeKind", (long)c.SizeKind, 0, 3)), ("SizeX", c.SizeX), ("SizeY", c.SizeY), ("RepeatX", Values.E("AetherUI.Rendering.BackgroundRepeat", (long)c.RepeatX, 0, 3)), ("RepeatY", Values.E("AetherUI.Rendering.BackgroundRepeat", (long)c.RepeatY, 0, 3)), ("Opacity", c.Opacity), ("SourceRectangle", c.SourceRectangle), ("DestinationRectangle", c.DestinationRectangle), ("HasResolvedGeometry", c.HasResolvedGeometry), ("Resource", null)),
            DrawBackgroundImageCommand => throw new ContractFailure("resolved-resource-forbidden"),
            PushClipCommand c => Values.R(nameof(PushClipCommand), ("Rectangle", c.Rectangle), ("Radii", c.Radii)),
            PopClipCommand => Values.R(nameof(PopClipCommand)),
            PushTransformCommand c => Values.R(nameof(PushTransformCommand), ("Matrix", c.Matrix)),
            PopTransformCommand => Values.R(nameof(PopTransformCommand)),
            PushScrollOffsetCommand c => Values.R(nameof(PushScrollOffsetCommand), ("owner", logical(c.ScrollContainerId))),
            PopScrollOffsetCommand => Values.R(nameof(PopScrollOffsetCommand)),
            DrawScrollbarChromeCommand c => Values.R(nameof(DrawScrollbarChromeCommand), ("owner", logical(c.ScrollContainerId))),
            _ => throw new ContractFailure("unsupported-command")
        };
        Program.Require(v.Fields.Select(f => f.Name).Order(StringComparer.Ordinal).SequenceEqual(PrimaryFields[v.Type]), "command-fields");
        return Values.R("command", ("kind", v.Type), ("values", v));
    }
}

internal sealed record Snapshot(object Digest, object Witnesses, double CanonicalMs, string Hash)
{
    internal static Snapshot Create(DocumentNode document, LayoutResult layout, Program.MemoryRenderer backend, Dictionary<string, object> counts, float dpi)
    {
        var canonicalWatch = Stopwatch.StartNew();
        var nodes = Traverse<Node>(document, n => n.Children);
        var ni = Index(nodes);
        foreach (Node n in nodes) foreach (Node child in n.Children) Program.Require(ReferenceEquals(child.Parent, n), "dom-parent");
        Program.Require(document.Parent is null, "dom-root-parent");
        var boxes = Traverse<LayoutBox>(layout.Root, b => b.Children);
        var bi = Index(boxes);
        var parents = new Dictionary<LayoutBox, LayoutBox>(ReferenceEqualityComparer.Instance);
        foreach (LayoutBox box in boxes)
        {
            Program.Require(ni.ContainsKey(box.Element), "orphan-element");
            foreach (LayoutBox child in box.Children) Program.Require(parents.TryAdd(child, box), "box-parent");
        }
        Program.Require(!parents.ContainsKey(layout.Root) && parents.Count == boxes.Count - 1, "box-root-parent");
        foreach (var box in layout.PaintOrder) Program.Require(bi.ContainsKey(box), "orphan-paint-box");
        var ids = nodes.OfType<ElementNode>().Where(e => e.Id?.StartsWith("FLEX-", StringComparison.Ordinal) == true).Select(e => e.Id!).Order(StringComparer.Ordinal).ToArray();
        Program.Require(ids.SequenceEqual(Enumerable.Range(1, 403).Select(i => "FLEX-" + i.ToString("D3", CultureInfo.InvariantCulture))), "fixture-ids");
        var images = nodes.OfType<ElementNode>().Where(e => e.TagName == "img").ToArray();
        Program.Require(images.Length == 3 && images.All(e => e.GetAttribute("src")?.StartsWith("data:image/svg+xml", StringComparison.Ordinal) == true), "fixture-images");
        var owners = new Dictionary<long, int>();
        int Logical(long raw) { if (!owners.TryGetValue(raw, out int index)) owners.Add(raw, index = owners.Count); return index; }
        foreach (var chunk in backend.Chunks) { Logical(chunk.StyleNodeId); Program.Require(chunk.Start >= 0 && chunk.End >= chunk.Start && chunk.End <= backend.Commands.Length && chunk.Depth >= 0, "chunk-range"); }
        foreach (var command in backend.Commands)
        {
            if (command is PushScrollOffsetCommand s) Logical(s.ScrollContainerId);
            if (command is DrawScrollbarChromeCommand c) Logical(c.ScrollContainerId);
        }
        var commandTypes = new Dictionary<string, int>(StringComparer.Ordinal);
        using var encoder = new Canon();
        encoder.Record("root",
            ("boxes", () => { encoder.Domain = "boxes"; encoder.Array(boxes.Count, i => {
                var b = boxes[i]; encoder.Record("box", ("index", () => encoder.Int(i)), ("parent", () => encoder.Value(parents.TryGetValue(b, out var p) ? bi[p] : null)),
                    ("children", () => encoder.Array(b.Children.Count, j => encoder.Int(bi[b.Children[j]]))), ("element", () => encoder.Int(ni[b.Element])),
                    ("Kind", () => encoder.Value(Values.E("AetherUI.Layout.LayoutBoxKind", (long)b.Kind, 0, 1))), ("Text", () => encoder.String(b.Text)),
                    ("MarginBox", () => encoder.Value(b.MarginBox)), ("BorderBox", () => encoder.Value(b.BorderBox)), ("PaddingBox", () => encoder.Value(b.PaddingBox)), ("ContentBox", () => encoder.Value(b.ContentBox)),
                    ("IsAtomicControl", () => encoder.Value(b.IsAtomicControl)), ("Scroll", () => encoder.Value(b.Scroll)), ("Scrollbar", () => encoder.Value(b.Scrollbar)));
            }); }),
            ("chunks", () => { encoder.Domain = "chunks"; encoder.Array(backend.Chunks.Length, i => { var c = backend.Chunks[i]; encoder.Value(Values.R("chunk", ("owner", Logical(c.StyleNodeId)), ("Start", c.Start), ("End", c.End), ("Bounds", c.Bounds), ("Depth", c.Depth))); }); }),
            ("commands", () => { encoder.Domain = "commands"; encoder.Array(backend.Commands.Length, i => { var record = Shapes.Command(backend.Commands[i], Logical); var kind = (string)record.Fields[0].Value!; commandTypes[kind] = commandTypes.GetValueOrDefault(kind) + 1; encoder.Value(record); }); }),
            ("counts", () => { encoder.Domain = "counts"; encoder.Record("counts", counts.Select(kv => (kv.Key, (Action)(() => encoder.Value(kv.Key == "LastStyleImpact" ? Values.Flags("AetherUI.Core.Native.NativeStyleImpact", (long)kv.Value, 127) : kv.Value)))).Concat(new[] {
                ("dom", (Action)(() => encoder.Int(nodes.Count))), ("boxes", (Action)(() => encoder.Int(boxes.Count))), ("chunks", (Action)(() => encoder.Int(backend.Chunks.Length))), ("commands", (Action)(() => encoder.Int(backend.Commands.Length))) }).ToArray()); }),
            ("dom", () => { encoder.Domain = "dom"; encoder.Array(nodes.Count, i => {
                Node n = nodes[i];
                Program.Require(n is DocumentNode or ElementNode or TextNode or CommentNode, "unsupported-node");
                encoder.Record("node", ("index", () => encoder.Int(i)), ("NodeType", () => encoder.Value(Values.E("AetherUI.Dom.NodeType", (long)n.NodeType, 0, 3))),
                    ("parent", () => encoder.Value(n.Parent is null ? null : ni[n.Parent])), ("children", () => encoder.Array(n.Children.Count, j => encoder.Int(ni[n.Children[j]]))),
                    ("text", () => encoder.String(n is TextNode t ? t.TextContent : n is CommentNode c ? c.Text : null)),
                    ("element", () => {
                        if (n is not ElementNode e) { encoder.Value(null); return; }
                        var attrs = e.Attributes.OrderBy(kv => kv.Key, StringComparer.Ordinal).ToArray();
                        Program.Require(attrs.Select(a => a.Key).Distinct(StringComparer.Ordinal).Count() == attrs.Length, "duplicate-attribute");
                        encoder.Record("element", ("TagName", () => encoder.String(e.TagName)), ("Id", () => encoder.String(e.Id)),
                            ("attributes", () => encoder.Array(attrs.Length, j => encoder.Value(Values.R("attribute", ("name", attrs[j].Key), ("value", attrs[j].Value))))),
                            ("State", () => encoder.Value(Values.Flags("AetherUI.Dom.ElementState", (long)e.State, 15))), ("RuntimeValue", () => encoder.String(e.RuntimeValue)),
                            ("primaryBox", () => encoder.Value(layout.FindPrimaryBox(e) is { } b ? bi[b] : null)),
                            ("paintBoxes", () => { var paint = layout.FindPaintBoxes(e); encoder.Array(paint.Count, j => encoder.Int(bi[paint[j]])); }));
                    }));
            }); }),
            ("paintOrder", () => { encoder.Domain = "paintOrder"; encoder.Array(layout.PaintOrder.Count, i => encoder.Int(bi[layout.PaintOrder[i]])); }),
            ("viewport", () => { encoder.Domain = "viewport"; encoder.Value(Values.R("viewport", ("Width", layout.Viewport.Width), ("Height", layout.Viewport.Height), ("DpiScale", dpi),
                ("OverflowX", Values.E("AetherUI.Css.OverflowKind", (long)layout.ViewportOverflowX, 0, 4)), ("OverflowY", Values.E("AetherUI.Css.OverflowKind", (long)layout.ViewportOverflowY, 0, 4)))); }));
        foreach (var (kind, count) in commandTypes)
            foreach (var field in Shapes.PrimaryFields[kind])
                Program.Require(encoder.FieldCounts.GetValueOrDefault(kind + "." + field) == count, "active-command-field-count");
        var digest = encoder.Finish();
        double canonicalMs = canonicalWatch.Elapsed.TotalMilliseconds;
        Program.Require(canonicalMs <= 5000, "canonical-time-limit");
        var witnesses = new List<object>();
        using var witnessEncoder = new Canon(true, 65536);
        var sentinels = new List<(string Name, ElementNode Element)>();
        foreach (var id in new[] { "FLEX-001", "FLEX-200", "FLEX-403" })
        {
            var articles = nodes.OfType<ElementNode>().Where(e => e.TagName == "article" && e.Id == id).ToArray();
            Program.Require(articles.Length == 1, "sentinel-article");
            var stages = articles[0].Children.OfType<ElementNode>().Where(e => e.TagName == "div" && e.HasClass("stage")).ToArray();
            Program.Require(stages.Length == 1, "sentinel-stage");
            var targets = stages[0].Children.OfType<ElementNode>().Where(e => e.TagName == "div" && e.HasClass("flex-target")).ToArray();
            Program.Require(targets.Length == 1, "sentinel-target");
            sentinels.Add((id, articles[0])); sentinels.Add((id + "/stage", stages[0])); sentinels.Add((id + "/stage/flex-target", targets[0]));
        }
        var textCommand = backend.Commands.First(c => c is DrawTextCommand or DrawTextGradientCommand or DrawTextShadowCommand);
        (string Text, RectF Rectangle) textWitness = textCommand switch {
            DrawTextCommand c => (c.Text, c.Rectangle), DrawTextGradientCommand c => (c.Text, c.Rectangle), DrawTextShadowCommand c => (c.Text, c.Rectangle), _ => throw new ContractFailure("missing-text") };
        witnessEncoder.Record("witness", ("textWitness", () => witnessEncoder.Value(Values.R("textWitness", ("text", textWitness.Text), ("Rectangle", textWitness.Rectangle)))), ("schema", () => witnessEncoder.String("WITNESS-1")), ("sentinels", () => witnessEncoder.Array(sentinels.Count, i => {
            var (name, element) = sentinels[i]; var box = layout.FindPrimaryBox(element);
            Program.Require(box is not null && ReferenceEquals(box.Element, element) && bi.ContainsKey(box), "sentinel-box");
            Program.Require(box!.BorderBox.Width > 0 && box.BorderBox.Height > 0, "sentinel-area");
            int? parent = parents.TryGetValue(box, out var p) ? bi[p] : null;
            var record = Values.R("sentinel", ("name", name), ("node", ni[element]), ("box", bi[box]), ("parent", parent), ("BorderBox", box.BorderBox), ("ContentBox", box.ContentBox));
            witnessEncoder.Value(record);
            witnesses.Add(new { name, node = ni[element], box = bi[box], parent, borderBox = RectJson(box.BorderBox), contentBox = RectJson(box.ContentBox) });
        })), ("counts", () => witnessEncoder.Value(Values.R("cardinalities", ("dom", nodes.Count), ("boxes", boxes.Count), ("commands", backend.Commands.Length), ("chunks", backend.Chunks.Length), ("paintOrder", layout.PaintOrder.Count)))));
        var witness = witnessEncoder.Finish();
        var decoded = Decoder.Read(witness.Retained!);
        Program.Require(decoded is Dictionary<string, object?>, "witness-decode");
        var root = (Dictionary<string, object?>)decoded!;
        Program.Require(root["schema"] is string version && version == "WITNESS-1", "witness-version");
        var decodedSentinels = (object?[])root["sentinels"]!;
        Program.Require(decodedSentinels.Length == 9, "witness-count");
        for (int i = 0; i < sentinels.Count; i++)
        {
            var record = (Dictionary<string, object?>)decodedSentinels[i]!;
            var element = sentinels[i].Element; var box = layout.FindPrimaryBox(element)!;
            Program.Require((string)record["name"]! == sentinels[i].Name && (long)record["node"]! == ni[element] && (long)record["box"]! == bi[box], "witness-active-index");
            int? parent = parents.TryGetValue(box, out var parentBox) ? bi[parentBox] : null;
            Program.Require(parent is null ? record["parent"] is null : record["parent"] is long index && index == parent, "witness-active-parent");
            CheckRect(record["BorderBox"], box.BorderBox); CheckRect(record["ContentBox"], box.ContentBox);
        }
        var decodedText = (Dictionary<string, object?>)root["textWitness"]!;
        Program.Require((string)decodedText["text"]! == textWitness.Text, "witness-active-text"); CheckRect(decodedText["Rectangle"], textWitness.Rectangle);
        var decodedCounts = (Dictionary<string, object?>)root["counts"]!;
        Program.Require((long)decodedCounts["dom"]! == nodes.Count && (long)decodedCounts["boxes"]! == boxes.Count && (long)decodedCounts["commands"]! == backend.Commands.Length && (long)decodedCounts["chunks"]! == backend.Chunks.Length && (long)decodedCounts["paintOrder"]! == layout.PaintOrder.Count, "witness-active-counts");
        Program.Require(encoder.FieldCounts["node.index"] == nodes.Count && encoder.FieldCounts["box.index"] == boxes.Count && encoder.FieldCounts["command.kind"] == backend.Commands.Length, "active-emission-counts");
        return new Snapshot(new { schema = "FFD-CANON-1", sha256 = digest.Hash, bytes = encoder.Bytes, scope = "declared-full-stream-sha256; stream-not-retained",
            cardinalities = new { dom = nodes.Count, boxes = boxes.Count, paintOrder = layout.PaintOrder.Count, chunks = backend.Chunks.Length, commands = backend.Commands.Length }, commandTypes, fieldCounts = encoder.FieldCounts },
            new { schema = "WITNESS-1", sentinels = witnesses, canonicalBase64 = System.Convert.ToBase64String(witness.Retained!), sha256 = witness.Hash, bytes = witness.Retained!.Length }, canonicalMs, digest.Hash);
    }
    private static void CheckRect(object? decoded, RectF active)
    {
        var r = decoded as Dictionary<string, object?> ?? throw new ContractFailure("witness-rect");
        Program.Require(r.Count == 4 && Bits(r["X"], active.X) && Bits(r["Y"], active.Y) && Bits(r["Width"], active.Width) && Bits(r["Height"], active.Height), "witness-active-geometry");
    }
    private static bool Bits(object? value, float expected) => value is float actual && BitConverter.SingleToInt32Bits(actual) == BitConverter.SingleToInt32Bits(expected);
    private static object RectJson(RectF r) => new { r.X, r.Y, r.Width, r.Height };
    private static List<T> Traverse<T>(T root, Func<T, IReadOnlyList<T>> children) where T : class
    {
        var result = new List<T>(); var seen = new HashSet<T>(ReferenceEqualityComparer.Instance); var stack = new Stack<T>(); stack.Push(root);
        while (stack.Count > 0) { var item = stack.Pop(); Program.Require(seen.Add(item), "tree-cycle-or-duplicate"); result.Add(item); var list = children(item); for (int i = list.Count - 1; i >= 0; i--) stack.Push(list[i]); }
        return result;
    }
    private static Dictionary<T, int> Index<T>(IReadOnlyList<T> list) where T : class { var r = new Dictionary<T, int>(ReferenceEqualityComparer.Instance); for (int i = 0; i < list.Count; i++) r.Add(list[i], i); return r; }
}

// Decoder is independent of Canon: it consumes retained test/witness bytes, not encoder callbacks.
internal sealed class Decoder
{
    private readonly byte[] bytes;
    private int at;
    private Decoder(byte[] input) { bytes = input; }
    internal static object? Read(byte[] bytes)
    {
        Program.Require(bytes.Length <= 1048576 && bytes.AsSpan().StartsWith("FFD-CANON-1\0"u8), "decode-prefix");
        var decoder = new Decoder(bytes) { at = 12 };
        var value = decoder.Next(0);
        Program.Require(decoder.at == bytes.Length, "decode-trailing"); return value;
    }
    private byte Byte() { Program.Require(at < bytes.Length, "decode-eof"); return bytes[at++]; }
    private ulong Unsigned(int width) { ulong n = 0; for (int i = 0; i < width; i++) n |= (ulong)Byte() << (i * 8); return n; }
    private object? Next(int depth)
    {
        Program.Require(depth <= 1024, "decode-depth");
        switch (Byte())
        {
            case 0: return null;
            case 1: return false;
            case 2: return true;
            case 3: return unchecked((long)Unsigned(8));
            case 4: { float f = BitConverter.Int32BitsToSingle(unchecked((int)Unsigned(4))); Program.Require(float.IsFinite(f), "decode-nonfinite"); return f; }
            case 5: { double d = BitConverter.Int64BitsToDouble(unchecked((long)Unsigned(8))); Program.Require(double.IsFinite(d), "decode-nonfinite"); return d; }
            case 6: { ulong length = Unsigned(4); Program.Require(length <= 1048576 && length <= (ulong)(bytes.Length - at), "decode-string"); string s = Program.Utf8.GetString(bytes, at, (int)length); at += (int)length; return s; }
            case 7: { ulong count = Unsigned(4); Program.Require(count <= (ulong)(bytes.Length - at), "decode-array"); var list = new object?[(int)count]; for (int i = 0; i < list.Length; i++) list[i] = Next(depth + 1); return list; }
            case 8:
            {
                ulong count = Unsigned(4); Program.Require(count <= (ulong)(bytes.Length - at), "decode-record");
                var r = new Dictionary<string, object?>(StringComparer.Ordinal); string? previous = null;
                for (ulong i = 0; i < count; i++) { var key = Next(depth + 1) as string; Program.Require(key is not null && key.All(c => c < 128) && (previous is null || string.CompareOrdinal(previous, key) < 0), "decode-key"); previous = key; r.Add(key!, Next(depth + 1)); }
                return r;
            }
            case 9: { var name = Next(depth + 1) as string; Program.Require(name is not null && name.All(c => c < 128), "decode-enum"); return new EnumValue(name!, unchecked((long)Unsigned(8))); }
            default: throw new ContractFailure("decode-tag");
        }
    }
}

internal static class ContractTests
{
    private static readonly List<object> vectors = [];
    private static readonly Dictionary<string, byte[]> retainedVectors = new(StringComparer.Ordinal);
    private static readonly List<string> checks = [];
    private static byte[] Encode(object? value)
    {
        using var c = new Canon(true, 1048576); c.Value(value); var result = c.Finish();
        Program.Require(Program.Hash(result.Retained!) == result.Hash, "contract-hash"); Decoder.Read(result.Retained!); return result.Retained!;
    }
    private static void Vector(string name, object? value, string? literal = null, string? expectedHash = null)
    {
        byte[] actual = Encode(value); string hex = Convert.ToHexString(actual).ToLowerInvariant(); string sha256 = Program.Hash(actual);
        if (literal is not null) Program.Require(hex == literal && sha256 == expectedHash, "literal-vector");
        retainedVectors.Add(name, actual);
        vectors.Add(new { name, hex, sha256, expectedHex = literal, expectedSha256 = expectedHash });
    }
    private static void Reject(string name, Action action)
    {
        bool failed = false; try { action(); } catch (ContractFailure) { failed = true; } catch (EncoderFallbackException) { failed = true; } catch (DecoderFallbackException) { failed = true; }
        Program.Require(failed, "negative-accepted-" + name); checks.Add(name);
    }
    private static void Different(string name, object? a, object? b)
    {
        Program.Require(!Encode(a).SequenceEqual(Encode(b)), "mutation-missed-" + name); checks.Add(name);
    }
    private static void Same(string name, object? a, object? b)
    {
        Program.Require(Encode(a).SequenceEqual(Encode(b)), "invariance-missed-" + name); checks.Add(name);
    }
    internal static object Structure() => Values.R("root",
        ("boxes", new object?[] { Values.R("box", ("children", new object?[] { 1 }), ("parent", null), ("X", 1f)), Values.R("box", ("children", Array.Empty<object?>()), ("parent", 0), ("X", 2f)) }),
        ("chunks", new object?[] { Values.R("chunk", ("owner", 0), ("Start", 0), ("End", 1)), Values.R("chunk", ("owner", 0), ("Start", 1), ("End", 2)) }),
        ("commands", new object?[] { Shapes.Command(new PushScrollOffsetCommand(0), _ => 0) }),
        ("dom", new object?[] { Values.R("node", ("children", new object?[] { 1 }), ("parent", null), ("text", "parent")), Values.R("node", ("children", Array.Empty<object?>()), ("parent", 0), ("text", "child")) }),
        ("paintOrder", new object?[] { 0, 1 }));
    internal static void Run(string output)
    {
        Vector("null", null, "4646442d43414e4f4e2d310000", "5b84b991559b927f207294b51a183772b8457a8ea4e58a7f35db155968d65e24");
        Vector("plus-zero", 0f, "4646442d43414e4f4e2d31000400000000", "f8b5beb66527ee0fd04ea2ce718cce64d7f8ec115eb2707dda071e209b1ef1f0");
        Vector("minus-zero", BitConverter.Int32BitsToSingle(int.MinValue), "4646442d43414e4f4e2d31000400000080", "916d6bca33004da3b1ae0f86e16a18ebe9a60aad05a79ae498f1bf4a0975b9d6");
        Vector("e-acute", "é", "4646442d43414e4f4e2d31000602000000c3a9", "dbfcf1edd83782d2da6146957296e8c58e37ed427ab2257be5e6af077406d3b6");
        Vector("structure", Structure(), "4646442d43414e4f4e2d310008050000000605000000626f78657307020000000803000000060100000058040000803f06080000006368696c6472656e07010000000301000000000000000606000000706172656e74000803000000060100000058040000004006080000006368696c6472656e07000000000606000000706172656e7403000000000000000006060000006368756e6b73070200000008030000000603000000456e640301000000000000000605000000537461727403000000000000000006050000006f776e657203000000000000000008030000000603000000456e640302000000000000000605000000537461727403010000000000000006050000006f776e65720300000000000000000608000000636f6d6d616e64730701000000080200000006040000006b696e640617000000507573685363726f6c6c4f6666736574436f6d6d616e64060600000076616c756573080100000006050000006f776e65720300000000000000000603000000646f6d0702000000080300000006080000006368696c6472656e07010000000301000000000000000606000000706172656e74000604000000746578740606000000706172656e74080300000006080000006368696c6472656e07000000000606000000706172656e7403000000000000000006040000007465787406050000006368696c64060a0000007061696e744f726465720702000000030000000000000000030100000000000000", "cc293997382a11e449b8534ef72f36e3b7b9a97a7c969b57993008930d47866b");
        Same("record-insertion-order", Values.R("r", ("b", 2), ("a", 1)), Values.R("r", ("a", 1), ("b", 2)));
        Same("logical-renumber", Owners([41, 41], [41]), Owners([902, 902], [902]));
        Different("owner-aliasing", Owners([41, 41], [41]), Owners([41, 42], [41]));
        Different("array-order", new object?[] { 0, 1 }, new object?[] { 1, 0 });
        Different("null-vs-empty", null, ""); Different("signed-zero", 0f, BitConverter.Int32BitsToSingle(int.MinValue));
        Reject("nan32", () => Encode(float.NaN)); Reject("infinity32", () => Encode(float.PositiveInfinity));
        Reject("nan64", () => Encode(double.NaN)); Reject("infinity64", () => Encode(double.NegativeInfinity));
        Reject("invalid-utf16", () => Encode("\ud800")); Reject("unknown-value", () => Encode(new object()));
        Reject("duplicate-fields", () => Encode(Values.R("r", ("a", 1), ("a", 2))));
        Reject("nonascii-field", () => Encode(Values.R("r", ("é", 1))));
        Reject("enum-invalid", () => Values.E("type", 9, 0, 2)); Reject("flags-invalid", () => Values.Flags("type", 16, 15));
        Reject("bytes-limit", () => { using var c = new Canon(false, 12); c.Value("too-long"); });
        Reject("string-limit", () => Encode(new string('x', 1048577)));
        Reject("depth-limit", () => { object? v = null; for (int i = 0; i < 1026; i++) v = new object?[] { v }; Encode(v); });
        Reject("decode-invalid-utf8", () => Decoder.Read(Convert.FromHexString("4646442d43414e4f4e2d31000601000000ff")));
        Reject("decode-unknown-tag", () => Decoder.Read(Convert.FromHexString("4646442d43414e4f4e2d3100ff")));
        Reject("decode-trailing", () => Decoder.Read(Convert.FromHexString("4646442d43414e4f4e2d31000000")));
        CommandTests(); CompositeTests(); SyntheticGraphs.Run(checks); Program.CheckFixtureContracts(checks);
        using (var process = Process.GetCurrentProcess())
            Program.Require(!process.Modules.Cast<ProcessModule>().Any(m => string.Equals(m.ModuleName, "aether_core.dll", StringComparison.OrdinalIgnoreCase)), "synthetic-native-loaded");
        Program.WriteJson(output, new { schema = "FFD-VECTORS-1", status = "passed", vectors, checks }, 1048576);
    }
    private static object Owners(long[] chunks, long[] commands)
    {
        var ids = new Dictionary<long, int>();
        int Id(long raw) { if (!ids.TryGetValue(raw, out int i)) ids.Add(raw, i = ids.Count); return i; }
        var c = chunks.Select(x => (object?)Id(x)).ToArray(); var d = commands.Select(x => (object?)Id(x)).ToArray(); return Values.R("owners", ("chunks", c), ("commands", d));
    }
    private static void CommandMutation(string field, PaintCommand before, PaintCommand after)
    {
        var a = Shapes.Command(before, x => checked((int)x)); var b = Shapes.Command(after, x => checked((int)x));
        Different(before.GetType().Name + "." + field, a, b);
        byte[] bytes = Encode(b);
        var root = (Dictionary<string, object?>)Decoder.Read(bytes)!;
        var values = (Dictionary<string, object?>)root["values"]!;
        Program.Require(values.ContainsKey(field), "mutation-field-missing");
        vectors.Add(new { name = before.GetType().Name + "." + field, hex = Convert.ToHexString(bytes).ToLowerInvariant(), sha256 = Program.Hash(bytes), expectedHex = (string?)null, expectedSha256 = (string?)null });
    }
    private static void CompositeMutation(string name, object a, object b)
    {
        Different(name, a, b); Vector(name + ".before", a); Vector(name + ".after", b);
    }
    private static void CommandTests()
    {
        var rect = new RectF(-1, 2, 3, 4); var rect2 = rect with { X = -2 };
        var radii = new CornerRadii(1,2,3,4,5,6,7,8); var radii2 = radii with { TopLeftX = 2 };
        var color = new ColorRgba(.1f,.2f,.3f,.4f); var color2 = color with { R = .9f };
        var shadow = new BoxShadow(-1,2,3,4,color,false); var shadow2 = shadow with { OffsetX = -2 };
        var gradient = new LinearGradient(1,2,new[]{new GradientStop(color,.25f),new GradientStop(color2,.75f)});
        var gradient2 = gradient with { DirectionX = 2 };
        var length = new CssLength(1,CssUnit.Linear,2,3,4,5,6); var length2 = length with { Value=2 };
        var filter = new BackdropFilter(BackdropFilterKind.Blur,length,1f);
        var matrix = new TransformMatrix(1,2,3,4,5,6);
        var types = new HashSet<string>(StringComparer.Ordinal);
        var c0 = new FillRoundedRectangleCommand(rect, radii, color);
        types.Add(nameof(FillRoundedRectangleCommand)); Vector(nameof(FillRoundedRectangleCommand), Shapes.Command(c0, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c0, x => checked((int)x)).Fields[1].Value!).Fields.Length == 3, "command-field-count");
        CommandMutation("Rectangle", c0, c0 with { Rectangle = rect2 });
        CommandMutation("Radii", c0, c0 with { Radii = radii2 });
        CommandMutation("Color", c0, c0 with { Color = color2 });
        var c1 = new DrawBoxShadowCommand(rect, radii, shadow);
        types.Add(nameof(DrawBoxShadowCommand)); Vector(nameof(DrawBoxShadowCommand), Shapes.Command(c1, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c1, x => checked((int)x)).Fields[1].Value!).Fields.Length == 3, "command-field-count");
        CommandMutation("BorderBoxRectangle", c1, c1 with { BorderBoxRectangle = rect2 });
        CommandMutation("Radii", c1, c1 with { Radii = radii2 });
        CommandMutation("Shadow", c1, c1 with { Shadow = shadow2 });
        var c2 = new FillRoundedRectangleGradientCommand(rect, radii, rect, gradient, .5f);
        types.Add(nameof(FillRoundedRectangleGradientCommand)); Vector(nameof(FillRoundedRectangleGradientCommand), Shapes.Command(c2, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c2, x => checked((int)x)).Fields[1].Value!).Fields.Length == 5, "command-field-count");
        CommandMutation("Rectangle", c2, c2 with { Rectangle = rect2 });
        CommandMutation("Radii", c2, c2 with { Radii = radii2 });
        CommandMutation("GradientRectangle", c2, c2 with { GradientRectangle = rect2 });
        CommandMutation("Gradient", c2, c2 with { Gradient = gradient2 });
        CommandMutation("Opacity", c2, c2 with { Opacity = .6f });
        var c3 = new StrokeRoundedRectangleCommand(rect, radii, 1f, color);
        types.Add(nameof(StrokeRoundedRectangleCommand)); Vector(nameof(StrokeRoundedRectangleCommand), Shapes.Command(c3, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c3, x => checked((int)x)).Fields[1].Value!).Fields.Length == 4, "command-field-count");
        CommandMutation("Rectangle", c3, c3 with { Rectangle = rect2 });
        CommandMutation("Radii", c3, c3 with { Radii = radii2 });
        CommandMutation("StrokeWidth", c3, c3 with { StrokeWidth = 2f });
        CommandMutation("Color", c3, c3 with { Color = color2 });
        var c4 = new FillBorderSideCommand(rect,radii,rect,radii,0,0,color);
        types.Add(nameof(FillBorderSideCommand)); Vector(nameof(FillBorderSideCommand), Shapes.Command(c4, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c4, x => checked((int)x)).Fields[1].Value!).Fields.Length == 7, "command-field-count");
        CommandMutation("OuterRectangle", c4, c4 with { OuterRectangle = rect2 });
        CommandMutation("OuterRadii", c4, c4 with { OuterRadii = radii2 });
        CommandMutation("InnerRectangle", c4, c4 with { InnerRectangle = rect2 });
        CommandMutation("InnerRadii", c4, c4 with { InnerRadii = radii2 });
        CommandMutation("Side", c4, c4 with { Side = 1 });
        CommandMutation("Style", c4, c4 with { Style = 1 });
        CommandMutation("Color", c4, c4 with { Color = color2 });
        var c5 = new DrawTextCommand("text",rect,"sans",12f,FontWeight.Normal,TextAlign.Start,color,false);
        types.Add(nameof(DrawTextCommand)); Vector(nameof(DrawTextCommand), Shapes.Command(c5, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c5, x => checked((int)x)).Fields[1].Value!).Fields.Length == 8, "command-field-count");
        CommandMutation("Text", c5, c5 with { Text = "changed" });
        CommandMutation("Rectangle", c5, c5 with { Rectangle = rect2 });
        CommandMutation("FontFamily", c5, c5 with { FontFamily = "serif" });
        CommandMutation("FontSize", c5, c5 with { FontSize = 13f });
        CommandMutation("FontWeight", c5, c5 with { FontWeight = FontWeight.Bold });
        CommandMutation("Alignment", c5, c5 with { Alignment = TextAlign.End });
        CommandMutation("Color", c5, c5 with { Color = color2 });
        CommandMutation("Wrap", c5, c5 with { Wrap = true });
        var c6 = new DrawTextGradientCommand("text",rect,"sans",12f,FontWeight.Normal,TextAlign.Start,gradient,.5f);
        types.Add(nameof(DrawTextGradientCommand)); Vector(nameof(DrawTextGradientCommand), Shapes.Command(c6, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c6, x => checked((int)x)).Fields[1].Value!).Fields.Length == 8, "command-field-count");
        CommandMutation("Text", c6, c6 with { Text = "changed" });
        CommandMutation("Rectangle", c6, c6 with { Rectangle = rect2 });
        CommandMutation("FontFamily", c6, c6 with { FontFamily = "serif" });
        CommandMutation("FontSize", c6, c6 with { FontSize = 13f });
        CommandMutation("FontWeight", c6, c6 with { FontWeight = FontWeight.Bold });
        CommandMutation("Alignment", c6, c6 with { Alignment = TextAlign.End });
        CommandMutation("Gradient", c6, c6 with { Gradient = gradient2 });
        CommandMutation("Opacity", c6, c6 with { Opacity = .6f });
        var c7 = new DrawTextShadowCommand("text",rect,"sans",12f,FontWeight.Normal,TextAlign.Start,shadow,false);
        types.Add(nameof(DrawTextShadowCommand)); Vector(nameof(DrawTextShadowCommand), Shapes.Command(c7, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c7, x => checked((int)x)).Fields[1].Value!).Fields.Length == 8, "command-field-count");
        CommandMutation("Text", c7, c7 with { Text = "changed" });
        CommandMutation("Rectangle", c7, c7 with { Rectangle = rect2 });
        CommandMutation("FontFamily", c7, c7 with { FontFamily = "serif" });
        CommandMutation("FontSize", c7, c7 with { FontSize = 13f });
        CommandMutation("FontWeight", c7, c7 with { FontWeight = FontWeight.Bold });
        CommandMutation("Alignment", c7, c7 with { Alignment = TextAlign.End });
        CommandMutation("Shadow", c7, c7 with { Shadow = shadow2 });
        CommandMutation("Wrap", c7, c7 with { Wrap = true });
        var c8 = new ApplyBackdropFilterCommand(rect,radii,new[]{filter});
        types.Add(nameof(ApplyBackdropFilterCommand)); Vector(nameof(ApplyBackdropFilterCommand), Shapes.Command(c8, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c8, x => checked((int)x)).Fields[1].Value!).Fields.Length == 3, "command-field-count");
        CommandMutation("Rectangle", c8, c8 with { Rectangle = rect2 });
        CommandMutation("Radii", c8, c8 with { Radii = radii2 });
        CommandMutation("Filters", c8, c8 with { Filters = new[]{filter with {ScalarAmount=2f}} });
        var c9 = new DrawBackgroundImageCommand("data:image/svg+xml,x",rect,length,length,BackgroundImageSizeKind.Auto,length,length,BackgroundRepeat.Repeat,BackgroundRepeat.Repeat,.5f,rect,rect,false,null);
        types.Add(nameof(DrawBackgroundImageCommand)); Vector(nameof(DrawBackgroundImageCommand), Shapes.Command(c9, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c9, x => checked((int)x)).Fields[1].Value!).Fields.Length == 14, "command-field-count");
        CommandMutation("Source", c9, c9 with { Source = "changed" });
        CommandMutation("PaintRectangle", c9, c9 with { PaintRectangle = rect2 });
        CommandMutation("PositionX", c9, c9 with { PositionX = length2 });
        CommandMutation("PositionY", c9, c9 with { PositionY = length2 });
        CommandMutation("SizeKind", c9, c9 with { SizeKind = BackgroundImageSizeKind.Cover });
        CommandMutation("SizeX", c9, c9 with { SizeX = length2 });
        CommandMutation("SizeY", c9, c9 with { SizeY = length2 });
        CommandMutation("RepeatX", c9, c9 with { RepeatX = BackgroundRepeat.NoRepeat });
        CommandMutation("RepeatY", c9, c9 with { RepeatY = BackgroundRepeat.NoRepeat });
        CommandMutation("Opacity", c9, c9 with { Opacity = .6f });
        CommandMutation("SourceRectangle", c9, c9 with { SourceRectangle = rect2 });
        CommandMutation("DestinationRectangle", c9, c9 with { DestinationRectangle = rect2 });
        CommandMutation("HasResolvedGeometry", c9, c9 with { HasResolvedGeometry = true });
        var c10 = new PushClipCommand(rect,radii);
        types.Add(nameof(PushClipCommand)); Vector(nameof(PushClipCommand), Shapes.Command(c10, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c10, x => checked((int)x)).Fields[1].Value!).Fields.Length == 2, "command-field-count");
        CommandMutation("Rectangle", c10, c10 with { Rectangle = rect2 });
        CommandMutation("Radii", c10, c10 with { Radii = radii2 });
        var c11 = new PopClipCommand();
        types.Add(nameof(PopClipCommand)); Vector(nameof(PopClipCommand), Shapes.Command(c11, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c11, x => checked((int)x)).Fields[1].Value!).Fields.Length == 0, "command-field-count");
        var c12 = new PushTransformCommand(matrix);
        types.Add(nameof(PushTransformCommand)); Vector(nameof(PushTransformCommand), Shapes.Command(c12, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c12, x => checked((int)x)).Fields[1].Value!).Fields.Length == 1, "command-field-count");
        CommandMutation("Matrix", c12, c12 with { Matrix = matrix with {Dx=99f} });
        var c13 = new PopTransformCommand();
        types.Add(nameof(PopTransformCommand)); Vector(nameof(PopTransformCommand), Shapes.Command(c13, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c13, x => checked((int)x)).Fields[1].Value!).Fields.Length == 0, "command-field-count");
        var c14 = new PushScrollOffsetCommand(0);
        types.Add(nameof(PushScrollOffsetCommand)); Vector(nameof(PushScrollOffsetCommand), Shapes.Command(c14, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c14, x => checked((int)x)).Fields[1].Value!).Fields.Length == 1, "command-field-count");
        CommandMutation("owner", c14, c14 with { ScrollContainerId = 1 });
        var c15 = new PopScrollOffsetCommand();
        types.Add(nameof(PopScrollOffsetCommand)); Vector(nameof(PopScrollOffsetCommand), Shapes.Command(c15, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c15, x => checked((int)x)).Fields[1].Value!).Fields.Length == 0, "command-field-count");
        var c16 = new DrawScrollbarChromeCommand(0);
        types.Add(nameof(DrawScrollbarChromeCommand)); Vector(nameof(DrawScrollbarChromeCommand), Shapes.Command(c16, x => checked((int)x)));
        Program.Require(((RecordValue)Shapes.Command(c16, x => checked((int)x)).Fields[1].Value!).Fields.Length == 1, "command-field-count");
        CommandMutation("owner", c16, c16 with { ScrollContainerId = 1 });
        Reject("resource-nonnull", () => Shapes.Command(c9 with { Resource = new ImageResourceResult(new ImageDimensions(1,1), ReadOnlyMemory<byte>.Empty) }, _ => 0));
        Program.Require(types.Count == 17, "command-type-count");
        // Metadata catalog only, never automatic serialization of runtime objects.
        var publicCommands = typeof(PaintCommand).Assembly.GetTypes().Where(t => t.IsPublic && !t.IsAbstract && typeof(PaintCommand).IsAssignableFrom(t)).Select(t => t.Name).Order(StringComparer.Ordinal).ToArray();
        Program.Require(publicCommands.SequenceEqual(types.Order(StringComparer.Ordinal)), "public-command-catalog");
        foreach (Type type in typeof(PaintCommand).Assembly.GetTypes().Where(t => t.IsPublic && !t.IsAbstract && typeof(PaintCommand).IsAssignableFrom(t)))
        {
            var parameters = type.GetConstructors().OrderByDescending(c => c.GetParameters().Length).First().GetParameters();
            var decoded = (Dictionary<string, object?>)Decoder.Read(retainedVectors[type.Name])!;
            var fields = (Dictionary<string, object?>)decoded["values"]!;
            Program.Require(fields.Keys.Order(StringComparer.Ordinal).SequenceEqual(parameters.Select(p => p.Name == "ScrollContainerId" ? "owner" : p.Name!).Order(StringComparer.Ordinal)), "command-primary-fields");
        }
        Reject("unsupported-command", () => Shapes.Command(new UnknownCommand(), _ => 0));
    }
    private sealed record UnknownCommand : PaintCommand;
    private static void CompositeTests()
    {
        var s0 = new RectF(1,2,3,4);
        CompositeMutation("RectF.X", s0, s0 with { X = 2f });
        CompositeMutation("RectF.Y", s0, s0 with { Y = 3f });
        CompositeMutation("RectF.Width", s0, s0 with { Width = 4f });
        CompositeMutation("RectF.Height", s0, s0 with { Height = 5f });
        var s1 = new SizeF(1,2);
        CompositeMutation("SizeF.Width", s1, s1 with { Width = 2f });
        CompositeMutation("SizeF.Height", s1, s1 with { Height = 3f });
        var s2 = new ColorRgba(.1f,.2f,.3f,.4f);
        CompositeMutation("ColorRgba.R", s2, s2 with { R = .2f });
        CompositeMutation("ColorRgba.G", s2, s2 with { G = .3f });
        CompositeMutation("ColorRgba.B", s2, s2 with { B = .4f });
        CompositeMutation("ColorRgba.A", s2, s2 with { A = .5f });
        var s3 = new CornerRadii(1,2,3,4,5,6,7,8);
        CompositeMutation("CornerRadii.TopLeftX", s3, s3 with { TopLeftX = 99f });
        CompositeMutation("CornerRadii.TopLeftY", s3, s3 with { TopLeftY = 99f });
        CompositeMutation("CornerRadii.TopRightX", s3, s3 with { TopRightX = 99f });
        CompositeMutation("CornerRadii.TopRightY", s3, s3 with { TopRightY = 99f });
        CompositeMutation("CornerRadii.BottomRightX", s3, s3 with { BottomRightX = 99f });
        CompositeMutation("CornerRadii.BottomRightY", s3, s3 with { BottomRightY = 99f });
        CompositeMutation("CornerRadii.BottomLeftX", s3, s3 with { BottomLeftX = 99f });
        CompositeMutation("CornerRadii.BottomLeftY", s3, s3 with { BottomLeftY = 99f });
        var s4 = new TransformMatrix(1,2,3,4,5,6);
        CompositeMutation("TransformMatrix.M11", s4, s4 with { M11 = 99f });
        CompositeMutation("TransformMatrix.M12", s4, s4 with { M12 = 99f });
        CompositeMutation("TransformMatrix.M21", s4, s4 with { M21 = 99f });
        CompositeMutation("TransformMatrix.M22", s4, s4 with { M22 = 99f });
        CompositeMutation("TransformMatrix.Dx", s4, s4 with { Dx = 99f });
        CompositeMutation("TransformMatrix.Dy", s4, s4 with { Dy = 99f });
        var s5 = new ScrollMetrics(1,2,3,4);
        CompositeMutation("ScrollMetrics.ScrollableOverflowX", s5, s5 with { ScrollableOverflowX = 99f });
        CompositeMutation("ScrollMetrics.ScrollableOverflowY", s5, s5 with { ScrollableOverflowY = 99f });
        CompositeMutation("ScrollMetrics.ScrollOffsetX", s5, s5 with { ScrollOffsetX = 99f });
        CompositeMutation("ScrollMetrics.ScrollOffsetY", s5, s5 with { ScrollOffsetY = 99f });
        var s6 = new ScrollbarGeometry(false,false,new RectF(1,2,3,4),new RectF(1,2,3,4),new RectF(1,2,3,4),new RectF(1,2,3,4),new RectF(1,2,3,4));
        CompositeMutation("ScrollbarGeometry.HorizontalVisible", s6, s6 with { HorizontalVisible = true });
        CompositeMutation("ScrollbarGeometry.VerticalVisible", s6, s6 with { VerticalVisible = true });
        CompositeMutation("ScrollbarGeometry.HorizontalTrack", s6, s6 with { HorizontalTrack = new RectF(2,3,4,5) });
        CompositeMutation("ScrollbarGeometry.HorizontalThumb", s6, s6 with { HorizontalThumb = new RectF(2,3,4,5) });
        CompositeMutation("ScrollbarGeometry.VerticalTrack", s6, s6 with { VerticalTrack = new RectF(2,3,4,5) });
        CompositeMutation("ScrollbarGeometry.VerticalThumb", s6, s6 with { VerticalThumb = new RectF(2,3,4,5) });
        CompositeMutation("ScrollbarGeometry.Corner", s6, s6 with { Corner = new RectF(2,3,4,5) });
        var s7 = new BoxShadow(1,2,3,4,ColorRgba.Black,false);
        CompositeMutation("BoxShadow.OffsetX", s7, s7 with { OffsetX = 99f });
        CompositeMutation("BoxShadow.OffsetY", s7, s7 with { OffsetY = 99f });
        CompositeMutation("BoxShadow.BlurRadius", s7, s7 with { BlurRadius = 99f });
        CompositeMutation("BoxShadow.SpreadRadius", s7, s7 with { SpreadRadius = 99f });
        CompositeMutation("BoxShadow.Color", s7, s7 with { Color = ColorRgba.White });
        CompositeMutation("BoxShadow.Inset", s7, s7 with { Inset = true });
        var s8 = new LinearGradient(1,2,new[]{new GradientStop(ColorRgba.Black,.1f),new GradientStop(ColorRgba.White,.9f)});
        CompositeMutation("LinearGradient.DirectionX", s8, s8 with { DirectionX = 99f });
        CompositeMutation("LinearGradient.DirectionY", s8, s8 with { DirectionY = 99f });
        CompositeMutation("LinearGradient.Stops", s8, s8 with { Stops = new[]{new GradientStop(ColorRgba.White,.9f),new GradientStop(ColorRgba.Black,.1f)} });
        var s9 = new GradientStop(ColorRgba.Black,.5f);
        CompositeMutation("GradientStop.Color", s9, s9 with { Color = ColorRgba.White });
        CompositeMutation("GradientStop.Offset", s9, s9 with { Offset = .6f });
        var s10 = new CssLength(1,CssUnit.Linear,2,3,4,5,6);
        CompositeMutation("CssLength.Value", s10, s10 with { Value = 99f });
        CompositeMutation("CssLength.Unit", s10, s10 with { Unit = CssUnit.Auto });
        CompositeMutation("CssLength.LinearPercent", s10, s10 with { LinearPercent = 99f });
        CompositeMutation("CssLength.LinearEm", s10, s10 with { LinearEm = 99f });
        CompositeMutation("CssLength.LinearRem", s10, s10 with { LinearRem = 99f });
        CompositeMutation("CssLength.LinearViewportWidth", s10, s10 with { LinearViewportWidth = 99f });
        CompositeMutation("CssLength.LinearViewportHeight", s10, s10 with { LinearViewportHeight = 99f });
        var s11 = new BackdropFilter(BackdropFilterKind.Blur,CssLength.Auto,1f);
        CompositeMutation("BackdropFilter.Kind", s11, s11 with { Kind = BackdropFilterKind.Brightness });
        CompositeMutation("BackdropFilter.LengthAmount", s11, s11 with { LengthAmount = CssLength.Zero });
        CompositeMutation("BackdropFilter.ScalarAmount", s11, s11 with { ScalarAmount = 2f });
    }
}

// Test-only construction of internal managed projection objects. It never constructs a host,
// invokes native methods, or supplies fields to the encoder through reflection.
internal static class SyntheticGraphs
{
    private sealed record Graph(DocumentNode Document, LayoutResult Layout, Dictionary<ElementNode, LayoutBox> Primary,
        List<LayoutBox> Paint, ElementNode Final, LayoutBox FinalBox, LayoutBox Root);
    private static LayoutBox Box(ElementNode element)
    {
        var ctor = typeof(LayoutBox).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null,
            [typeof(LayoutBoxKind), typeof(ElementNode), typeof(ComputedStyle)], null);
        Program.Require(ctor is not null, "synthetic-box-signature");
        var b = (LayoutBox)ctor!.Invoke([LayoutBoxKind.Element, element, new ComputedStyle()]);
        Set(b, "BorderBox", new RectF(-1, 2, 3, 4)); Set(b, "ContentBox", new RectF(0, 3, 1, 2));
        return b;
    }
    private static void Set(LayoutBox box, string name, RectF value)
    {
        var p = typeof(LayoutBox).GetProperty(name, BindingFlags.Public | BindingFlags.Instance);
        Program.Require(p?.PropertyType == typeof(RectF) && p.SetMethod is not null && p.SetMethod.IsAssembly, "synthetic-property-signature");
        p!.SetValue(box, value);
    }
    private static void Add(LayoutBox parent, LayoutBox child)
    {
        var m = typeof(LayoutBox).GetMethod("AddChild", BindingFlags.Instance | BindingFlags.NonPublic, null, [typeof(LayoutBox)], null);
        Program.Require(m is not null && m.ReturnType == typeof(void), "synthetic-add-signature"); m!.Invoke(parent, [child]);
    }
    private static LayoutResult Layout(LayoutBox root, Dictionary<ElementNode, LayoutBox> primary, List<LayoutBox> paint)
    {
        var c = typeof(LayoutResult).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null,
            [typeof(LayoutBox), typeof(IReadOnlyDictionary<ElementNode, LayoutBox>), typeof(IReadOnlyList<LayoutBox>), typeof(SizeF), typeof(OverflowKind), typeof(OverflowKind)], null);
        Program.Require(c is not null, "synthetic-layout-signature");
        return (LayoutResult)c!.Invoke([root, primary, paint, new SizeF(1440, 900), OverflowKind.Visible, OverflowKind.Visible]);
    }
    private static Graph Create()
    {
        var document = new DocumentNode(); var body = new ElementNode("body"); document.AppendChild(body);
        var root = Box(body); var primary = new Dictionary<ElementNode, LayoutBox> { [body] = root }; var paint = new List<LayoutBox> { root };
        ElementNode? final = null; LayoutBox? finalBox = null;
        for (int i = 1; i <= 403; i++)
        {
            var article = new ElementNode("article"); article.SetAttribute("id", "FLEX-" + i.ToString("D3", CultureInfo.InvariantCulture)); body.AppendChild(article);
            var b = Box(article); Add(root, b); primary.Add(article, b); paint.Add(b);
            if (i is 1 or 200 or 403)
            {
                var stage = new ElementNode("div"); stage.SetAttribute("class", "stage"); article.AppendChild(stage);
                var stageBox = Box(stage); Add(b, stageBox); primary.Add(stage, stageBox); paint.Add(stageBox);
                var target = new ElementNode("div"); target.SetAttribute("class", "flex-target"); stage.AppendChild(target);
                var targetBox = Box(target); Add(stageBox, targetBox); primary.Add(target, targetBox); paint.Add(targetBox);
                if (i == 403) { final = target; finalBox = targetBox; }
            }
        }
        for (int i = 0; i < 3; i++) { var img = new ElementNode("img"); img.SetAttribute("src", "data:image/svg+xml,x"); body.AppendChild(img); }
        return new(document, Layout(root, primary, paint), primary, paint, final!, finalBox!, root);
    }
    private static Program.MemoryRenderer Backend(bool text = true)
    {
        var b = new Program.MemoryRenderer();
        var commands = new List<PaintCommand> { new FillRoundedRectangleCommand(new RectF(0, 0, 1, 1), CornerRadii.Zero, ColorRgba.Black) };
        if (text) commands.Add(new DrawTextCommand("sentinel", new RectF(0, 0, 1, 1), "sans", 12, FontWeight.Normal, TextAlign.Start, ColorRgba.Black));
        b.Render(new DisplayList(commands), ColorRgba.White); return b;
    }
    internal static void Run(List<string> checks)
    {
        var m = new UiDocumentHostMetrics(1, 403, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, NativeStyleImpact.None) { LayoutProjections = 1 };
        var t = new Program.CountingMeasurer(); t.MeasureText("x", new ComputedStyle());
        var b = Backend(); Program.AssertAfter(m, b, t, false);
        var counts = Program.Counts(m, 1, t.Calls, 0, false, false);
        var positive = Create(); string baseline = Snapshot.Create(positive.Document, positive.Layout, b, counts, 1).Hash; checks.Add("D3-managed-positive");
        void Changed(string name, Graph graph, Program.MemoryRenderer backend)
        {
            Program.Require(Snapshot.Create(graph.Document, graph.Layout, backend, counts, 1).Hash != baseline, "domain-mutation-missed"); checks.Add(name);
        }
        var childrenOrder = Create(); var first = childrenOrder.Document.Children[0].Children[0]; childrenOrder.Document.Children[0].RemoveChild(first); childrenOrder.Document.Children[0].AppendChild(first);
        Changed("domain-children-order", childrenOrder, b);
        var boxOrder = Create(); Program.Require(boxOrder.Root.Children is List<LayoutBox>, "synthetic-children-storage"); ((List<LayoutBox>)boxOrder.Root.Children).Reverse(); Changed("domain-box-order", boxOrder, b);
        var paintOrder = Create(); paintOrder.Paint.Reverse(); Changed("domain-paint-order", paintOrder, b);
        var commandOrder = Backend(); commandOrder.Render(new DisplayList(b.Commands.Reverse().ToArray()), ColorRgba.White); Changed("domain-command-order", Create(), commandOrder);
        
        Program.MemoryRenderer OwnerBackend(long a, long second)
        {
            var ownerBackend = new Program.MemoryRenderer();
            ownerBackend.Render(new DisplayList([b.Commands[0], b.Commands[1], new PushScrollOffsetCommand(a), new PopScrollOffsetCommand()],
                [new PaintChunk(a,0,2,new RectF(0,0,1,1),0), new PaintChunk(second,2,4,new RectF(0,0,1,1),1)]),ColorRgba.White); return ownerBackend;
        }
        string OwnerDigest(long a, long second) { var g = Create(); return Snapshot.Create(g.Document,g.Layout,OwnerBackend(a,second),counts,1).Hash; }
        Program.Require(OwnerDigest(41,41) == OwnerDigest(902,902), "domain-owner-renumber"); checks.Add("domain-owner-renumber");
        Program.Require(OwnerDigest(41,41) != OwnerDigest(41,42), "domain-owner-aliasing"); checks.Add("domain-owner-aliasing");
        var attrs1 = Create(); attrs1.Final.SetAttribute("a", "1"); attrs1.Final.SetAttribute("z", "2");
        var attrs2 = Create(); attrs2.Final.SetAttribute("z", "2"); attrs2.Final.SetAttribute("a", "1");
        Program.Require(Snapshot.Create(attrs1.Document, attrs1.Layout, b, counts, 1).Hash == Snapshot.Create(attrs2.Document, attrs2.Layout, b, counts, 1).Hash, "domain-attribute-order"); checks.Add("domain-attribute-order");
        var gradientA = new LinearGradient(1,0,new[]{new GradientStop(ColorRgba.Black,0),new GradientStop(ColorRgba.White,1)});
        var gradientB = gradientA with { Stops = gradientA.Stops.Reverse().ToArray() };
        var backendA = new Program.MemoryRenderer(); var backendB = new Program.MemoryRenderer();
        backendA.Render(new DisplayList([new FillRoundedRectangleGradientCommand(new RectF(0,0,1,1),CornerRadii.Zero,new RectF(0,0,1,1),gradientA,1), b.Commands[1]]),ColorRgba.White);
        backendB.Render(new DisplayList([new FillRoundedRectangleGradientCommand(new RectF(0,0,1,1),CornerRadii.Zero,new RectF(0,0,1,1),gradientB,1), b.Commands[1]]),ColorRgba.White);
        var ga = Create(); var gb = Create(); Program.Require(Snapshot.Create(ga.Document,ga.Layout,backendA,counts,1).Hash != Snapshot.Create(gb.Document,gb.Layout,backendB,counts,1).Hash, "domain-stops-order"); checks.Add("domain-stops-order");
        void Bad(string name, Action action)
        {
            bool rejected = false; try { action(); } catch (ContractFailure) { rejected = true; }
            Program.Require(rejected, "synthetic-accepted-" + name); checks.Add(name);
        }
        Bad("D3-backend-zero", () => Program.AssertAfter(m, new Program.MemoryRenderer(), t, false));
        Bad("D3-projection-zero", () => Program.AssertAfter(m with { LayoutProjections = 0 }, b, t, false));
        Bad("D3-second-render", () => Program.AssertAfter(m with { RenderCalls = 2 }, b, t, false));
        Bad("D3-multipass", () => Program.AssertAfter(m with { LayoutBuilds = 2 }, b, t, false));
        Bad("D3-pending", () => Program.AssertAfter(m, b, t, true));
        Bad("D3-fill-without-text", () => Program.AssertAfter(m, Backend(false), t, false));
        var failedText = new Program.CountingMeasurer(); try { failedText.MeasureText(null!, new ComputedStyle()); } catch (ArgumentNullException) { }
        Bad("D3-swallowed-callback", () => Program.AssertAfter(m, b, failedText, false));
        Bad("D3-empty-layout", () => { var g = Create(); var empty = Layout(g.Root, new(), []); Snapshot.Create(g.Document, empty, b, counts, 1); });
        Bad("D3-final-box-missing", () => { var g = Create(); g.Primary.Remove(g.Final); Snapshot.Create(g.Document, g.Layout, b, counts, 1); });
        Bad("D3-wrong-box-element", () => { var g = Create(); g.Primary[g.Final] = g.Root; Snapshot.Create(g.Document, g.Layout, b, counts, 1); });
        Bad("D3-sentinel-nan", () => { var g = Create(); Set(g.FinalBox, "BorderBox", new RectF(float.NaN, 1, 2, 3)); Snapshot.Create(g.Document, g.Layout, b, counts, 1); });
        Bad("D3-sentinel-zero-area", () => { var g = Create(); Set(g.FinalBox, "BorderBox", new RectF(1, 1, 0, 3)); Snapshot.Create(g.Document, g.Layout, b, counts, 1); });
        Bad("D3-truncated-dom", () => { var g = Create(); g.Final.Parent!.RemoveChild(g.Final); Snapshot.Create(g.Document, g.Layout, b, counts, 1); });
        Bad("D3-cycle-box", () => { var g = Create(); Add(g.FinalBox, g.Root); Snapshot.Create(g.Document, g.Layout, b, counts, 1); });
        Bad("D3-orphan-paint", () => { var g = Create(); g.Paint.Add(Box(new ElementNode("div"))); Snapshot.Create(g.Document, g.Layout, b, counts, 1); });
    }
}
