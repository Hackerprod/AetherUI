using AetherUI.Css;
using AetherUI.Runtime;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Layout;
using System.Reflection;

if (args.Contains("--e-contract-baseline", StringComparer.Ordinal))
{
    InputEContractBaseline.Run();
    return;
}

if (args.Contains("--baseline", StringComparer.Ordinal))
{
    InputBaseline.Run();
    return;
}

var passed = 0;
foreach (var enabled in new[] { false, true })
{
    Run("shared ancestors and out before over", enabled, (host, c) =>
    {
        host.PointerMoved(new(20,20)); host.Render(); c.Trace.Clear();
        host.PointerMoved(new(20,100));
        Eq("", string.Join(",", c.Trace)); // Deferred event, synchronous cursor.
        Eq(CursorKind.Pointer, host.CursorAt(new(20,100)));
        host.Render();
        Eq("a:mouseout,p:mouseout,b:mouseover,p:mouseover", string.Join(",", c.Trace));
        True((host.Document!.GetElementById("p")!.State & ElementState.Hover) != 0);
    });
    Run("pending replacements and A-B-A cancellation", enabled, (host, c) =>
    {
        host.PointerMoved(new(20,20)); host.PointerMoved(new(20,100)); host.Render();
        Eq("b:mouseover,p:mouseover", string.Join(",", c.Trace)); c.Trace.Clear();
        host.PointerMoved(new(20,20)); host.PointerMoved(new(20,100)); host.Render();
        Eq("", string.Join(",", c.Trace));
        if (enabled) { Eq(4L,host.Metrics.PointerMovesReceived); Eq(2L,host.Metrics.PendingMovesCoalesced); Eq(1L,host.Metrics.PendingMovesProcessed); }
    });
    Run("discrete input precedes pending hover before paint", enabled, (host, c) =>
    {
        host.PointerMoved(new(20,20)); host.PointerPressed(new(20,20));
        host.PointerMoved(new(20,100)); host.PointerReleased(new(20,20));
        Eq("a:mousedown,p:mousedown,a:mouseup,p:mouseup,a:click,p:click", string.Join(",",c.Trace));
        if (enabled) { Eq(2L,host.Metrics.PointerBarriers); Eq(2L,host.Metrics.PointerBarriersWithPendingMove); }
        host.Render();
        Eq("a:mousedown,p:mousedown,a:mouseup,p:mouseup,a:click,p:click,b:mouseover,p:mouseover", string.Join(",",c.Trace));
    });
    Run("exact input counters and pending leave", enabled, (host,c) =>
    {
        host.PointerMoved(new(20,20)); host.CursorAt(new(20,20));
        if (enabled)
        {
            Eq(1L,host.Metrics.PointerMovesReceived); Eq(1L,host.Metrics.HitQueries);
            Eq(1L,host.Metrics.CursorQueries); Eq(1L,host.Metrics.CursorResolutions);
            Eq(0L,host.Metrics.PendingMovesProcessed);
        }
        host.PointerLeft(); host.Render(); Eq("",string.Join(",",c.Trace));
        if (enabled) { Eq(1L,host.Metrics.PointerBarriers); Eq(1L,host.Metrics.PointerBarriersWithPendingMove); }
    });
    Run("stop propagation", enabled, (host,c) =>
    {
        c.Stop = true; host.PointerMoved(new(20,20)); host.Render();
        Eq("a:mouseover",string.Join(",",c.Trace));
    });
    Run("out callback hides incoming target", enabled, (host,c) =>
    {
        host.PointerMoved(new(20,20)); host.Render(); c.Trace.Clear();
        c.OnOut = () =>
        {
            var b = host.Document!.GetElementById("b")!;
            typeof(UiDocumentHost).GetMethod("SetElementState", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(host, [b, ElementState.Invalid, true]);
            typeof(UiDocumentHost).GetMethod("InvalidateVisual", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(host, [false]);
        };
        host.PointerMoved(new(20,100)); host.Render();
        Eq("a:mouseout,p:mouseout,p:mouseover", string.Join(",",c.Trace));
        True((host.Document!.GetElementById("b")!.State & ElementState.Hover) == 0);
    });
    Run("stationary cursor and idle", enabled, (host,c) =>
    {
        Eq(CursorKind.Pointer,host.PointerMoved(new(20,20))); host.Render();
        Eq(CursorKind.Pointer,host.CursorAt(new(20,20)));
        True(!host.NeedsRender);
        var before = host.Metrics;
        for (var i=0;i<100;i++) host.Render();
        Eq(before.StyleResolutions,host.Metrics.StyleResolutions);
        Eq(before.PendingMovesProcessed,host.Metrics.PendingMovesProcessed);
        Eq("a:mouseover,p:mouseover",string.Join(",",c.Trace));
        host.PointerLeft(); Eq(CursorKind.Default,host.CursorAt(new(190,190)));
        Eq("a:mouseover,p:mouseover,a:mouseout,p:mouseout",string.Join(",",c.Trace));
    });
}
foreach(var enabled in new[]{false,true})
{
    using var renderer = new NullRenderer();
    using var host = new UiDocumentHost(new OscillatingComponent(),new ApproximateTextMeasurer(),renderer,
        new UiHostOptions { Width=200,Height=200,EnableInputWorkCounters=enabled });
    host.Render(); host.ResetMetrics(); host.PointerMoved(new(20,20)); host.Render();
    True(!host.NeedsRender); True(host.Metrics.StyleResolutions <= 18);
    True((host.Document!.GetElementById("a")!.State & ElementState.Hover) == 0);
    var before = host.Metrics;
    for(var i=0;i<100;i++) host.Render();
    Eq(before.StyleResolutions,host.Metrics.StyleResolutions);
    Eq(before.HitQueries,host.Metrics.HitQueries);
    Eq(before.PendingMovesProcessed,host.Metrics.PendingMovesProcessed);
    Console.WriteLine($"PASS bounded oscillating hover and idle counters={enabled}"); passed++;
}
Console.WriteLine($"PASS {passed} real Core input traces");
void Run(string name, bool enabled, Action<UiDocumentHost, TraceComponent> test)
{
    var c = new TraceComponent(); using var renderer = new NullRenderer();
    using var host = new UiDocumentHost(c,new ApproximateTextMeasurer(),renderer,new UiHostOptions { Width=200,Height=200,EnableInputWorkCounters=enabled });
    host.Render(); host.ResetMetrics(); test(host,c);
    if (!enabled) { Eq(0L,host.Metrics.PointerMovesReceived); Eq(0L,host.Metrics.HitQueries); Eq(0L,host.Metrics.CursorResolutions); }
    host.ResetMetrics(); Eq(0L,host.Metrics.PointerMovesReceived); Eq(0L,host.Metrics.PendingMovesProcessed); Eq(0L,host.Metrics.PointerBarriers);
    Console.WriteLine($"PASS {name} counters={enabled}"); passed++;
}
static void Eq<T>(T expected,T actual) { if (!Equals(expected,actual)) throw new InvalidOperationException($"Expected [{expected}] got [{actual}]"); }
static void True(bool value) { if (!value) throw new InvalidOperationException("Assertion failed"); }
sealed class TraceComponent : UiComponent
{
    public List<string> Trace { get; } = [];
    public bool Stop { get; set; }
    public Action? OnOut { get; set; }
    protected override void BuildRenderTree(MarkupWriter w)
    {
        w.WriteLiteral("<style>body { margin:0 } #p { width:160px;height:160px } #a,#b { width:80px;height:80px;cursor:pointer } #b:invalid { visibility:hidden }</style><body><div id='p'");
        Events(w,"p"); w.WriteLiteral("><div id='a'"); Events(w,"a");
        w.WriteLiteral("></div><div id='b'"); Events(w,"b"); w.WriteLiteral("></div></div></body>");
    }
    void Events(MarkupWriter w,string id)
    {
        foreach(var type in new[]{"mouseover","mouseout","mousedown","mouseup","click"})
            w.WriteEventAttribute("on"+type,e => { Trace.Add(id+":"+type); if(Stop)e.StopPropagation(); if(id=="a" && type=="mouseout")OnOut?.Invoke(); });
    }
}
sealed class NullRenderer : IRenderBackend
{
    public void Resize(int w,int h,float dpi) { }
    public void Render(DisplayList list,ColorRgba color) { }
    public void Dispose() { }
}

sealed class OscillatingComponent : UiComponent
{
    protected override void BuildRenderTree(MarkupWriter w) => w.WriteLiteral(
        "<style>body { margin:0 } #a { width:80px;height:80px;cursor:pointer } #a:hover { visibility:hidden }</style><body><div id='a'></div></body>");
}
