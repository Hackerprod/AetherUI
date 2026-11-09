using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

CssDiagnosticsTransactionTests.Run();
var count = 0;
Run("aborted inset staging does not consume diagnostic; same-node replay recovers", () =>
{
    var c = new Component("box-shadow:inset 0 0 2px red"); using var r = new Renderer(); using var host = New(c,r);
    var diagnostics = 0;
    Action<CssStyleDiagnostic> listener = d =>
    {
        if (d.Property != "box-shadow") return;
        ++diagnostics;
        var element = host.Document!.GetElementById("a")!;
        State(host, element, ElementState.Invalid, true);
        host.Render(); // Must neither publish nor consume Bnext inside replay.
        Eq(CursorKind.Default, host.CursorAt(new(10,10)));
    };
    CssDiagnostics.Reported += listener;
    try
    {
        host.StylePublicationProbe = phase => { if (phase == "managed-staged") throw new OutOfMemoryException("staging fault"); };
        Throws<OutOfMemoryException>(host.Render);
        Eq(0, diagnostics); Eq(0, r.Frames); Eq(0L, host.HostPublicationSerial); True(host.ManagedProjectionOutOfDate);
        host.StylePublicationProbe = null; host.Render();
        Eq(1, diagnostics); Eq(1, r.Frames); True(!host.ManagedProjectionOutOfDate);
        True((host.Document!.GetElementById("a")!.State & ElementState.Invalid) != 0);
        Eq(0f, Style(host).BackgroundColor.R); Eq(1f, Style(host).BackgroundColor.G);
        True(!host.NeedsRender);
    }
    finally { CssDiagnostics.Reported -= listener; }
});
Run("native callback R2 supersedes R1 without overwriting newer native inputs", () =>
{
    var c = new Component("transform:var(--undefined)"); using var r = new Renderer(); using var host = New(c,r);
    var runtime = Runtime(host); var invoked = false; ulong innerGeneration = 0;
    StyleDiagnosticCallback callback = (_,_,_,_,_) =>
    {
        if (invoked) return; invoked = true;
        var dom = Field<DomInterop>(host,"_nativeDom");
        dom.Native.SetAttribute(dom.GetNativeHandle(host.Document!.GetElementById("a")!),"style","width:99px");
        using var inner = runtime.PreparePublication(null,200,200,1);
        True(inner.Commit()); using var map = inner.Publish(); innerGeneration = inner.Token.Generation;
        host.Render(); Eq(0,r.Frames);
    };
    Native.SetDiagnostics(Handle(runtime),callback,IntPtr.Zero);
    host.Render(); GC.KeepAlive(callback);
    True(invoked); True(Field<NativeStylePublicationToken>(host,"_publishedStyleToken").Generation > innerGeneration);
    Eq(99f,Style(host).Width.Value); Eq(1,r.Frames); True(!host.ManagedProjectionOutOfDate);
});
Run("native mutation without R2 forces recovery", () =>
{
    using var r = new Renderer(); using var host = New(new Component("transform:var(--undefined)"),r);
    var called = false;
    StyleDiagnosticCallback callback = (_,_,_,_,_) =>
    {
        if(called)return; called=true;
        var dom=Field<DomInterop>(host,"_nativeDom");
        dom.Native.SetAttribute(dom.GetNativeHandle(host.Document!.GetElementById("a")!),"style","width:91px");
    };
    Native.SetDiagnostics(Handle(Runtime(host)),callback,IntPtr.Zero);
    host.Render(); GC.KeepAlive(callback); True(called); Eq(91f,Style(host).Width.Value); Eq(1,r.Frames);
});
Run("staging callback changes same node and preserves Bnext", () =>
{
    using var r=new Renderer(); using var host=New(new Component(),r); host.Render();
    var a=host.Document!.GetElementById("a")!; State(host,a,ElementState.Hover,true);
    var first=true;
    host.StylePublicationProbe=phase=> {if(phase=="managed-staged"&&first){first=false;State(host,a,ElementState.Invalid,true);host.Render();}};
    host.Render(); Eq(2,r.Frames); Eq(1f,Style(host).BackgroundColor.G); True(!host.ManagedProjectionOutOfDate);
});
Run("notification exception remains observable and preserves pending input", () =>
{
    using var r=new Renderer(); using var host=New(new Component(),r); host.Render();
    var key=Guid.NewGuid().ToString(); var error=new ApplicationException("listener failure");
    host.StylePublicationProbe=phase=> {if(phase=="native-preview")CssDiagnostics.ReportUnknownProperty(key,key);};
    Action<CssStyleDiagnostic> listener=d=> {if(d.Property==key){State(host,host.Document!.GetElementById("a")!,ElementState.Invalid,true);throw error;}};
    CssDiagnostics.Reported+=listener;
    try
    {
        State(host,host.Document!.GetElementById("a")!,ElementState.Hover,true);
        try{host.Render();throw new Exception("expected notification failure");}catch(CssDiagnostics.NotificationException ex){True(ReferenceEquals(error,ex.InnerException));}
        Eq(1,r.Frames); True(host.ManagedProjectionOutOfDate); True(!host.NeedsRender);
    }
    finally{CssDiagnostics.Reported-=listener;host.StylePublicationProbe=null;}
    host.Render(); Eq(2,r.Frames); Eq(1f,Style(host).BackgroundColor.G);
});
Run("continuous replay invalidation is bounded, dirty retained, explicit retry succeeds", () =>
{
    using var r=new Renderer(); using var host=New(new Component(),r); host.Render();
    var prefix=Guid.NewGuid().ToString(); var sequence=0;
    host.StylePublicationProbe=phase=> {if(phase=="native-preview")CssDiagnostics.ReportUnknownProperty(prefix,(++sequence).ToString());};
    Action<CssStyleDiagnostic> listener=d=>{if(d.Property==prefix)State(host,host.Document!.GetElementById("a")!,ElementState.Invalid,(sequence&1)!=0);};
    CssDiagnostics.Reported+=listener;
    try
    {
        State(host,host.Document!.GetElementById("a")!,ElementState.Hover,true);
        Throws<InvalidOperationException>(host.Render); Eq(2,sequence); Eq(1,r.Frames); True(host.ManagedProjectionOutOfDate);True(!host.NeedsRender);
    }
    finally{CssDiagnostics.Reported-=listener;host.StylePublicationProbe=null;}
    host.Render(); Eq(2,r.Frames);True(!host.ManagedProjectionOutOfDate);
});
Run("reentrant discrete events retain FIFO ordering", () =>
{
    var c=new Component(); using var r=new Renderer();using var host=New(c,r);host.Render();
    var key=Guid.NewGuid().ToString();host.StylePublicationProbe=p=>{if(p=="native-preview")CssDiagnostics.ReportUnknownProperty(key,key);};
    Action<CssStyleDiagnostic> listener=d=>{if(d.Property==key){host.PointerPressed(new(10,10));host.PointerReleased(new(10,10));Eq("",string.Join(",",c.Trace));}};
    CssDiagnostics.Reported+=listener;
    try{State(host,host.Document!.GetElementById("a")!,ElementState.Invalid,true);host.Render();}
    finally{CssDiagnostics.Reported-=listener;}
    Eq("mousedown,mouseup,click",string.Join(",",c.Trace));Eq(2,r.Frames);
});
Run("dispose during native replay defers resource destruction", () =>
{
    using var r=new Renderer();var host=New(new Component("transform:var(--undefined)"),r);var during=false;
    StyleDiagnosticCallback callback=(_,_,_,_,_)=>{host.Dispose();during=!r.Disposed;};
    Native.SetDiagnostics(Handle(Runtime(host)),callback,IntPtr.Zero);
    Throws<ObjectDisposedException>(host.Render);GC.KeepAlive(callback);True(during);True(r.Disposed);Eq(0,r.Frames);True(!host.NeedsRender);
});
Run("component replacement in replay uses current markup", () =>
{
    var c=new Component();using var r=new Renderer();using var host=New(c,r);host.Render();
    var key=Guid.NewGuid().ToString();host.StylePublicationProbe=p=>{if(p=="native-preview")CssDiagnostics.ReportUnknownProperty(key,key);};
    Action<CssStyleDiagnostic> listener=d=>{if(d.Property==key){c.Width=123;c.Invalidate();host.Render();}};
    CssDiagnostics.Reported+=listener;
    try{State(host,host.Document!.GetElementById("a")!,ElementState.Invalid,true);host.Render();}
    finally{CssDiagnostics.Reported-=listener;}
    Eq(123f,Style(host).Width.Value);Eq(2,r.Frames);True(!host.ManagedProjectionOutOfDate);
});
Run("unsupported direct managed DOM mutation fails closed pending DOMsync", () =>
{
    using var r=new Renderer();using var host=New(new Component(),r);host.Render();
    var once=true;host.StylePublicationProbe=p=>{if(p=="managed-staged"&&once){once=false;host.Document!.GetElementById("a")!.SetAttribute("style","width:177px");}};
    State(host,host.Document!.GetElementById("a")!,ElementState.Invalid,true);
    Throws<InvalidOperationException>(host.Render);Eq(1,r.Frames);True(host.ManagedProjectionOutOfDate);Eq(CursorKind.Default,host.CursorAt(new(10,10)));
});
Console.WriteLine($"PASS {count} D1 host protocol tests; managed DOM synchronization remains a separate pending gate.");
void Run(string name,Action action){action();++count;Console.WriteLine("PASS "+name);}
static UiDocumentHost New(Component c,Renderer r)=>new(c,new ApproximateTextMeasurer(),r,new UiHostOptions{Width=200,Height=200});
static T Field<T>(object o,string name)=>(T)o.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(o)!;
static StyleRuntimeInterop Runtime(UiDocumentHost h)=>Field<StyleRuntimeInterop>(h,"_nativeStyleRuntime");
static IntPtr Handle(StyleRuntimeInterop r)=>Field<IntPtr>(r,"_runtime");
static ComputedStyle Style(UiDocumentHost h)=>Field<Dictionary<ElementNode,ComputedStyle>>(h,"_computedStyles")[h.Document!.GetElementById("a")!];
static void State(UiDocumentHost h,ElementNode e,ElementState flag,bool enabled)
{
    typeof(UiDocumentHost).GetMethod("SetElementState",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(h,[e,flag,enabled]);
    typeof(UiDocumentHost).GetMethod("InvalidateVisual",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(h,[false]);
}
static void True(bool value){if(!value)throw new InvalidOperationException("Assertion failed");}
static void Eq<T>(T expected,T actual){if(!Equals(expected,actual))throw new InvalidOperationException($"Expected {expected}, got {actual}");}
static void Throws<T>(Action action)where T:Exception{try{action();}catch(T){return;}throw new InvalidOperationException("Expected "+typeof(T).Name);}
sealed class Component(string extra="") : UiComponent
{
    internal int Width=80;
    internal List<string> Trace=[];
    internal void Invalidate()=>StateHasChanged();
    protected override void BuildRenderTree(MarkupWriter w)
    {
        w.WriteLiteral($"<style>body{{margin:0}}#a{{width:{Width}px;height:80px;background:red;cursor:pointer;{extra}}}#a:invalid{{background:lime}}</style><body><button id='a' type='button'");
        foreach(var type in new[]{"mousedown","mouseup","click"})w.WriteEventAttribute("on"+type,_=>Trace.Add(type));
        w.WriteLiteral("></button></body>");
    }
}
sealed class Renderer : IRenderBackend
{
    internal int Frames;internal bool Disposed;
    public void Resize(int width,int height,float dpi){}
    public void Render(DisplayList display,ColorRgba color){if(Disposed)throw new ObjectDisposedException(nameof(Renderer));++Frames;}
    public void Dispose()=>Disposed=true;
}
static class Native
{
    [DllImport("aether_core",EntryPoint="aether_style_runtime_set_diagnostics_callback",CallingConvention=CallingConvention.Cdecl)]
    internal static extern void SetDiagnostics(IntPtr runtime,StyleDiagnosticCallback callback,IntPtr data);
}
