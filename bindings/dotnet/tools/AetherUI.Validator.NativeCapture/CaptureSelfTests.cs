using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;
using AetherUI.Validator;
using AetherUI.Windows.Graphics;
using Vortice.Direct3D;

namespace AetherUI.Validator.NativeCapture;

// Focused diagnostics only. Each failure stops this process; the external wrapper supplies
// the hard 60-second deadline. No browser, general regression suite or device-loss forcing.
internal static class CaptureSelfTests
{
    private static readonly List<object> Results = [];
    private static readonly ColorRgba Clear = new(1, 1, 1, 1);
    private const string Scene = "<html><head><style>html,body{margin:0;width:96px;height:64px;background:white}"
        + "div{position:absolute;width:16px;height:16px}"
        + "#red{left:8px;top:8px;background:#ff0000}#blue{left:40px;top:8px;background:#0000ff}"
        + "#blend{left:8px;top:40px;background:rgba(255,0,0,0.5)}</style></head>"
        + "<body><div id='red'></div><div id='blue'></div><div id='blend'></div></body></html>";

    internal static int Run(string outputDirectory)
    {
        string current = "environment";
        try
        {
            Check("STA", Thread.CurrentThread.GetApartmentState() == ApartmentState.STA);
            RuntimeGuard.Check();
            foreach (var test in new (string Name, Action Run)[] {
                ("T1", () => Encoder(outputDirectory)), ("T2", () => Raster(outputDirectory)),
                ("T2b", StableAndIncremental), ("T2c", Wrappers), ("T3", Transactions),
                ("T4", () => Timings(outputDirectory)), ("T5", Barrier), ("T6", () => Identity(outputDirectory)) })
            {
                current = test.Name;
                test.Run();
                Results.Add(new { test = current, status = "passed" });
            }
            RuntimeGuard.Check();
            WriteResults(outputDirectory, "passed", null);
            return 0;
        }
        catch (Exception error)
        {
            Results.Add(new { test = current, status = "failed", error = error.GetType().Name });
            WriteResults(outputDirectory, "failed", current);
            return 4;
        }
    }

    private static void WriteResults(string output, string status, string? failedTest)
    {
        using var file = new FileStream(Path.Combine(output, "tests.json"), FileMode.CreateNew);
        JsonSerializer.Serialize(file, new { schema = "native-capture-self-tests-v1", status, failedTest,
            driver = "warp", timestampFrequency = Stopwatch.Frequency, checks = Results,
            scope = "T1-T6 focal; no conformance or visual parity verdict" }, new JsonSerializerOptions { WriteIndented = true });
    }

    private static void Check(string name, bool condition)
    {
        Results.Add(new { check = name, status = condition ? "passed" : "failed" });
        if (!condition) throw new InvalidOperationException(name);
    }

    private static void Reject(string name, Action action)
    {
        bool rejected = false;
        try { action(); }
        catch (Exception e) when (e is ArgumentException or InvalidOperationException or IOException
            or OverflowException or UnauthorizedAccessException or TimeoutException) { rejected = true; }
        Check(name, rejected);
    }

    private static void Encoder(string output)
    {
        byte[] input = [0,0,255,255, 255,0,0,255, 0,255,0,255,
            32,64,128,128, 90,80,70,0, 3,2,1,255];
        byte[] expected = [255,0,0,255, 0,0,255,255, 0,255,0,255,
            255,128,64,128, 0,0,0,0, 1,2,3,255];
        Check("T1.rounding-and-channels", CapturePngEncoder.ToRgba(input, 3, 2).SequenceEqual(expected));
        string path = Path.Combine(output, "encoding-pattern.png");
        CapturePngEncoder.Write(path, input, 3, 2);
        CapturePngEncoder.Validate(path, input, 3, 2);
        using (var decoded = new System.Drawing.Bitmap(path))
        {
            Check("T1.independent-size", decoded.Width == 3 && decoded.Height == 2);
            for (int y = 0; y < 2; y++) for (int x = 0; x < 3; x++)
            {
                var c = decoded.GetPixel(x, y); int i = (y * 3 + x) * 4;
                Check($"T1.independent-pixel-{x}-{y}", c.R == expected[i] && c.G == expected[i+1]
                    && c.B == expected[i+2] && c.A == expected[i+3]);
            }
        }
        Reject("T1.existing-output", () => CapturePngEncoder.Write(path, input, 3, 2));
        Reject("T1.truncated", () => CapturePngEncoder.ToRgba(input[..^1], 3, 2));
        Reject("T1.excess", () => CapturePngEncoder.ToRgba([.. input, 0], 3, 2));
        Reject("T1.negative", () => CapturePngEncoder.ToRgba([], -1, 2));
        Reject("T1.zero", () => CapturePngEncoder.ToRgba([], 0, 2));
        Reject("T1.overflow", () => CapturePngEncoder.ToRgba([], int.MaxValue, int.MaxValue));
        byte[] changed = (byte[])input.Clone(); changed[0] = 255; changed[2] = 0;
        string changedPath = Path.Combine(output, "encoding-changed.png");
        CapturePngEncoder.Write(changedPath, changed, 3, 2);
        using var changedImage = new System.Drawing.Bitmap(changedPath);
        Check("T1.input-pixel-controls-png", changedImage.GetPixel(0, 0).B == 255 && changedImage.GetPixel(0, 0).R == 0);
    }

    private sealed class HostScene : IDisposable
    {
        internal readonly HiddenCaptureWindow Window;
        internal readonly Direct2DRenderBackend Backend;
        internal readonly RawHtmlFileComponent Component;
        internal readonly UiDocumentHost Host;
        internal HostScene(string html = Scene, IImageResourceResolver? images = null, Action<UiDocumentHostMetrics>? metrics = null)
        {
            Window = new HiddenCaptureWindow(96, 64);
            Backend = new Direct2DRenderBackend(Window.Handle, 96, 64, 1, DriverType.Warp);
            Component = new RawHtmlFileComponent(html);
            Host = new UiDocumentHost(Component, Backend, Backend,
                new UiHostOptions { Width = 96, Height = 64, ClearColor = Clear, ImageResolver = images, MetricsSink = metrics });
            Host.SetViewport(96, 64, 1);
        }
        internal CapturedFrame Capture(long id)
        {
            Backend.BeginDiagnosticCapture(new CaptureRequest(id, 96, 64, 1));
            try { Host.Render(); return Backend.TakeDiagnosticCapture(id); }
            finally { Backend.CancelDiagnosticCapture(id); }
        }
        public void Dispose()
        {
            // Each owner gets its cleanup even when an earlier Dispose fails.
            try { Host.Dispose(); } finally { try { Component.Dispose(); } finally { try { Backend.Dispose(); } finally { Window.Dispose(); } } }
        }
    }

    private static void Pixel(string name, byte[] rgba, int width, int x, int y, int r, int g, int b, int tolerance = 0)
    {
        int i = checked((y * width + x) * 4);
        Check(name, Math.Abs(rgba[i] - r) <= tolerance && Math.Abs(rgba[i+1] - g) <= tolerance
            && Math.Abs(rgba[i+2] - b) <= tolerance && rgba[i+3] == 255);
    }

    private static void Raster(string output)
    {
        using var context = new QueuedStaContext();
        using var scene = new HostScene();
        var frame = scene.Capture(1);
        Check("T2.real-frame", frame.BackendFrameSequence == 1 && frame.RowStride == 384
            && frame.PixelWidth == 96 && frame.PixelHeight == 64 && frame.BgraPremultiplied.Length == 96*64*4);
        byte[] rgba = CapturePngEncoder.ToRgba(frame.BgraPremultiplied, 96, 64);
        Pixel("T2.red-center", rgba, 96, 16, 16, 255, 0, 0);
        Pixel("T2.blue-center", rgba, 96, 48, 16, 0, 0, 255);
        Pixel("T2.white-background", rgba, 96, 80, 56, 255, 255, 255);
        Pixel("T2.half-alpha-composition", rgba, 96, 16, 48, 255, 128, 128, 1);
        string path = Path.Combine(output, "captured-pattern.png");
        CapturePngEncoder.Write(path, frame.BgraPremultiplied, 96, 64);
        CapturePngEncoder.Validate(path, frame.BgraPremultiplied, 96, 64);
        using var moved = new HostScene(Scene.Replace("left:8px;top:8px;background:#ff0000", "left:64px;top:8px;background:#00ff00", StringComparison.Ordinal));
        var second = moved.Capture(1);
        byte[] other = CapturePngEncoder.ToRgba(second.BgraPremultiplied, 96, 64);
        Pixel("T2.moved-original-empty", other, 96, 16, 16, 255,255,255);
        Pixel("T2.moved-green-landmark", other, 96, 72,16,0,255,0);
        string secondPath = Path.Combine(output, "captured-changed.png");
        CapturePngEncoder.Write(secondPath, second.BgraPremultiplied, 96,64);
        CapturePngEncoder.Validate(secondPath, second.BgraPremultiplied,96,64);
        Check("T2.control-png-hash-different", !SHA256.HashData(File.ReadAllBytes(path)).SequenceEqual(SHA256.HashData(File.ReadAllBytes(secondPath))));
    }

    private static DisplayList LegacyScene(bool changed) => new([
        new FillRoundedRectangleCommand(new RectF(0, 0, 96, 64), CornerRadii.Zero, Clear),
        new FillRoundedRectangleCommand(new RectF(8, 8, 16, 16), CornerRadii.Zero,
            changed ? new ColorRgba(0, 0, 1, 1) : new ColorRgba(1, 0, 0, 1))]);

    private static void StableAndIncremental()
    {
        using var context = new QueuedStaContext();
        using var scene = new HostScene();
        scene.Host.Render();
        Check("T2b.initial-stable", !scene.Host.NeedsRender && context.PendingCount == 0);
        long initialSequence = scene.Backend.BackendFrameSequence;
        long initialIncremental = scene.Host.Metrics.IncrementalRenderCalls;
        var first = scene.Capture(1);
        Check("T2b.full-diagnostic-fallback", first.BackendFrameSequence == initialSequence+1
            && scene.Host.Metrics.IncrementalRenderCalls == initialIncremental
            && scene.Backend.DiagnosticCaptureCommitCount == 1 && scene.Backend.DiagnosticCaptureCopyCount == 1);
        var second = scene.Capture(2);
        Check("T2b.same-stable-pixels", first.BgraPremultiplied.SequenceEqual(second.BgraPremultiplied));
        foreach(var stableFrame in new[]{first,second})
        {
            byte[] rgba=CapturePngEncoder.ToRgba(stableFrame.BgraPremultiplied,96,64);
            Pixel($"T2b.stable-{stableFrame.RequestId}-red",rgba,96,16,16,255,0,0);
            Pixel($"T2b.stable-{stableFrame.RequestId}-blue",rgba,96,48,16,0,0,255);
            Pixel($"T2b.stable-{stableFrame.RequestId}-background",rgba,96,80,56,255,255,255);
        }
        long copies = scene.Backend.DiagnosticCaptureCopyCount, frames = scene.Backend.BackendFrameSequence;
        scene.Host.Render();
        Check("T2b.unarmed-no-damage", scene.Backend.BackendFrameSequence == frames
            && scene.Backend.DiagnosticCaptureCopyCount == copies
            && scene.Host.Metrics.IncrementalRenderCalls == initialIncremental + 1);
        using var window = new HiddenCaptureWindow(96,64);
        using var incremental = new Direct2DRenderBackend(window.Handle,96,64,1,DriverType.Warp);
        using var fullWindow = new HiddenCaptureWindow(96,64);
        using var full = new Direct2DRenderBackend(fullWindow.Handle,96,64,1,DriverType.Warp);
        incremental.Render(LegacyScene(false), Clear);
        incremental.RequestBackBufferCaptureForDiagnostics();
        Check("T2b.legacy-real-damage-accepted", incremental.TryRenderIncremental(LegacyScene(true),Clear));
        var inc = incremental.TakeBackBufferCaptureForDiagnostics();
        var reference = full.RenderAndCaptureBackBufferForDiagnostics(LegacyScene(true),Clear);
        Check("T2b.legacy-damage-matches-full", inc.SequenceEqual(reference));
    }

    private static void Wrappers()
    {
        using var window = new HiddenCaptureWindow(96,64);
        using var backend = new Direct2DRenderBackend(window.Handle,96,64,1,DriverType.Warp);
        var display = LegacyScene(false);
        var first = backend.RenderAndCaptureBackBufferForDiagnostics(display,Clear);
        Check("T2c.first-frame-no-preparation", first.Length == 96*64*4 && backend.BackendFrameSequence == 1
            && backend.DiagnosticCaptureCopyCount == 1);
        foreach (var test in new[] { (W:80,H:48,Dpi:1f,ExpectedW:80,ExpectedH:48),
            (W:0,H:0,Dpi:1f,ExpectedW:80,ExpectedH:48), (W:80,H:48,Dpi:1.25f,ExpectedW:80,ExpectedH:48) })
        {
            long before = backend.BackendFrameSequence, copies = backend.DiagnosticCaptureCopyCount;
            backend.Resize(test.W,test.H,test.Dpi);
            backend.RequestBackBufferCaptureForDiagnostics();
            backend.Render(display,Clear);
            var pixels = backend.TakeBackBufferCaptureForDiagnostics();
            Check($"T2c.resize-{test.W}-{test.H}-{test.Dpi}", pixels.Length == test.ExpectedW*test.ExpectedH*4
                && backend.BackendFrameSequence == before+1 && backend.DiagnosticCaptureCopyCount == copies+1
                && backend.DiagnosticTargetDpiScale == test.Dpi);
        }
        backend.BeginDiagnosticCapture(new CaptureRequest(100,80,48,1.25f));
        backend.Render(display,Clear);
        Check("T2c.dpi-effective", backend.TakeDiagnosticCapture(100).DpiScale == 1.25f);
        long priorFrames=backend.BackendFrameSequence, priorCopies=backend.DiagnosticCaptureCopyCount;
        backend.RequestBackBufferCaptureForDiagnostics(); backend.RequestBackBufferCaptureForDiagnostics();
        backend.Render(display,Clear); _ = backend.TakeBackBufferCaptureForDiagnostics();
        Check("T2c.repeat-legacy-one-render", backend.BackendFrameSequence==priorFrames+1 && backend.DiagnosticCaptureCopyCount==priorCopies+1);
        Reject("T2c.double-legacy-take",()=>backend.TakeBackBufferCaptureForDiagnostics());
        backend.Dispose();
        Reject("T2c.disposed-target",()=>backend.RequestBackBufferCaptureForDiagnostics());
    }

    private static void Transactions()
    {
        using var window = new HiddenCaptureWindow(96,64);
        using var backend = new Direct2DRenderBackend(window.Handle,96,64,1,DriverType.Warp);
        var display = LegacyScene(false);
        Reject("T3.take-without-begin",()=>backend.TakeDiagnosticCapture(1));
        Reject("T3.wrong-owner",()=>Task.Run(()=>backend.BeginDiagnosticCapture(new CaptureRequest(1,96,64,1))).GetAwaiter().GetResult());
        Reject("T3.invalid-dimensions",()=>backend.BeginDiagnosticCapture(new CaptureRequest(1,95,64,1)));
        backend.BeginDiagnosticCapture(new CaptureRequest(1,96,64,1));
        Reject("T3.duplicate-begin",()=>backend.BeginDiagnosticCapture(new CaptureRequest(2,96,64,1)));
        Reject("T3.before-render",()=>backend.TakeDiagnosticCapture(1));
        Reject("T3.legacy-cannot-cancel-explicit",()=>backend.RequestBackBufferCaptureForDiagnostics());
        backend.Render(display,Clear);
        Reject("T3.wrong-id",()=>backend.TakeDiagnosticCapture(2));
        _=backend.TakeDiagnosticCapture(1);
        Reject("T3.double-take",()=>backend.TakeDiagnosticCapture(1));
        Reject("T3.old-id",()=>backend.BeginDiagnosticCapture(new CaptureRequest(1,96,64,1)));
        backend.BeginDiagnosticCapture(new CaptureRequest(3,96,64,1)); backend.CancelDiagnosticCapture(3); backend.CancelDiagnosticCapture(3);
        Reject("T3.cancel-clears",()=>backend.TakeDiagnosticCapture(3));
        backend.BeginDiagnosticCapture(new CaptureRequest(4,96,64,1)); backend.Render(display,Clear); backend.Resize(80,48,1);
        Reject("T3.resize-invalidates",()=>backend.TakeDiagnosticCapture(4)); backend.CancelDiagnosticCapture(4);
        backend.BeginDiagnosticCapture(new CaptureRequest(5,80,48,1)); backend.Render(display,Clear); backend.AbortDiagnosticCaptureForTesting();
        Reject("T3.abort-no-result",()=>backend.TakeDiagnosticCapture(5)); backend.CancelDiagnosticCapture(5);
        backend.BeginDiagnosticCapture(new CaptureRequest(6,80,48,1));
        Reject("T3.new-id-not-old-bytes",()=>backend.TakeDiagnosticCapture(6));
        backend.Render(display,Clear); var fresh=backend.TakeDiagnosticCapture(6);
        Check("T3.new-result-identity",fresh.RequestId==6 && fresh.PixelWidth==80 && fresh.PixelHeight==48);
        backend.BeginDiagnosticCapture(new CaptureRequest(7,80,48,1)); backend.Render(display,Clear); backend.Dispose();
        Reject("T3.dispose-invalidates",()=>backend.TakeDiagnosticCapture(7));
        CommitFailure(false);
        CommitFailure(true);
    }

    private static void CommitFailure(bool incremental)
    {
        string label=incremental?"T3.incremental-commit-failure":"T3.full-commit-failure";
        using var window=new HiddenCaptureWindow(96,64);
        using var backend=new Direct2DRenderBackend(window.Handle,96,64,1,DriverType.Warp);
        backend.Render(LegacyScene(false),Clear);
        long frames=backend.BackendFrameSequence, copies=backend.DiagnosticCaptureCopyCount, commits=backend.DiagnosticCaptureCommitCount;
        backend.BeginDiagnosticCapture(new CaptureRequest(1,96,64,1));
        backend.FailNextDiagnosticCommitForTesting(1);
        Reject(label+".duplicate-arm-rejected",()=>backend.FailNextDiagnosticCommitForTesting(1));
        bool exactFailure=false;
        try
        {
            // Real red-to-blue damage. A false return does not satisfy this test: the
            // exact seam exception proves the incremental path reached its public commit.
            if(incremental) _=backend.TryRenderIncremental(LegacyScene(true),Clear);
            else backend.Render(LegacyScene(true),Clear);
        }
        catch(InvalidOperationException error) when(error.Message=="Injected diagnostic capture commit failure.")
        { exactFailure=true; }
        Check(label+".exact-seam-reached",exactFailure);
        Check(label+".copied-not-committed",backend.DiagnosticCaptureCopyCount==copies+1
            && backend.DiagnosticCaptureCommitCount==commits && backend.BackendFrameSequence==frames);
        Reject(label+".failed-request-has-no-bytes",()=>backend.TakeDiagnosticCapture(1));
        // The setter rejects an existing flag. Rearming before Cancel proves the throw
        // consumed the one-shot flag; the later successful frame tests Cancel cleanup.
        backend.FailNextDiagnosticCommitForTesting(1);
        Check(label+".one-shot-consumed-before-cancel",true);
        backend.CancelDiagnosticCapture(1);
        backend.BeginDiagnosticCapture(new CaptureRequest(2,96,64,1));
        Reject(label+".new-id-cannot-consume-old-candidate",()=>backend.TakeDiagnosticCapture(2));
        // The failed attempt retained the blue display list; red is fresh real damage.
        // This is an explicit independent test action, not a production capture retry.
        if(incremental) Check(label+".next-incremental-accepted",backend.TryRenderIncremental(LegacyScene(false),Clear));
        else backend.Render(LegacyScene(false),Clear);
        var recovered=backend.TakeDiagnosticCapture(2);
        Check(label+".next-commit-succeeds",recovered.RequestId==2 && backend.DiagnosticCaptureCopyCount==copies+2
            && backend.DiagnosticCaptureCommitCount==commits+1 && backend.BackendFrameSequence==frames+1);
        Pixel(label+".fresh-red-not-stale-blue",CapturePngEncoder.ToRgba(recovered.BgraPremultiplied,96,64),96,16,16,255,0,0);
        Reject(label+".consumed-once",()=>backend.TakeDiagnosticCapture(2));
    }

    private static void Timings(string output)
    {
        using var context = new QueuedStaContext();
        using var report = new ValidationReport(Path.Combine(output,"timing-report"));
        try
        {
        using var reporter = new PerfReporter("native-uncaptured",report);
        bool measurementOpen=true;
        using var scene = new HostScene(metrics:m=> { if(measurementOpen) reporter.Report(m); });
        scene.Host.Render(); var a=scene.Host.Metrics;
        Check("T4.A-no-hook",scene.Backend.DiagnosticCaptureRequestCount==0 && scene.Backend.DiagnosticCaptureCopyCount==0);
        measurementOpen=false; reporter.Dispose(); reporter.Dispose();
        long endA=Stopwatch.GetTimestamp();
        string log=Path.Combine(report.DirectoryPath,"report.log");
        string before=File.ReadAllText(log);
        var finals=before.Split('\n',StringSplitOptions.RemoveEmptyEntries).Select(line=>JsonDocument.Parse(line))
            .Where(d=>d.RootElement.GetProperty("kind").GetString()=="metrics-final").ToArray();
        try
        {
            Check("T4.single-final-before-B",finals.Length==1);
            var data=finals[0].RootElement.GetProperty("data");
            Check("T4.final-A-counts",data.GetProperty("renders").GetInt64()==a.RenderCalls
                && data.GetProperty("samples").GetInt64()==1);
            Check("T4.final-process-sample",data.GetProperty("process").GetProperty("Status").GetString()=="measured");
        }
        finally { foreach(var final in finals) final.Dispose(); }
        long beginB=Stopwatch.GetTimestamp(); var frame=scene.Capture(1); long takeEnd=Stopwatch.GetTimestamp();
        long encodeStart=Stopwatch.GetTimestamp();
        string path=Path.Combine(output,"timing-pattern.png"); CapturePngEncoder.Write(path,frame.BgraPremultiplied,96,64);
        long encodeEnd=Stopwatch.GetTimestamp(); CapturePngEncoder.Validate(path,frame.BgraPremultiplied,96,64);
        Check("T4.real-boundaries",endA<=beginB && beginB<=frame.ReadbackStartTicks
            && frame.ReadbackStartTicks<=frame.ReadbackEndTicks && frame.ReadbackEndTicks<=frame.CommittedTicks
            && frame.CommittedTicks<=takeEnd && takeEnd<=encodeStart && encodeStart<=encodeEnd);
        Check("T4.B-single-hook",scene.Backend.DiagnosticCaptureRequestCount==1 && scene.Backend.DiagnosticCaptureCopyCount==1
            && scene.Backend.DiagnosticCaptureCommitCount==1);
        reporter.Report(scene.Host.Metrics); reporter.Dispose();
        Check("T4.no-B-sample-or-double-final",File.ReadAllText(log)==before);
        Results.Add(new { test="T4.timestamps",endA,beginB,frame.ReadbackStartTicks,frame.ReadbackEndTicks,frame.CommittedTicks,takeEnd,encodeStart,encodeEnd });
        }
        finally
        {
            report.Dispose();
            File.Copy(Path.Combine(report.DirectoryPath,"report.log"),Path.Combine(output,"report.log"),false);
        }
    }

    private sealed class ControlledLoader(bool synchronous, bool failure) : IImageResourceLoader
    {
        internal readonly TaskCompletionSource<ImageResourceResult> Completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal Action? OnRequest;
        private static ImageResourceResult Image() => new(new ImageDimensions(1,1),new byte[]{0,0,255,255});
        public ValueTask<ImageResourceResult> LoadAsync(string source,CancellationToken cancellationToken)
        {
            OnRequest?.Invoke();
            if(synchronous)
            {
                if(failure) throw new UnauthorizedAccessException("Controlled Policy failure");
                return ValueTask.FromResult(Image());
            }
            return new ValueTask<ImageResourceResult>(Completion.Task.WaitAsync(cancellationToken));
        }
        internal void Complete()
        {
            if(failure) Completion.SetException(new UnauthorizedAccessException("Controlled Policy failure"));
            else Completion.SetResult(Image());
        }
    }

    private static void Barrier()
    {
        foreach(bool failure in new[]{false,true}) foreach(bool synchronous in new[]{false,true})
            BarrierCase(failure,synchronous);
        NativeCaptureRunner.RequireBeforeDeadline(0,29,1);
        Reject("T5.injected-timeout",()=>NativeCaptureRunner.RequireBeforeDeadline(0,30,1));
    }

    private static void BarrierCase(bool failure,bool synchronous)
    {
        string label=$"T5.{(failure?"failure":"ready")}.{(synchronous?"sync":"async")}";
        using var context=new QueuedStaContext();
        var source=new ControlledLoader(synchronous,failure);
        var terminal=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var trackedLoader=new TrackedLoader(source,()=>{
            terminal.TrySetResult(); return synchronous?Task.CompletedTask:release.Task;
        });
        using var resolver=new TrackedImageResolver(trackedLoader,context);
        const string uri="data:image/png;base64,controlled";
        const string markup="<html><head><style>body{margin:0}#image{width:20px;height:20px;background-image:url('data:image/png;base64,controlled')}</style></head><body><div id='image'></div></body></html>";
        bool registeredBeforeLoader=false;
        source.OnRequest=()=>{ registeredBeforeLoader=resolver.Snapshot().Pending==1; };
        using var scene=new HostScene(markup,resolver);
        int owner=Environment.CurrentManagedThreadId,handlerCount=0;
        resolver.Changed+=(_,args)=>{
            Check(label+".handler-STA",Environment.CurrentManagedThreadId==owner && SynchronizationContext.Current==context);
            Check(label+".terminal-kind",args.State==(failure?ImageResourceState.Failed:ImageResourceState.Ready)
                && args.Failure==(failure?ImageResourceFailure.Policy:null));
            handlerCount++;
        };
        scene.Host.Render();
        Check(label+".registered-before-delegate",registeredBeforeLoader);
        Check(label+".requested-one-uri",resolver.Snapshot().UniqueRequests==1);
        Check(label+".host-initially-settled",!scene.Host.NeedsRender);
        long requestedEpoch=resolver.Snapshot().Epoch;
        if(!synchronous)
        {
            Task.Run(source.Complete).GetAwaiter().GetResult();
            Check(label+".terminal-handshake",terminal.Task.Wait(TimeSpan.FromSeconds(5)));
            Check(label+".terminal-does-not-close",trackedLoader.TerminalCount==1 && resolver.Snapshot().Pending==1 && resolver.Snapshot().Unapplied==0
                && context.PendingCount==0 && handlerCount==0 && !scene.Host.NeedsRender);
            Reject(label+".terminal-not-stable",()=>resolver.RequireStable(scene.Host));
            release.SetResult();
        }
        Check(label+".changed-handshake",resolver.ChangedReceived.WaitOne(TimeSpan.FromSeconds(5)));
        var received=resolver.Snapshot();
        Check(label+".received-not-applied",received.Pending==1 && received.Unapplied==1 && handlerCount==0
            && !scene.Host.NeedsRender && (synchronous || received.Epoch>requestedEpoch));
        Reject(label+".received-not-stable",()=>resolver.RequireStable(scene.Host));
        context.Drain(); var applied=resolver.Snapshot();
        Check(label+".applied-terminal",applied.Pending==0 && applied.Unapplied==0 && applied.Epoch>received.Epoch
            && handlerCount==1 && scene.Host.NeedsRender && applied.Failed==(failure?1:0));
        Reject(label+".host-still-needs-render",()=>resolver.RequireStable(scene.Host));
        scene.Host.Render(); var stable=resolver.RequireStable(scene.Host);
        resolver.Request(uri);
        Check(label+".repeat-does-not-reopen",resolver.RequireStable(scene.Host).Epoch==stable.Epoch
            && resolver.Snapshot().UniqueRequests==1);
        // Model a resource request occurring within the capture interval. Even once its
        // real Changed has been applied, equality with the accepted epoch must reject it.
        resolver.Request("data:image/png;base64,new-controlled-uri");
        Reject(label+".request-during-capture-rejected",()=>resolver.RequireStable(scene.Host,stable.Epoch));
        Check(label+".new-request-changed",resolver.ChangedReceived.WaitOne(TimeSpan.FromSeconds(5)));
        context.Drain(); if(scene.Host.NeedsRender) scene.Host.Render();
        Reject(label+".drain-cannot-hide-epoch",()=>resolver.RequireStable(scene.Host,stable.Epoch));
    }

    private static void Identity(string output)
    {
        string source=Path.Combine(output,"identity-input.html");
        byte[] input=System.Text.Encoding.UTF8.GetBytes("<body>private synthetic identity test</body>");
        File.WriteAllBytes(source,input);
        string hash=Convert.ToHexString(SHA256.HashData(input)).ToLowerInvariant();
        Check("T6.snapshot-exact",NativeCaptureRunner.ValidateSnapshot(input,hash)==System.Text.Encoding.UTF8.GetString(input));
        Reject("T6.snapshot-hash-mismatch",()=>NativeCaptureRunner.ValidateSnapshot(input,new string('0',64)));
        Reject("T6.bad-hash-before-HWND",()=>NativeCaptureRunner.ValidateInput(source,new string('0',64)));
        NativeCaptureRunner.VerifyUnchanged(source,hash);
        File.AppendAllText(source,"changed");
        Reject("T6.changed-after-snapshot",()=>NativeCaptureRunner.VerifyUnchanged(source,hash));
        Reject("T6.existing-output",()=>NativeCaptureRunner.ClaimOutput(output));
        RuntimeGuard.ValidateNames(new[]{"AetherUI.Validator.NativeCapture.dll","aether_core.dll","d2d1.dll","dwrite.dll"});
        Reject("T6.browser-dependency",()=>RuntimeGuard.ValidateNames(new[]{"Microsoft.Web.WebView2.Core.dll"}));
        Reject("T6.browser-module",()=>RuntimeGuard.ValidateNames(new[]{"libcef.dll"}));
        Reject("T6.browser-process",()=>RuntimeGuard.ValidateNames(new[]{"msedge.exe"}));
        // No renderer involved: this is a rejection test, never a fabricated raster artifact.
        byte[] uniform=Enumerable.Repeat((byte)255,1440*900*4).ToArray();
        Reject("T6.uniform-is-not-scene",()=>NativeCaptureRunner.RequireNonUniform(uniform,1440,900));
        // T3 independently proves missing frame cannot be consumed; repeat on a fresh target.
        using var window=new HiddenCaptureWindow(96,64);
        using var backend=new Direct2DRenderBackend(window.Handle,96,64,1,DriverType.Warp);
        Reject("T6.missing-capture",()=>backend.TakeDiagnosticCapture(1));
    }
}
