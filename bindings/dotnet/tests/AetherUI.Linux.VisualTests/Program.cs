using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using AetherUI.Linux;
using AetherUI.Primitives;
using AetherUI.Rendering;
using VisualValidation;

internal static class Program
{
    private sealed record InvalidCommand : PaintCommand;
    private sealed record Result(string Fixture, int Dpi, Verdict Linux, Verdict Chromium,
        string Reason, string[] Hashes, int Width, int Height);
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true, Converters = { new JsonStringEnumConverter() } };
    private static string Hash(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
    internal static Raster Capture(IRenderBackend renderer, MemoryFramePresenter presenter, DisplayList displayList, int width, int height)
    {
        // This guard also protects future reuse: Render must return AND publish a new frame.
        var before = presenter.CurrentFrame;
        renderer.Render(displayList, ColorRgba.Transparent);
        var frame = presenter.CurrentFrame;
        if (width <= 0 || height <= 0 || frame is null || ReferenceEquals(before, frame))
            throw new InvalidOperationException("No fresh presentation; capture rejected.");
        if (frame.PixelWidth != width || frame.PixelHeight != height || frame.Stride != checked(width * 4) || frame.Pixels.Length != checked(width * height * 4))
            throw new InvalidOperationException("Unexpected capture dimensions/stride/length.");
        return new Raster(width, height, frame.Pixels.ToArray());
    }
    private static Raster Render(Fixture fixture, int dpi)
    {
        var presenter = new MemoryFramePresenter();
        if (presenter.CurrentFrame is not null) throw new InvalidOperationException("Presenter not fresh.");
        using var renderer = new LinuxCairoRenderModule().Create(new(presenter), new(256 * dpi, 192 * dpi, dpi));
        renderer.SetScrollVisualStateProvider(_ => new(new(12, 8), new(true, true,
            new(16, 120, 136, 8), new(32, 120, 40, 8), new(152, 16, 8, 112), new(152, 40, 8, 32), new(152, 120, 8, 8))));
        return Capture(renderer, presenter, new(fixture.Commands), 256 * dpi, 192 * dpi);
    }
    private static void CaptureTests()
    {
        var presenter = new MemoryFramePresenter();
        using var renderer = new LinuxCairoRenderModule().Create(new(presenter), new(8, 8, 1));
        var valid = new DisplayList(new PaintCommand[] { new FillRoundedRectangleCommand(new(0, 0, 8, 8), default, ColorRgba.White) });
        _ = Capture(renderer, presenter, valid, 8, 8);
        var previous = presenter.CurrentFrame;
        bool rejected = false;
        try { _ = Capture(renderer, presenter, new(new PaintCommand[] { new InvalidCommand() }), 8, 8); }
        catch (NotSupportedException) { rejected = true; }
        if (!rejected || !ReferenceEquals(previous, presenter.CurrentFrame)) throw new InvalidOperationException("Failed-render capture contract.");
        renderer.Resize(0, 8, 1);
        rejected = false;
        try { _ = Capture(renderer, presenter, valid, 8, 8); }
        catch (InvalidOperationException) { rejected = true; }
        if (!rejected || !ReferenceEquals(previous, presenter.CurrentFrame)) throw new InvalidOperationException("Suspended stale capture contract.");
        var fresh = new MemoryFramePresenter();
        using var suspended = new LinuxCairoRenderModule().Create(new(fresh), new(0, 8, 1));
        rejected = false;
        try { _ = Capture(suspended, fresh, valid, 0, 8); }
        catch (InvalidOperationException) { rejected = true; }
        if (!rejected || fresh.CurrentFrame is not null) throw new InvalidOperationException("Fresh suspended capture contract.");
    }
    private static int Main(string[] args)
    {
        if (args.Length != 2) { Console.Error.WriteLine("Usage: <new-output-directory> <explicit-font-family>"); return 2; }
        if (Directory.Exists(args[0])) { Console.Error.WriteLine("Refusing an existing capture directory."); return 2; }
        Directory.CreateDirectory(args[0]);
        var results = new List<Result>();
        Verdict contracts = Verdict.NOT_RUN;
        string contractReason = "Not run";
        try
        {
            ComparatorTests.Run();
            CaptureTests();
            contracts = Verdict.PASS; contractReason = "Comparator synthetic and failed/suspended capture tests passed";
        }
        catch (Exception e) { contracts = e is PlatformNotSupportedException or DllNotFoundException ? Verdict.BLOCKED : Verdict.FAIL; contractReason = e.Message; }
        var fixtures = Fixtures.All(args[1]);
        var inventory = typeof(PaintCommand).Assembly.GetTypes().Where(t => !t.IsAbstract && t.IsSubclassOf(typeof(PaintCommand))).Select(t => t.Name).Order().ToArray();
        var covered = fixtures.SelectMany(f => f.Commands).Select(c => c.GetType().Name).Distinct().Order().ToArray();
        bool inventoryPass = inventory.SequenceEqual(covered) && fixtures.Length > 0 && fixtures.Select(f => f.Id).Distinct().Count() == fixtures.Length;
        foreach (var fixture in fixtures)
        foreach (int dpi in new[] { 1, 2 })
        {
            var hashes = new List<string>();
            var status = Verdict.PASS; string reason = "Analytic assertions and three same-environment captures passed; no independent image oracle.";
            try
            {
                Raster? first = null;
                for (int repeat = 0; repeat < 3; repeat++)
                {
                    Raster raster = Render(fixture, dpi);
                    string name = $"{fixture.Id}-dpi{dpi}-run{repeat}";
                    File.WriteAllBytes(Path.Combine(args[0], name + ".bgra"), raster.Pixels);
                    RasterTools.WritePng(Path.Combine(args[0], name + ".png"), raster);
                    hashes.Add(Hash(raster.Pixels));
                    fixture.Check(raster, dpi);
                    if (first is not null)
                    {
                        var comparison = RasterTools.Compare(first, raster, "run0", $"run{repeat}");
                        if (comparison.Status != Verdict.PASS)
                        {
                            RasterTools.WritePng(Path.Combine(args[0], name + "-diff.png"), RasterTools.Diff(first, raster));
                            RasterTools.WritePng(Path.Combine(args[0], name + "-overlay.png"), RasterTools.Overlay(first, raster));
                            throw new InvalidOperationException("Nondeterministic repetition: " + comparison.Reason);
                        }
                    }
                    first ??= raster;
                }
            }
            catch (Exception e) { status = e is PlatformNotSupportedException or DllNotFoundException ? Verdict.BLOCKED : Verdict.FAIL; reason = e.Message; }
            results.Add(new(fixture.Id, dpi, status, Verdict.BLOCKED, reason, hashes.ToArray(), 256 * dpi, 192 * dpi));
            Console.WriteLine($"{status} Linux {fixture.Id} DPI={dpi}; Chromium BLOCKED (no permitted reference)");
        }
        var linux = RasterTools.Aggregate(results.Select(r => r.Linux).Concat(new[] { contracts, inventoryPass ? Verdict.PASS : Verdict.FAIL }).ToArray());
        var chromium = RasterTools.Aggregate(results.Select(r => r.Chromium).ToArray());
        File.WriteAllText(Path.Combine(args[0], "summary.json"), JsonSerializer.Serialize(new {
            schema = 1, linux, chromium, completeConformance = RasterTools.Aggregate(linux, chromium), contracts, contractReason, inventoryPass, inventory, covered,
            requiredFixtures = fixtures.Select(f => f.Id), results,
            chromiumReason = "Reference generation remains blocked. No browser started or reference imported.",
            comparisonScope = "Exact premultiplied BGRA repetition, analytic per-fixture assertions. Not Chromium, DOM/layout, glyph correctness, Windows, GPU or event validation."
        }, Json));
        Console.WriteLine($"Linux {linux}; Chromium {chromium}; complete conformance {RasterTools.Aggregate(linux, chromium)}");
        return linux == Verdict.PASS ? 0 : linux == Verdict.BLOCKED ? 2 : 1;
    }
}
