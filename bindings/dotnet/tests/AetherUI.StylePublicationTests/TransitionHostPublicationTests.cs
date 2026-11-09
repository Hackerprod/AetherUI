using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

internal static class TransitionHostPublicationTests
{
    [DllImport("aether_core", EntryPoint = "aether_transition_test_fail_after", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FailAfter(long allocations);

    internal static void Run()
    {
        NativeDirectTickDetected();
        PrepareFailureNotAcknowledgedByTick();
        NativeCallbackFailure(false);
        NativeCallbackFailure(true);
        ReplayTick(false);
        ReplayTick(true);
        DerivedNotificationTick();
        LayoutCallbackTick();
        DisposeDuringReplay();
        Console.WriteLine("PASS transition host native/replay error, no-ABA, acknowledgment and derived gates");
    }

    private static void NativeDirectTickDetected()
    {
        using var renderer = new TestRenderer();
        using var host = New(renderer);
        host.Render();
        Check(!host.NeedsRender, "clean initial projection");
        var runtime = Field<TransitionRuntimeInterop>(host, "_transitionRuntime");
        Seed(host, runtime);
        Check(host.NeedsRender, "NeedsRender detects direct native Observe without a DOM getter");
        host.Render();
        Check(runtime.TryTick(long.MaxValue, out var active) == 0 && !active, "direct final native tick");
        Check(host.NeedsRender, "NeedsRender detects final tick even when HasActive is false");
        Check(host.CursorAt(new(10, 10)) == CursorKind.Default, "native tick invalidates cached hit");
        host.Render();
        Check(!host.NeedsRender && renderer.Frames == 3, "direct native ticks reproject without restoration");
    }

    private static void PrepareFailureNotAcknowledgedByTick()
    {
        using var renderer = new TestRenderer();
        using var host = New(renderer);
        host.Render();
        var runtime = Field<TransitionRuntimeInterop>(host, "_transitionRuntime");
        Seed(host, runtime);
        State(host);
        host.StylePublicationProbe = phase => { if (phase == "native-preview") FailAfter(0); };
        try { Throws<OutOfMemoryException>(host.Render); }
        finally { FailAfter(-1); host.StylePublicationProbe = null; }
        Check(renderer.Frames == 1 && runtime.ReadToken().TickError == 0, "prepare error is not a TickError");
        host.TickTransitions(500);
        Check(!host.NeedsRender, "successful Tick does not clear unrelated prepare failure retry gate");
        host.Render();
        Check(renderer.Frames == 2, "explicit render retries pending prepare");
    }

    private static void NativeCallbackFailure(bool acknowledgeInsideCallback)
    {
        using var renderer = new TestRenderer();
        using var host = New(renderer, "transform:var(--transition-undefined)");
        host.Render();
        var runtime = Field<TransitionRuntimeInterop>(host, "_transitionRuntime");
        Seed(host, runtime);
        var publication = host.HostPublicationSerial;
        var called = false;
        StyleDiagnosticCallback callback = (_, _, _, _, _) =>
        {
            if (called) return;
            called = true;
            try { FailAfter(0); Check(NativeMethods.TransitionRuntimeTick(runtime.Handle, 1000) != 0, "legacy failure preserves activity"); }
            finally { FailAfter(-1); }
            Check(host.HostPublicationSerial == publication, "native callback occurs before managed publication");
            if (acknowledgeInsideCallback) Check(runtime.TryTick(500, out _) == 0, "explicit callback acknowledgment");
        };
        Native.SetDiagnostics(Field<IntPtr>(Field<StyleRuntimeInterop>(host, "_nativeStyleRuntime"), "_runtime"), callback, IntPtr.Zero);
        State(host);
        if (acknowledgeInsideCallback)
        {
            host.Render();
            Check(host.HostPublicationSerial == publication + 1, "failure-success rejected first candidate without first managed swap");
            Check(renderer.Frames == 2, "only recovered frame presented");
        }
        else
        {
            Throws<OutOfMemoryException>(host.Render);
            Check(host.HostPublicationSerial == publication && renderer.Frames == 1, "Tick OOM before publish causes zero managed swaps/frames");
            Check(!host.NeedsRender && host.ManagedProjectionOutOfDate, "OOM preserves dirty but disables automatic retry");
            Check(host.CursorAt(new(10, 10)) == CursorKind.Default, "TickError closes hit authority");
            Check(host.TickTransitions(500), "explicit valid sample acknowledges current error");
            host.Render();
            Check(renderer.Frames == 2, "explicit recovery presents current output");
        }
        Check(called, "native diagnostic callback ran");
        GC.KeepAlive(callback);
    }

    private static void ReplayTick(bool fail)
    {
        using var renderer = new TestRenderer();
        using var host = New(renderer);
        host.Render();
        var runtime = Field<TransitionRuntimeInterop>(host, "_transitionRuntime");
        var property = "transition-replay-" + Guid.NewGuid().ToString("N");
        var called = false;
        host.StylePublicationProbe = phase =>
        {
            if (phase == "native-preview" && !called) CssDiagnostics.ReportUnsupportedValue(property, "sample");
        };
        Action<CssStyleDiagnostic> listener = d =>
        {
            if (d.Property != property || called) return;
            called = true;
            Check(host.HostPublicationSerial == 2 && runtime.HasActive, "listener sees R1 maps and transitions together");
            var styles = Field<IReadOnlyDictionary<ElementNode, ComputedStyle>>(host, "_computedStyles");
            Check(styles[host.Document!.GetElementById("a")!].Opacity == .5f, "listener sees new style opacity");
            if (!fail) { Check(!host.TickTransitions(long.MaxValue), "replay tick completes R1 transition"); return; }
            try { FailAfter(0); Check(NativeMethods.TransitionRuntimeTick(runtime.Handle, long.MaxValue) != 0, "replay OOM legacy activity"); }
            finally { FailAfter(-1); }
            var node = Field<DomInterop>(host, "_nativeDom").GetNativeHandle(host.Document!.GetElementById("a")!);
            var unchanged = new ComputedStyle { Opacity = .5f, TransitionProperties = 1, TransitionDuration = 1 };
            TransitionStyleMarshaler.Observe(runtime, node, unchanged, unchanged, 0);
            Check(runtime.ReadToken().TickError == -3, "no-op Observe does not acknowledge replay TickError");
        };
        CssDiagnostics.Reported += listener;
        try
        {
            State(host);
            if (fail)
            {
                Throws<OutOfMemoryException>(host.Render);
                Check(host.HostPublicationSerial == 2 && renderer.Frames == 1, "R1 remains published after replay OOM; no frame");
                Check(host.ManagedProjectionOutOfDate && !host.NeedsRender, "error blocks presentation and automatic recovery");
                Check(!host.TickTransitions(long.MaxValue), "explicit full sample clears error even on final tick");
                host.Render();
            }
            else host.Render();
            Check(called && renderer.Frames == 2, "one current frame after replay recovery");
            Check(!runtime.HasActive, "recovery did not reinstall transition after newer tick");
            var node = Field<DomInterop>(host, "_nativeDom").GetNativeHandle(host.Document!.GetElementById("a")!);
            Check(runtime.TryGetOpacityOverride(node, out var opacity) && opacity == .5f, "newer finished sample survives recovery");
        }
        finally { CssDiagnostics.Reported -= listener; }
    }

    private static void DerivedNotificationTick()
    {
        using var renderer = new TestRenderer();
        using var host = New(renderer);
        host.Render(); State(host); host.Render();
        var frames = renderer.Frames;
        var once = false;
        renderer.OnComposition = () =>
        {
            if (once) return;
            once = true;
            host.TickTransitions(long.MaxValue);
        };
        Throws<InvalidOperationException>(host.Render);
        Check(once && renderer.Frames == frames, "tick in renderer notification invalidates already built display list");
        Check(host.CursorAt(new(10, 10)) == CursorKind.Default, "derived mismatch denies hit");
        renderer.OnComposition = null;
        host.Render();
        Check(renderer.Frames == frames + 1, "derived recovery presents new sample");
    }

    private static void LayoutCallbackTick()
    {
        using var renderer = new TestRenderer();
        var measurer = new CallbackMeasurer();
        using var host = new UiDocumentHost(new TestComponent(""), measurer, renderer,
            new UiHostOptions { Width = 200, Height = 200 });
        host.Render(); State(host); host.Render();
        var frames = renderer.Frames;
        var called = false;
        measurer.OnMeasure = () =>
        {
            if (called) return;
            called = true;
            host.TickTransitions(long.MaxValue);
        };
        host.SetViewport(210, 200, 1);
        Throws<InvalidOperationException>(host.Render);
        Check(called && renderer.Frames == frames, "layout callback tick rejects derived output without new frame");
        Check(host.ManagedProjectionOutOfDate && host.CursorAt(new(10, 10)) == CursorKind.Default,
            "layout callback closes hit gate");
        measurer.OnMeasure = null;
        host.Render();
        Check(renderer.Frames == frames + 1, "layout recovers from current sample");
    }

    private static void DisposeDuringReplay()
    {
        using var renderer = new TestRenderer();
        using var host = New(renderer);
        host.Render();
        var property = "transition-dispose-" + Guid.NewGuid().ToString("N");
        host.StylePublicationProbe = phase => { if (phase == "native-preview") CssDiagnostics.ReportUnsupportedValue(property, "dispose"); };
        Action<CssStyleDiagnostic> listener = d => { if (d.Property == property) host.Dispose(); };
        CssDiagnostics.Reported += listener;
        try { State(host); Throws<ObjectDisposedException>(host.Render); Check(renderer.Frames == 1, "dispose during replay presents nothing"); }
        finally { CssDiagnostics.Reported -= listener; }
    }

    private static void Seed(UiDocumentHost host, TransitionRuntimeInterop runtime)
    {
        var node = Field<DomInterop>(host, "_nativeDom").GetNativeHandle(host.Document!.GetElementById("a")!);
        TransitionStyleMarshaler.Observe(runtime, node, new ComputedStyle { Opacity = 1 },
            new ComputedStyle { Opacity = 0, TransitionProperties = 1, TransitionDuration = 1 }, 0);
    }
    private static UiDocumentHost New(TestRenderer renderer, string extra = "") =>
        new(new TestComponent(extra), new ApproximateTextMeasurer(), renderer, new UiHostOptions { Width = 200, Height = 200 });
    private static T Field<T>(object target, string name) =>
        (T)target.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(target)!;
    private static void State(UiDocumentHost host)
    {
        var element = host.Document!.GetElementById("a")!;
        typeof(UiDocumentHost).GetMethod("SetElementState", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(host, [element, ElementState.Invalid, true]);
        typeof(UiDocumentHost).GetMethod("InvalidateVisual", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(host, [false]);
    }
    private static void Check(bool condition, string message)
    { if (!condition) throw new InvalidOperationException("Transition host: " + message); }
    private static void Throws<T>(Action action) where T : Exception
    { try { action(); } catch (T) { return; } throw new InvalidOperationException("Expected " + typeof(T).Name); }

    private sealed class TestComponent(string extra) : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(
            "<style>body{margin:0}#a{width:80px;height:80px;opacity:1;transition:opacity 1s linear;cursor:pointer;" + extra +
            "}#a:invalid{opacity:.5}</style><body><button id='a' type='button'>sample</button></body>");
    }
    private sealed class CallbackMeasurer : ITextMeasurer
    {
        private readonly ApproximateTextMeasurer _inner = new();
        internal Action? OnMeasure;
        public TextMetrics MeasureText(string text, ComputedStyle style, float maxWidth = float.PositiveInfinity)
        {
            OnMeasure?.Invoke();
            return _inner.MeasureText(text, style, maxWidth);
        }
    }
    private sealed class TestRenderer : IRenderBackend
    {
        internal int Frames;
        internal Action? OnComposition;
        public void Resize(int width, int height, float dpi) { }
        public void SetScrollCompositionMode(bool enabled) => OnComposition?.Invoke();
        public void Render(DisplayList displayList, ColorRgba clearColor) => ++Frames;
        public void Dispose() { }
    }
}
