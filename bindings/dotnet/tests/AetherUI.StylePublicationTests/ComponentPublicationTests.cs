using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

// Integration tests use the production host, native parser, exact mapping and source compiler.
// Phase probes simulate managed failure AFTER those real operations. They do not inject native OOM.
internal static class ComponentPublicationTests
{
    [DllImport("aether_core", EntryPoint = "aether_style_runtime_add_stylesheet", CallingConvention = CallingConvention.Cdecl)]
    private static extern int AddSheet(IntPtr runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string source,
        int layer, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out long handle, out int rules);

    internal static void Run()
    {
        EveryStagingFailurePreservesA();
        DirectRenderCannotReroutePublishedHost();
        ReentrantBuildCannotPublishB();
        NativeOldDocumentWriteSupersedesCandidate();
        AfterCommitFailureKeepsB();
        Console.WriteLine("PASS component host staging, pinned dispatch and source-slot replacement");
    }

    private static void EveryStagingFailurePreservesA()
    {
        foreach (var phase in new[] { "built", "parsed", "mapped", "initialized", "slot-prepared", "ready" })
        {
            var component = new TestComponent();
            using var renderer = new TestRenderer();
            using var host = New(component, renderer);
            host.Render();
            var document = host.Document!;
            var a = document.GetElementById("a")!;
            var dom = Field<DomInterop>(host, "_nativeDom");
            var registry = Field<UiComponent.EventRegistry>(host, "_componentRegistry");
            var styles = Field<object>(host, "_computedStyles");
            var transitions = Field<object>(host, "_transitionRuntime");
            var values = Field<object>(host, "_uncontrolledInputValues");
            var runtime = Field<StyleRuntimeInterop>(host, "_nativeStyleRuntime");
            var cache = Field<object>(runtime, "_styles");
            var index = Field<object>(runtime, "_elementIndex");
            var slot = runtime.ReadDomSourceSlot();
            var drag = MakeDrag(a);
            SetField(host, "_scrollbarDrag", drag);
            SetField(host, "_scrollbarPointerCaptured", true);
            component.Label = "B";
            component.Invalidate();
            host.ComponentPublicationProbe = current => { if (current == phase) throw new OutOfMemoryException(phase); };
            Throws<OutOfMemoryException>(host.Render);
            Check(ReferenceEquals(document, host.Document) && ReferenceEquals(dom, Field<object>(host, "_nativeDom")), "Failed staging replaced DOM A.");
            Check(ReferenceEquals(registry, Field<object>(host, "_componentRegistry")), "Failed staging replaced handlers A.");
            Check(ReferenceEquals(styles, Field<object>(host, "_computedStyles")) && ReferenceEquals(transitions, Field<object>(host, "_transitionRuntime")), "Failed staging cleared styles or transitions A.");
            Check(ReferenceEquals(values, Field<object>(host, "_uncontrolledInputValues")) && ReferenceEquals(cache, Field<object>(runtime, "_styles")) && ReferenceEquals(index, Field<object>(runtime, "_elementIndex")), "Failed staging changed caches A.");
            Check(runtime.ReadDomSourceSlot() == slot, "Failed staging changed native source slot.");
            Check(ReferenceEquals(drag, Field<object>(host, "_scrollbarDrag")) && Field<bool>(host, "_scrollbarPointerCaptured"), "Failed staging canceled an existing scrollbar gesture.");
            Check(renderer.Frames == 1 && !host.NeedsRender, "Failure painted or enabled automatic retry.");
            Deliver(registry, a);
            Check(component.Trace.SequenceEqual(new[] { "A" }), "Failed B rerouted reused aether:0.");
            host.ComponentPublicationProbe = null;
            host.Render();
            Check(!ReferenceEquals(document, host.Document), "Explicit retry did not publish B.");
            Check(FieldOrNull(host, "_scrollbarDrag") is null && Field<bool>(host, "_scrollbarPointerCaptured"), "Successful replacement lost capture or retained stale drag.");
        }
    }

    private static void DirectRenderCannotReroutePublishedHost()
    {
        var component = new TestComponent();
        using var renderer = new TestRenderer();
        using var host = New(component, renderer);
        host.Render();
        var document = host.Document;
        var registry = Field<UiComponent.EventRegistry>(host, "_componentRegistry");
        component.Label = "B";
        component.RenderMarkup(); // No invalidation: this is an independent external consumer.
        host.PointerPressed(new(10, 10));
        host.PointerReleased(new(10, 10));
        Check(ReferenceEquals(host.Document, document) && ReferenceEquals(registry, Field<object>(host, "_componentRegistry")), "External RenderMarkup replaced host inputs.");
        Check(component.Trace.SequenceEqual(new[] { "A" }), "Host dispatch followed the global last RenderMarkup.");
        host.Dispose();
        Throws<ObjectDisposedException>(() => host.PointerPressed(new(10, 10)));
        Check(component.Trace.Count == 1, "Disposed host invoked a retained registry.");
    }

    private static void ReentrantBuildCannotPublishB()
    {
        foreach (var throwBuild in new[] { false, true })
        {
            var component = new TestComponent();
            using var renderer = new TestRenderer();
            using var host = New(component, renderer);
            host.Render();
            var document = host.Document;
            var registry = Field<object>(host, "_componentRegistry");
            var effects = 0;
            component.Label = "B";
            component.DuringBuild = () =>
            {
                effects++;
                if (throwBuild) throw new FormatException("Build failed after registering token 0");
                component.Invalidate();
                host.Render(); // Host guard defers; candidate revision is still invalidated.
            };
            component.Invalidate();
            if (throwBuild) Throws<FormatException>(host.Render);
            else Throws<InvalidOperationException>(host.Render);
            Check(ReferenceEquals(document, host.Document) && ReferenceEquals(registry, Field<object>(host, "_componentRegistry")), "Reentrant/throwing Build published B.");
            Check(effects == 1 && renderer.Frames == 1, "Build side effects were replayed or a rejected frame painted.");
        }
    }

    private static void NativeOldDocumentWriteSupersedesCandidate()
    {
        var component = new TestComponent();
        using var renderer = new TestRenderer();
        using var host = New(component, renderer);
        host.Render();
        var document = host.Document!;
        var dom = Field<DomInterop>(host, "_nativeDom");
        var a = document.GetElementById("a")!;
        component.Label = "B";
        component.Invalidate();
        host.ComponentPublicationProbe = phase =>
        {
            if (phase == "ready") dom.Native.SetAttribute(dom.GetNativeHandle(a), "data-native", "newer");
        };
        Throws<InvalidOperationException>(host.Render);
        Check(ReferenceEquals(document, host.Document), "Native A mutation did not supersede replacement.");
        Check(a.GetAttribute("data-native") == "newer", "Rejected replacement rolled back a native side effect.");
    }

    private static void AfterCommitFailureKeepsB()
    {
        var component = new TestComponent();
        using var renderer = new TestRenderer();
        using var host = New(component, renderer);
        host.Render();
        var document = host.Document;
        var oldRegistry = Field<UiComponent.EventRegistry>(host, "_componentRegistry");
        var runtime = Field<StyleRuntimeInterop>(host, "_nativeStyleRuntime");
        var nativeRuntime = Field<IntPtr>(runtime, "_runtime");
        Check(AddSheet(nativeRuntime, "button{height:81px}", 2, "foreign.css", out var foreign, out _) == 0, "Foreign stylesheet setup failed.");
        component.Label = "B";
        component.Invalidate();
        host.ComponentPublicationProbe = phase =>
        {
            if (phase != "published") return;
            Check(!ReferenceEquals(document, host.Document), "Post-commit callback observed A.");
            host.Document!.GetElementById("a")!.SetAttribute("data-after", "kept");
            throw new FormatException("after-commit notification");
        };
        Throws<FormatException>(host.Render);
        Check(!ReferenceEquals(document, host.Document), "Notification failure rolled back B.");
        Check(host.Document!.GetElementById("a")!.GetAttribute("data-after") == "kept", "Notification side effect was overwritten.");
        var nextRegistry = Field<UiComponent.EventRegistry>(host, "_componentRegistry");
        Deliver(nextRegistry, host.Document.GetElementById("a")!);
        Deliver(oldRegistry, document!.GetElementById("a")!);
        Check(component.Trace.SequenceEqual(new[] { "B", "A" }), "Registry ownership changed during retirement.");
        Check(Field<IntPtr>(runtime, "_runtime") == nativeRuntime && NativeMethods.StyleRuntimeRemoveStyleSheet(nativeRuntime, foreign) != 0, "Replacement discarded a foreign sheet/runtime.");
        Check(renderer.Frames == 1, "Failed post-commit work presented an incomplete frame.");
    }

    private static void Deliver(UiComponent.EventRegistry registry, ElementNode target)
    {
        Check(registry.TryGetEventHandler("aether:0", out var handler), "Missing handler token.");
        handler(new UiEvent("click", target));
    }
    private static UiDocumentHost New(TestComponent component, TestRenderer renderer) =>
        new(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions { Width = 200, Height = 200 });
    private static object MakeDrag(ElementNode element)
    {
        var type = typeof(UiDocumentHost).GetNestedType("ScrollbarDrag", BindingFlags.NonPublic)!;
        var axis = typeof(UiDocumentHost).GetNestedType("ScrollAxis", BindingFlags.NonPublic)!;
        return Activator.CreateInstance(type, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
            binder: null, args: [element, Enum.ToObject(axis, 0), 0f], culture: null)!;
    }
    private static object? FieldOrNull(object value, string name) => value.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(value);
    private static T Field<T>(object value, string name) => (T)FieldOrNull(value, name)!;
    private static void SetField(object value, string name, object? field) => value.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(value, field);
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }
    private sealed class TestComponent : UiComponent
    {
        internal string Label = "A";
        internal Action? DuringBuild;
        internal readonly List<string> Trace = [];
        internal void Invalidate() => StateHasChanged();
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            var label = Label;
            writer.WriteLiteral("<style>body{margin:0}button{width:80px;height:80px}</style><body><button id='a'");
            writer.WriteEventAttribute("onclick", () => Trace.Add(label));
            DuringBuild?.Invoke();
            writer.WriteLiteral(">" + label + "</button><input id='field' value='" + label + "'></body>");
        }
    }
    private sealed class TestRenderer : IRenderBackend
    {
        internal int Frames;
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList display, ColorRgba clear) => ++Frames;
        public void Dispose() { }
    }
}
