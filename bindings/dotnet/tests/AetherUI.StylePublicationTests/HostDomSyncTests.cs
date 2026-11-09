using System.Reflection;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

internal static class HostDomSyncTests
{
    internal static void Run()
    {
        GetterImportsWithoutNotification();
        ValuesAndSourceRemainInPlace();
        BookkeepingAbortPreservesInputs();
        Console.WriteLine("PASS host DOM current-input import, typed edits, source slots and staged bookkeeping");
    }
    private static void GetterImportsWithoutNotification()
    {
        using var renderer = new TestRenderer(); using var host = New(renderer); host.Render();
        var input = host.Document!.GetElementById("i")!; var dom = Field<DomInterop>(host, "_nativeDom");
        var handle = dom.GetNativeHandle(input); var identity = input.NativeRecordCore!.Identity;
        var calls = 0; host.RenderRequested += (_, _) => calls++;
        dom.Native.SetState(handle, (int)ElementState.Invalid);
        dom.Native.SetAttribute(handle, "style", "width:73px");
        Check(host.NeedsRender, "Pure host entry missed native input.");
        Check(calls == 0, "NeedsRender invoked a notification.");
        Check(input.State == ElementState.Invalid && input.GetAttribute("style") == "width:73px", "Getter failed to import R2.");
        Check(calls == 0, "Getter dispatched RenderRequested.");
        input.State |= ElementState.Hover;
        Check(calls == 1 && input.State == (ElementState.Invalid | ElementState.Hover), "Compound state write lost imported flags or notified twice.");
        host.Render();
        Check(ReferenceEquals(input, host.Document.GetElementById("i")) && dom.GetNativeHandle(input) == handle && input.NativeRecordCore.Identity == identity, "In-place sync replaced the wrapper or handle.");
        Check(!host.ManagedProjectionOutOfDate && renderer.Frames == 2, "Current inputs were not rendered.");
    }
    private static void ValuesAndSourceRemainInPlace()
    {
        using var renderer = new TestRenderer(); using var host = New(renderer); host.Render();
        var input = host.Document!.GetElementById("i")!; var dom = Field<DomInterop>(host, "_nativeDom");
        var runtime = Field<StyleRuntimeInterop>(host, "_nativeStyleRuntime"); var slot = runtime.ReadDomSourceSlot();
        var transition = Field<object>(host, "_transitionRuntime");
        input.RuntimeValue = "override"; input.SetAttribute("value", "new base"); input.SetAttribute("id", "next");
        var values = Field<Dictionary<string,string>>(host, "_uncontrolledInputValues");
        Check(values["i"] == "override" && values["next"] == "override", "Value/id edit lost historical cache entries.");
        input.RuntimeValue = null;
        Check(Field<Dictionary<string,string>>(host, "_uncontrolledInputValues")["next"] == "new base", "Null override reinstalled old cache.");
        dom.Native.SetAttribute(dom.GetNativeHandle(input), "value", "native latest");
        Check(input.RuntimeValue is null && input.GetAttribute("value") == "native latest", "Legacy value did not supersede runtime override.");
        Check(Field<Dictionary<string,string>>(host, "_uncontrolledInputValues")["next"] == "native latest", "Import failed to stage effective value cache.");
        var source = host.Document.GetElementById("css")!;
        source.TextContent = "body{margin:0} input{width:91px;height:20px}";
        Check(runtime.ReadDomSourceSlot().Handle == slot.Handle, "Changing source moved its chronological slot.");
        var added = new ElementNode("span"); added.SetAttribute("id", "added"); added.AppendChild(new TextNode("typed"));
        host.Document.GetElementById("parent")!.AppendChild(added);
        var expectedIndex = host.Document.DescendantElements().Select(dom.GetNativeHandle).ToArray();
        Check(Field<List<IntPtr>>(runtime, "_elements").SequenceEqual(expectedIndex), "Typed insertion left the public runtime element index stale.");
        runtime.ResolveStateChanges(new[] { dom.GetNativeHandle(added) }, new[] { (int)ElementState.Invalid });
        Check(added.State == ElementState.Invalid, "Public native state resolution ignored a newly inserted element.");
        host.Render();
        Check(ReferenceEquals(transition, Field<object>(host, "_transitionRuntime")) && ReferenceEquals(added, host.Document.GetElementById("added")), "DOM sync reset transitions or rebuilt wrappers.");
        Check(!host.ManagedProjectionOutOfDate, "Structural/source edit failed to recover.");
    }
    private static void BookkeepingAbortPreservesInputs()
    {
        using var renderer = new TestRenderer(); using var host = New(renderer); host.Render();
        var input = host.Document!.GetElementById("i")!; var dom = Field<DomInterop>(host, "_nativeDom");
        var token = NativeDomSync.ReadToken(dom.Native); var version = host.Document.InputVersion;
        var values = Field<object>(host, "_uncontrolledInputValues"); var serial = host.HostInputSerial;
        host.DomSynchronizationProbe = () => throw new OutOfMemoryException("managed bookkeeping");
        Throws<OutOfMemoryException>(() => input.RuntimeValue = "not committed");
        host.DomSynchronizationProbe = null;
        Check(NativeDomSync.ReadToken(dom.Native) == token && host.Document.InputVersion == version && host.HostInputSerial == serial, "Failed bookkeeping published inputs/counters.");
        Check(ReferenceEquals(values, Field<object>(host, "_uncontrolledInputValues")) && input.RuntimeValue == "base", "Failed bookkeeping changed value/cache.");
        Set(host, "_hostInputSerial", long.MaxValue);
        Throws<OverflowException>(() => input.SetAttribute("overflow", "never"));
        Check(NativeDomSync.ReadToken(dom.Native) == token && !input.HasAttribute("overflow"), "Host serial exhaustion crossed native commit.");
        Set(host, "_hostInputSerial", serial);
    }
    private static UiDocumentHost New(TestRenderer renderer) => new(new TestComponent(), new ApproximateTextMeasurer(), renderer, new UiHostOptions { Width=200, Height=200 });
    private static T Field<T>(object source,string name)=>(T)source.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(source)!;
    private static void Set(object source,string name,object value)=>source.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic)!.SetValue(source,value);
    private static void Check(bool condition,string message){if(!condition)throw new InvalidOperationException(message);}
    private static void Throws<T>(Action action) where T:Exception {try{action();}catch(T){return;}throw new InvalidOperationException("Expected "+typeof(T).Name);}
    private sealed class TestComponent : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral("<style id='css'>body{margin:0} input{width:80px;height:20px}</style><body><div id='parent'><input id='i' value='base'></div></body>");
    }
    private sealed class TestRenderer : IRenderBackend
    {
        internal int Frames;
        public void Resize(int width,int height,float dpi){}
        public void Render(DisplayList list,ColorRgba color)=>Frames++;
        public void Dispose(){}
    }
}
