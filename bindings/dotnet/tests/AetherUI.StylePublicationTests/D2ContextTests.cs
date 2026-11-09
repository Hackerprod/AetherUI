using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

internal static class D2ContextTests
{
    internal static void Run()
    {
        Check(Native.Enable(1) != 0, "D2 tests require diagnostic native build");
        foreach (var size in new[] {100,500,1000})
        foreach (var owners in new[] {0,1,size})
        {
            using var renderer = new PaintCounter();
            using var host = new UiDocumentHost(new Cards(size,owners),new ApproximateTextMeasurer(),renderer,new UiHostOptions {Width=400,Height=400});
            host.ExperimentalStyleDeltas = true;
            host.Render(); host.PointerMoved(new(5,5)); host.Render();
            Oracle(host);
            var layout=host.Layout; var styles=Field<object>(host,"_styles");
            var boxes=host.ProjectedBoxEntriesVisited; var frames=renderer.Frames;
            host.ResetMetrics(); Native.Reset();
            for(var i=0;i<12;++i) {host.PointerMoved(new(5,25+(i%2)*20));host.Render();}
            Check(Native.Read(out var counters)==0,"counters");
            Check(counters.Calls>=12,"EnsureVisualTree must call context preparation");
            Check(counters.Backing==0 && counters.Active==0 && counters.Static==0 && counters.Builds==0 && counters.Allocations==0 && counters.Fallbacks==0,"no context work after warmup: "+counters);
            Check(host.Metrics.LayoutBuilds==0 && host.Metrics.VisualStyleMapBuilds==0 && host.Metrics.VisualStyleMapUpdates==0 && host.Metrics.DisplayListBuilds==0 && host.Metrics.PaintChunkBuilds==0,"no-op host rebuild");
            Check(host.ProjectedBoxEntriesVisited==boxes && ReferenceEquals(layout,host.Layout) && ReferenceEquals(styles,Field<object>(host,"_styles")),"no box walk or projection replacement");
            Check(renderer.Frames==frames,"hover no-op must not paint");
            Oracle(host);
            Console.WriteLine($"PASS D2 real hover pipeline cards={size} static={owners}: {counters}");
        }
        TransitionLifecycle();
        SemanticAndVisualDeltas();
    }
    private static ComputedStyle Transform(float x, float delay=.2f) => new()
    {
        TransitionProperties=2,TransitionDuration=1,TransitionDelay=delay,
        Transforms=[new(TransformOperationKind.Translate,new(x,CssUnit.Pixels),CssLength.Zero,0,0,0,0,0,0,0,0,0)]
    };
    private static (ulong Serial,ulong Size) Snapshot(UiDocumentHost host)
    {
        Check(Native.Info(Field<NativeLayoutResult>(host,"_nativeLayout").Handle,out var serial,out var size)==0,"snapshot info");return (serial,size);
    }
    private static void TransitionLifecycle()
    {
        using var renderer=new PaintCounter();using var host=new UiDocumentHost(new Cards(100,0),new ApproximateTextMeasurer(),renderer,new UiHostOptions{Width=400,Height=400});
        host.ExperimentalStyleDeltas=true;host.Render();host.PointerMoved(new(5,45));host.Render();
        var runtime=Field<TransitionRuntimeInterop>(host,"_transitionRuntime");var dom=Field<DomInterop>(host,"_nativeDom");var node=dom.GetNativeHandle(host.Document!.GetElementById("c0")!);
        var baseline=new ComputedStyle();var opacity=new ComputedStyle{Opacity=.5f,TransitionProperties=1,TransitionDuration=1};
        var initial=Snapshot(host);TransitionStyleMarshaler.Observe(runtime,node,baseline,opacity,0);
        host.ResetMetrics();host.TickTransitions(100);host.Render();Check(host.Metrics.LayoutBuilds==0 && Snapshot(host)==initial,"opacity must not change context");Oracle(host);
        runtime.Tick(2000);
        var target=Transform(20);TransitionStyleMarshaler.Observe(runtime,node,baseline,target,3000);
        host.ResetMetrics();host.TickTransitions(3100);host.Render();Check(host.Metrics.LayoutBuilds==1 && Snapshot(host).Size==1,"delay is active owner");Oracle(host);
        var active=Snapshot(host);host.ResetMetrics();Native.Reset();
        host.PointerMoved(new(5,65));host.Render();Check(Native.Read(out var counts)==0 && counts.Backing==0 && counts.Active==0 && counts.Allocations==0 && counts.Builds==0,"active hover context reuse");
        Check(host.Metrics.LayoutBuilds==0 && Snapshot(host)==active,"active hover must retain context");Oracle(host);
        host.TickTransitions(3500);host.Render();Check(Snapshot(host)==active,"matrix-only tick must preserve membership");Oracle(host);
        var next=Transform(40);TransitionStyleMarshaler.Observe(runtime,node,target,next,3600);host.TickTransitions(3650);host.Render();Check(Snapshot(host)==active,"retarget preserves owner");Oracle(host);
        host.TickTransitions(5000);host.Render();Check(Snapshot(host).Size==0,"finished is not active");Oracle(host);
        TransitionStyleMarshaler.Observe(runtime,node,baseline,target,6000);host.TickTransitions(6100);host.Render();Check(Snapshot(host).Size==1,"restart");
        TransitionStyleMarshaler.Observe(runtime,node,target,baseline,6150);host.PointerMoved(new(5,85));host.Render();Check(Snapshot(host).Size==0,"cancel retires active owner");Oracle(host);
        Console.WriteLine("PASS D2 managed opacity/transform delay/active/matrix tick/retarget/finish/cancel");
    }
    private static void SemanticAndVisualDeltas()
    {
        foreach(var css in new[]{"#c0:hover{background:blue}","#c0:hover{width:7.5rem}","#c0:hover{backdrop-filter:blur(2px)}","#c0{transform:translateX(2px);transition:transform 1s .2s}#c0:hover{transform:none}","#c0{transform:translateX(0px);backdrop-filter:blur(2px)}#c0:hover{transform:none}"})
        {
            using var renderer=new PaintCounter();using var host=new UiDocumentHost(new Cards(100,0,css),new ApproximateTextMeasurer(),renderer,new UiHostOptions{Width=400,Height=400});
            host.ExperimentalStyleDeltas=true;host.Render();var previous=Snapshot(host);var style=Field<StyleMap>(host,"_styles")[host.Document!.GetElementById("c0")!];
            var boxes=host.ProjectedBoxEntriesVisited;host.ResetMetrics();host.PointerMoved(new(5,5));host.Render();Oracle(host);
            if(css.Contains("7.5rem",StringComparison.Ordinal))
            {
                Check(ReferenceEquals(style,Field<StyleMap>(host,"_styles")[host.Document.GetElementById("c0")!]),"metadata-only projection reused");
                Check(!host.HasActiveTransitions && host.Metrics.DisplayListBuilds==0 && host.Metrics.PaintChunkBuilds==0 && host.ProjectedBoxEntriesVisited==boxes,"metadata-only no transitions or paint");
            }
            if(css.Contains("background:blue",StringComparison.Ordinal))
                Check(host.Metrics.LayoutBuilds==0 && host.ProjectedBoxEntriesVisited-boxes<=2 && host.Metrics.StyleUpdatedElements==1,"paint delta touches only indexed owner boxes");
            if(css.StartsWith("#c0:hover{backdrop",StringComparison.Ordinal))Check(Snapshot(host).Size==1 && host.Metrics.LayoutBuilds==1,"backdrop add invalidates context");
            if(css.Contains("transform:none",StringComparison.Ordinal))Check(Snapshot(host)==previous,"remaining static/active cause keeps effective membership");
            host.PointerMoved(new(5,45));host.Render();Oracle(host);
            Console.WriteLine("PASS D2 managed semantic/visual causes: "+css);
        }
    }
    internal static T Field<T>(object value,string name)=>(T)value.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(value)!;
    private static void Oracle(UiDocumentHost host)
    {
        var map=Field<NativeStyleMap>(host,"_nativeVisualStyleMap");var layout=Field<NativeLayoutResult>(host,"_nativeLayout");var runtime=Field<TransitionRuntimeInterop>(host,"_transitionRuntime");
        Check(Native.Oracle(runtime.Handle,map.Handle,layout.Handle,out var equal)==0 && equal!=0,"full oracle disagrees");
    }
    private static void Check(bool value,string message) {if(!value)throw new InvalidOperationException(message);}
    private sealed class Cards(int count,int owners,string extra=""):UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style>html,body{margin:0}.card{height:20px;width:120px}.owner{transform:translateX(0px)}"+extra+"</style><body>");
            for(var i=0;i<count;++i)writer.WriteLiteral($"<div id='c{i}' class='card{(i<owners?" owner":"")}'></div>");
            writer.WriteLiteral("</body>");
        }
    }
    private sealed class PaintCounter:IRenderBackend
    {
        internal int Frames;
        public void Resize(int width,int height,float dpi) {}
        public void Render(DisplayList list,ColorRgba clear) {++Frames;}
        public void Dispose() {}
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Counts
    {
        internal ulong Calls,Backing,Active,Updates,Builds,Reuses,Fallbacks,Allocations,Index,Static,Reason;
        public override readonly string ToString()=>$"calls={Calls} backing={Backing} active={Active} static={Static} builds={Builds} reuse={Reuses} alloc={Allocations} fallback={Fallbacks}/{Reason}";
    }
    private static class Native
    {
        [DllImport("aether_core",EntryPoint="aether_work_counters_set_enabled")] internal static extern int Enable(int enabled);
        [DllImport("aether_core",EntryPoint="aether_work_counters_reset")] internal static extern void Reset();
        [DllImport("aether_core",EntryPoint="aether_context_work_counters_get_v1")] internal static extern int Read(out Counts counts);
        [DllImport("aether_core",EntryPoint="aether_layout_context_snapshot_info")] internal static extern int Info(IntPtr layout,out ulong serial,out ulong size);
        [DllImport("aether_core",EntryPoint="aether_layout_context_validate_full")] internal static extern int Oracle(IntPtr runtime,IntPtr map,IntPtr layout,out int equal);
    }
}
