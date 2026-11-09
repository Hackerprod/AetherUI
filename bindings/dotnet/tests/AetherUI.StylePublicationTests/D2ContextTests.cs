using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Css;
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
    }
    internal static T Field<T>(object value,string name)=>(T)value.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(value)!;
    private static void Oracle(UiDocumentHost host)
    {
        var map=Field<NativeStyleMap>(host,"_nativeVisualStyleMap");var layout=Field<NativeLayoutResult>(host,"_nativeLayout");var runtime=Field<TransitionRuntimeInterop>(host,"_transitionRuntime");
        Check(Native.Oracle(runtime.Handle,map.Handle,layout.Handle,out var equal)==0 && equal!=0,"full oracle disagrees");
    }
    private static void Check(bool value,string message) {if(!value)throw new InvalidOperationException(message);}
    private sealed class Cards(int count,int owners):UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style>html,body{margin:0}.card{height:20px;width:120px}.owner{transform:translateX(0px)}</style><body>");
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
        [DllImport("aether_core",EntryPoint="aether_layout_context_validate_full")] internal static extern int Oracle(IntPtr runtime,IntPtr map,IntPtr layout,out int equal);
    }
}
