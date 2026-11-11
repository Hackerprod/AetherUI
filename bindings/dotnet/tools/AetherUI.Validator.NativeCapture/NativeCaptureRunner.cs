using System.Diagnostics;
using System.Text;
using AetherUI.Dom;
using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Graphics;
using AetherUI.Windows.Resources;
using Vortice.Direct3D;
namespace AetherUI.Validator.NativeCapture;
internal static class NativeCaptureRunner
{
    internal const string FixtureHash="eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837";
    internal static string ValidateInput(string page,string expected)
    {
        if(expected!=FixtureHash) throw new ArgumentException("Only the reviewed fixture hash is supported");
        byte[] bytes=File.ReadAllBytes(page);
        if(bytes.Length!=474495) throw new ArgumentException("Fixture byte length differs");
        return ValidateSnapshot(bytes,expected);
    }
    internal static string ValidateSnapshot(byte[] bytes,string expected)
    {
        if(ArtifactManifest.Hash(bytes)!=expected) throw new ArgumentException("Snapshot SHA256 differs");
        using var input=new MemoryStream(bytes,false); using var reader=new StreamReader(input,Encoding.UTF8,true); return reader.ReadToEnd();
    }
    internal static void VerifyUnchanged(string page,string expectedSha)
    { if(ArtifactManifest.HashFile(page)!=expectedSha) throw new InvalidOperationException("Input changed after snapshot"); }
    internal static void ClaimOutput(string path)
    {
        if(Directory.Exists(path)||File.Exists(path)) throw new ArgumentException("Output must not already exist");
        Directory.CreateDirectory(path);
        using var claim=new FileStream(Path.Combine(path,".capture-owner"),FileMode.CreateNew,FileAccess.Write);
    }
    internal static void RequireBeforeDeadline(long start,long now,long frequency)
    { if(frequency<=0 || now<start || (now-start)/(double)frequency>=30) throw new TimeoutException("Resource stabilization deadline reached"); }
    internal static int Run(string page,string markup,string output)
    {
        var owned=new List<IDisposable>();
        T Own<T>(T value) where T:IDisposable { owned.Add(value); return value; }
        var failures=new List<string>(); var incomplete=new List<string>();
        var manifest=new Dictionary<string,object?>
        {
            ["schema"]="aether-native-capture-v1", ["phase"]="initialization", ["snapshotSha256"]=FixtureHash, ["applicationGeneration"]=1,
            ["driver"]="warp", ["viewport"]=new{width=1440,height=900,dpi=96,scrollX=0,scrollY=0},
            ["timestampFrequency"]=Stopwatch.Frequency,["os"]=Environment.OSVersion.ToString(),["runtime"]=Environment.Version.ToString(),
            ["effectiveGlyphFallback"]="unverified",["fontParity"]="not-evaluated",["visualParity"]="not-run",
            ["declaredCssFontFamily"]="Arial, Helvetica, sans-serif",["textAntialiasMode"]="Grayscale",
            ["visualInspection"]="pending",["pngValid"]=false,["resourceCompleteness"]="unknown",
            ["scope"]="Different frames A/B, same immutable snapshot; B is full diagnostic fallback with readback, not pure render or FPS; no conformance verdict"
        };
        Direct2DRenderBackend? renderer=null; ValidationReport? report=null; int exit=4;
        try
        {
            var context=Own(new QueuedStaContext());
            var window=Own(new HiddenCaptureWindow(1440,900));
            renderer=Own(new Direct2DRenderBackend(window.Handle,1440,900,1,DriverType.Warp));
            var http=Own(new HttpImageResourceLoader());
            var loader=new TrackedLoader(new CompositeImageResourceLoader(http,new DataImageResourceLoader(),new FileImageResourceLoader()));
            var images=Own(new TrackedImageResolver(loader,context));
            var component=Own(new RawHtmlFileComponent(markup));
            report=Own(new ValidationReport(Path.Combine(output,"session")));
            var reporter=Own(new PerfReporter("native-uncaptured",report)); bool measurementOpen=true;
            var host=Own(new UiDocumentHost(component,renderer,renderer,new UiHostOptions
            {
                Width=1440,Height=900,ClearColor=new ColorRgba(0.5f,0.5f,0.5f,1),ImageResolver=images,
                MetricsSink=m=>{if(measurementOpen)reporter.Report(m);}
            }));
            host.SetViewport(1440,900,1);
            manifest["phase"]="uncaptured-initialization";
            long startA=Stopwatch.GetTimestamp(); host.Render(); int frames=1;
            while(true)
            {
                RequireBeforeDeadline(startA,Stopwatch.GetTimestamp(),Stopwatch.Frequency);
                context.Drain();
                if(host.NeedsRender)
                {
                    if(frames>=8) throw new InvalidOperationException("Stabilization frame limit reached");
                    host.Render(); frames++; continue;
                }
                var state=images.Snapshot();
                if(state.Error is not null) throw new InvalidOperationException(state.Error);
                if(state.Pending==0 && state.Unapplied==0 && context.PendingCount==0) break;
                context.WaitForWork(TimeSpan.FromMilliseconds(100)); // Notification wait; never evidence of stability.
            }
            var barrier=images.RequireStable(host);
            measurementOpen=false; reporter.Dispose(); var metricsA=host.Metrics; long endA=Stopwatch.GetTimestamp();
            if(renderer.DiagnosticCaptureRequestCount!=0 || renderer.DiagnosticCaptureCopyCount!=0) throw new InvalidOperationException("Capture hook contaminated interval A");
            manifest["intervalA"]=new{startTicks=startA,endTicks=endA,frames,metrics=metricsA,captureRequests=renderer.DiagnosticCaptureRequestCount,copies=renderer.DiagnosticCaptureCopyCount};
            manifest["resources"]=images.Resources(); manifest["resourceBarrier"]=barrier;
            manifest["loaderTerminalTelemetry"]=loader.TerminalCount;
            manifest["resourceCompleteness"]=barrier.Failed==0?"complete":"incomplete";
            if(barrier.Failed!=0) incomplete.Add("resource-failures");
            manifest["hasActiveTransitions"]=host.HasActiveTransitions;
            if(host.HasActiveTransitions) incomplete.Add("animations-unsettled");
            var elements=host.Document!.DescendantElements().ToArray();
            manifest["imageElements"]=elements.Count(e=>e.TagName=="img");
            manifest["imageElementUniqueSources"]=elements.Where(e=>e.TagName=="img").Select(e=>e.GetAttribute("src")).Distinct(StringComparer.Ordinal).Count();
            RuntimeGuard.Check(); images.RequireStable(host,barrier.Epoch);
            long frameBefore=renderer.BackendFrameSequence;
            manifest["phase"]="capture-frame";
            long startB=Stopwatch.GetTimestamp(); renderer.BeginDiagnosticCapture(new CaptureRequest(1,1440,900,1));
            host.Render(); long endB=Stopwatch.GetTimestamp(); var frame=renderer.TakeDiagnosticCapture(1); long taken=Stopwatch.GetTimestamp();
            RuntimeGuard.Check();
            bool Stable() { try { images.RequireStable(host,barrier.Epoch); return true; } catch(InvalidOperationException){return false;} }
            if(!Stable()) incomplete.Add("resource-or-host-changed-during-capture");
            if(frame.RequestId!=1 || frame.PixelWidth!=1440 || frame.PixelHeight!=900 || frame.DpiScale!=1 || frame.RowStride!=5760 || frame.BgraPremultiplied.Length!=1440*900*4 || frame.BackendFrameSequence<=frameBefore
                || frame.ReadbackStartTicks<startB || frame.ReadbackEndTicks<frame.ReadbackStartTicks || frame.CommittedTicks<frame.ReadbackEndTicks || frame.CommittedTicks>endB || renderer.DiagnosticCaptureRequestCount!=1 || renderer.DiagnosticCaptureCopyCount!=1 || renderer.DiagnosticCaptureCommitCount!=1)
                throw new InvalidOperationException("Frame identity, timing or capture counter validation failed");
            string pending=Path.Combine(output,"native.png.pending");
            manifest["phase"]="encode";
            long encodeStart=Stopwatch.GetTimestamp(); CapturePngEncoder.Write(pending,frame.BgraPremultiplied,frame.PixelWidth,frame.PixelHeight); long encodeEnd=Stopwatch.GetTimestamp();
            manifest["phase"]="independent-decode";
            long decodeStart=Stopwatch.GetTimestamp(); CapturePngEncoder.Validate(pending,frame.BgraPremultiplied,frame.PixelWidth,frame.PixelHeight); long decodeEnd=Stopwatch.GetTimestamp();
            byte[] rgba=CapturePngEncoder.ToRgba(frame.BgraPremultiplied,frame.PixelWidth,frame.PixelHeight);
            if(Enumerable.Range(0,1440*900).Any(i=>rgba[i*4+3]!=255)) incomplete.Add("unexpected-nonopaque-alpha");
            if(!Stable()) incomplete.Add("resource-or-host-changed-during-encoding");
            try { VerifyUnchanged(page,FixtureHash); } catch(InvalidOperationException) { incomplete.Add("input-file-changed"); }
            manifest["intervalB"]=new{startTicks=startB,endTicks=endB,takenTicks=taken,hostCaptureFrameWallMs=Ms(endB-startB),readbackWallMs=Ms(frame.ReadbackEndTicks-frame.ReadbackStartTicks),encodeStartTicks=encodeStart,encodeEndTicks=encodeEnd,encodeWallMs=Ms(encodeEnd-encodeStart),decodeStartTicks=decodeStart,decodeEndTicks=decodeEnd,decodeValidationWallMs=Ms(decodeEnd-decodeStart),frame.RequestId,frame.BackendFrameSequence,frame.DeviceGeneration,frame.ReadbackStartTicks,frame.ReadbackEndTicks,frame.CommittedTicks};
            manifest["loadedIdentity"]=ArtifactManifest.LoadedIdentity();
            manifest["bgraSha256"]=ArtifactManifest.Hash(frame.BgraPremultiplied); manifest["pngSha256"]=ArtifactManifest.HashFile(pending);
            manifest["pngValid"]=true;
            // Predefined check failures preserve the genuine decoded raster as failed evidence, never acceptance.
            manifest["phase"]="landmarks";
            try { manifest["landmarks"]=CheckLandmarks(host,rgba,manifest); }
            catch(Exception error) { failures.Add("landmark-check: "+error.GetType().Name); }
            RuntimeGuard.Check();
            if(!Stable()) incomplete.Add("state-changed-before-finalization");
            try { VerifyUnchanged(page,FixtureHash); } catch(InvalidOperationException) { incomplete.Add("input-changed-before-finalization"); }
            manifest["phase"]="finalization";
            manifest["finalResourceBarrier"]=images.Snapshot();
            File.Move(pending,Path.Combine(output,"native.png"));
            exit=failures.Count!=0?4:incomplete.Count==0?0:3;
        }
        catch(Exception error)
        { failures.Add(error.GetType().Name); manifest["failurePhase"]=manifest["phase"]; exit=4; }
        finally
        {
            if(renderer is not null) try { renderer.CancelDiagnosticCapture(1); } catch(Exception error) { failures.Add("cancel: "+error.GetType().Name); }
            for(int i=owned.Count-1;i>=0;i--) try { owned[i].Dispose(); } catch(Exception error) { failures.Add("cleanup: "+error.GetType().Name); }
            if(failures.Count!=0) exit=4;
            if(report is not null) try { File.Copy(Path.Combine(report.DirectoryPath,"report.log"),Path.Combine(output,"report.log"),false); } catch(Exception error) { failures.Add("report: "+error.GetType().Name); exit=4; }
        }
        manifest["exitCode"]=exit; manifest["status"]=exit==0?"raster-valid-awaiting-visual-review":exit==3?"diagnostic-incomplete":"failed";
        manifest["accepted"]=false; manifest["errors"]=failures; manifest["incompleteReasons"]=incomplete;
        ArtifactManifest.Write(Path.Combine(output,"manifest.json"),manifest); return exit;
    }
    private static double Ms(long ticks)=>ticks*1000d/Stopwatch.Frequency;
    internal static void RequireNonUniform(byte[] rgba,int width,int height)
    {
        if(width<=0 || height<=0 || rgba.Length!=checked(width*height*4)) throw new InvalidDataException("Raster dimensions invalid");
        if(Enumerable.Range(1,width*height-1).All(i=>rgba.AsSpan(i*4,4).SequenceEqual(rgba.AsSpan(0,4)))) throw new InvalidDataException("Uniform capture rejected");
    }
    internal static object CheckLandmarks(UiDocumentHost host,byte[] rgba,IDictionary<string,object?>? evidence=null)
    {
        RequireNonUniform(rgba,1440,900);
        var doc=host.Document??throw new InvalidOperationException("Missing DOM");
        var header=doc.DescendantElements().First(e=>e.TagName=="header"&&e.HasClass("page-header"));
        var title=header.DescendantElements().First(e=>e.TagName=="h1"&&e.TextContent=="Flexbox Runtime Validation Suite");
        var main=doc.DescendantElements().First(e=>e.TagName=="main");
        var summary=main.Children.OfType<ElementNode>().First(e=>e.TagName=="section"&&e.HasClass("summary"));
        RectF Bounds(ElementNode element)
        {
            var box=host.Layout!.FindPrimaryBox(element)??throw new InvalidOperationException("Missing landmark primary box"); var b=box.BorderBox;
            if(!float.IsFinite(b.X)||!float.IsFinite(b.Y)||!float.IsFinite(b.Width)||!float.IsFinite(b.Height)||b.Width<=0||b.Height<=0||b.Right<=0||b.Bottom<=0||b.Left>=1440||b.Top>=900) throw new InvalidDataException("Landmark box must be finite, positive and visible"); return b;
        }
        var hb=Bounds(header); var tb=Bounds(title); var sb=Bounds(summary);
        if(evidence is not null) evidence["landmarkBounds"]=new{header=hb,title=tb,summary=sb};
        bool White(int x,int y,int min) { int i=(y*1440+x)*4; return rgba[i]>=min&&rgba[i+1]>=min&&rgba[i+2]>=min; }
        int h=(12*1440+12)*4; if(Math.Abs(rgba[h]-23)>2||Math.Abs(rgba[h+1]-33)>2||Math.Abs(rgba[h+2]-43)>2) throw new InvalidDataException("Predefined header background landmark failed");
        int white=0; for(int y=Math.Max(0,(int)Math.Ceiling(tb.Top));y<Math.Min(900,(int)Math.Floor(tb.Bottom));y++) for(int x=Math.Max(0,(int)Math.Ceiling(tb.Left));x<Math.Min(1440,(int)Math.Floor(tb.Right));x++) if(White(x,y,240)) white++;
        if(white<30) throw new InvalidDataException("Predefined title white pixels failed");
        int sx=(int)Math.Ceiling(sb.Left)+8,sy=(int)Math.Ceiling(sb.Top)+8;
        if(sx<0||sy<0||sx>=1440||sy>=900||sx>=sb.Right-2||sy>=sb.Bottom-2||!White(sx,sy,250)) throw new InvalidDataException("Predefined summary background landmark failed");
        bool flex001=doc.DescendantElements().Any(e=>e.Id=="FLEX-001" || e.GetAttribute("data-case-id")=="FLEX-001" || e.TextContent.Contains("FLEX-001",StringComparison.Ordinal));
        if(!flex001) throw new InvalidDataException("FLEX-001 absent from DOM");
        return new{header=hb,title=tb,summary=sb,titleWhitePixels=white,summaryPoint=new{x=sx,y=sy},flex001Present=flex001,flex001Visibility="not-asserted"};
    }
}
