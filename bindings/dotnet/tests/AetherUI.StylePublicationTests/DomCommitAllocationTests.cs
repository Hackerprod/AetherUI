#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
using System.Reflection;
using System.Runtime.CompilerServices;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

// Allocation measurement of the real public mutation publication intervals.
// Staging, teardown, user notification, component and transition publication are
// not mislabeled as zero-allocation commit. Native allocation is a separate test.
internal static class DomCommitAllocationTests
{
    private static object? _escape;
    private static readonly HashSet<DomCommitAllocationKind> Covered = [];
    internal static void Run()
    {
        Check(typeof(Node).Assembly.GetType("AetherUI.Dom.DomCommitAllocationLedger") is not null, "test ledger missing");
        // Initialize the measuring API, not the product's commit paths.
        _ = GC.GetAllocatedBytesForCurrentThread();
        Fixture? first = null;
        Measure("cold initial import publication", () => first = new Fixture(), DomCommitAllocationKind.Import);
        using (var fixture = first!)
        {
            var node = fixture.Input;
            Measure("cold State commit", () => node.State = ElementState.Hover, DomCommitAllocationKind.State);
            Check(node.State == ElementState.Hover, "cold State was not published");
            Measure("State no-op", () => node.State = ElementState.Hover, DomCommitAllocationKind.State, 0);
            Measure("cold RuntimeValue", () => node.RuntimeValue = "runtime", DomCommitAllocationKind.RuntimeValue);
            Measure("RuntimeValue clear", () => node.RuntimeValue = null, DomCommitAllocationKind.RuntimeValue);
            Measure("cold SetAttribute", () => node.SetAttribute("title", "new"), DomCommitAllocationKind.SetAttribute);
            Measure("cold RemoveAttribute", () => node.RemoveAttribute("title"), DomCommitAllocationKind.RemoveAttribute);
            var text = (TextNode)fixture.Container.Children[0].Children[0];
            Measure("cold text", () => text.TextContent = "replacement", DomCommitAllocationKind.Text);
            var fresh = new ElementNode("span"); fresh.AppendChild(new TextNode("new"));
            Measure("cold append", () => fixture.Container.AppendChild(fresh), DomCommitAllocationKind.InsertChild);
            Measure("cold remove", () => fixture.Container.RemoveChild(fresh), DomCommitAllocationKind.RemoveChild);
            Measure("cold ReplaceText", () => fixture.Container.TextContent = "whole replacement", DomCommitAllocationKind.ReplaceText);
            Measure("cold ClearChildren", fixture.Container.ClearChildren, DomCommitAllocationKind.ClearChildren);
            fixture.Dom.Native.SetAttribute(fixture.Dom.GetNativeHandle(node), "title", "native R2");
            Measure("cold native import publication", () => Check(node.GetAttribute("title") == "native R2", "R2 lost"), DomCommitAllocationKind.Import);
            var session = Field<object>(fixture.Host, "_interactionSession");
            var owner = Field<NativeInteractionOwner>(session, "Owner");
            var binding = fixture.Dom.BindingCore!;
            using var acquire = binding.PrepareInteractionAcquire(owner, node, ElementState.Focus, 123);
            Measure("cold interaction acquire", acquire.Commit, DomCommitAllocationKind.Interaction);
            Check((node.State & ElementState.Focus) != 0, "interaction State not published");
            var claim = acquire.Claim;
            using var cleanup = binding.PrepareInteractionCleanup(node, claim, claim.Read().Generation);
            Measure("interaction cleanup", cleanup.Commit, DomCommitAllocationKind.Interaction);
            Check((node.State & ElementState.Focus) == 0, "interaction cleanup not published");
            claim.Dispose();
        }
        Controls();
        StructuralAndSourceCases();
        FailureBoundaries();
        foreach (var cards in new[] { 100, 500, 1000 }) HoverAfterWarmup(cards);
        Check(Covered.Count == 11, "a publication interval was never exercised");
        Console.WriteLine("PASS managed DOM allocation intervals (9 public mutation paths + import + interaction), GC bytes on calling thread; native/other threads/component/transition publication UNMEASURED here");
    }

    private static void Measure(string name, Action action, DomCommitAllocationKind kind, ulong expected = 1)
    {
        DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.SetEnabled(true);
        var before = GC.GetAllocatedBytesForCurrentThread();
        try { action(); }
        finally { DomCommitAllocationLedger.SetEnabled(false); }
        var total = GC.GetAllocatedBytesForCurrentThread() - before;
        var counts = DomCommitAllocationLedger.Read();
        Check(!counts.Invalid && counts.Successful == expected && counts.Failed == 0 && counts.Count(kind) == expected,
            $"{name}: missing/extra/failed interval successful={counts.Successful}, failed={counts.Failed}, kind={counts.Count(kind)}");
        if (counts.SuccessfulBytes != 0)
            Console.WriteLine($"DOM allocation attribution {name}: nativeValidate={counts.NativeValidationBytes}, bookkeepingValidate={counts.BookkeepingValidationBytes}, nativeCommit={counts.NativeCommitBytes}, managedMaps={counts.ManagedMapsBytes}, bookkeepingPublish={counts.BookkeepingPublishBytes}, scalarMirror={counts.ScalarMirrorBytes}, total={counts.SuccessfulBytes}");
        Check(counts.SuccessfulBytes == 0, $"{name}: commit allocated {counts.SuccessfulBytes} managed bytes (cold allocations also fail)");
        if (expected > 0) Covered.Add(kind);
        Console.WriteLine($"DOM managed {name}: intervals={counts.Successful}, commitBytes={counts.SuccessfulBytes}, wholeOperationBytes={total} (staging/teardown/notification included only in whole operation)");
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void AllocatePositive() => _escape = new byte[257];
    private static void Controls()
    {
        DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.SetEnabled(true);
        DomCommitAllocationLedger.Begin(DomCommitAllocationKind.State); DomCommitAllocationLedger.End(true);
        Check(DomCommitAllocationLedger.Read().SuccessfulBytes == 0, "measurement itself allocates");
        DomCommitAllocationLedger.Reset();
        DomCommitAllocationLedger.Begin(DomCommitAllocationKind.State); AllocatePositive(); DomCommitAllocationLedger.End(true);
        Check(DomCommitAllocationLedger.Read().SuccessfulBytes >= 257, "positive GC allocation not observed");
        DomCommitAllocationLedger.Reset();
        DomCommitAllocationLedger.Begin(DomCommitAllocationKind.State); DomCommitAllocationLedger.Begin(DomCommitAllocationKind.State);
        AllocatePositive(); DomCommitAllocationLedger.End(true); DomCommitAllocationLedger.End(true);
        var nested = DomCommitAllocationLedger.Read();
        Check(!nested.Invalid && nested.Successful == 1 && nested.Nested == 1 && nested.SuccessfulBytes >= 257, "nested scope measurement invalid");
        DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.End(true);
        Check(DomCommitAllocationLedger.Read().Invalid, "unbalanced scope reported valid zero");
        DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.SetEnabled(false);
        DomCommitAllocationLedger.Begin(DomCommitAllocationKind.State); AllocatePositive(); DomCommitAllocationLedger.End(true);
        Check(DomCommitAllocationLedger.Read().Successful == 0, "disabled ledger measured");
        DomCommitAllocationLedger.SetEnabled(true);
        Exception? error = null;
        var thread = new Thread(() =>
        {
            try
            {
                DomCommitAllocationLedger.Begin(DomCommitAllocationKind.State); AllocatePositive(); DomCommitAllocationLedger.End(true);
                Check(DomCommitAllocationLedger.Read().Successful == 0, "worker inherited enabled state");
                DomCommitAllocationLedger.SetEnabled(true);
                DomCommitAllocationLedger.Begin(DomCommitAllocationKind.State); AllocatePositive(); DomCommitAllocationLedger.End(true);
                Check(DomCommitAllocationLedger.Read().SuccessfulBytes >= 257, "worker positive measurement missing");
            }
            catch (Exception failure) { error = failure; }
            finally { DomCommitAllocationLedger.SetEnabled(false); }
        });
        thread.Start(); thread.Join(); DomCommitAllocationLedger.SetEnabled(false);
        if (error is not null) throw error;
        Check(DomCommitAllocationLedger.Read().Successful == 0, "worker modified caller ledger");
        _escape = null;
    }

    private static void StructuralAndSourceCases()
    {
        using var source = new Fixture(); using var destination = new Fixture();
        var child = source.Container.Children[0];
        Measure("cross-document adoption", () => destination.Container.AppendChild(child), DomCommitAllocationKind.InsertChild);
        Check(ReferenceEquals(child.OwnerDocument, destination.Host.Document), "adoption owner wrong");
        var detached = new ElementNode("section"); detached.AppendChild(new TextNode("prefix"));
        Measure("materialize forest", () => detached.AppendChild(child), DomCommitAllocationKind.InsertChild);
        Check(child.OwnerDocument is null && ReferenceEquals(child.Parent, detached), "forest links wrong");
        var document = new DocumentNode();
        Measure("materialize implicit document", () => document.AppendChild(detached), DomCommitAllocationKind.InsertChild);
        Check(ReferenceEquals(child.OwnerDocument, document), "implicit document owner wrong");
        var style = source.Host.Document!.GetElementById("s")!;
        Measure("style source text/slot publication", () => ((TextNode)style.Children[0]).TextContent = "#a{width:23px}", DomCommitAllocationKind.Text);
        // A cold/first path is already tested. Repeating cases is not an excuse to
        // discard any earlier allocation sample.
        Measure("warm State", () => source.Input.State = ElementState.Invalid, DomCommitAllocationKind.State);
    }

    private static void FailureBoundaries()
    {
        using var fixture = new Fixture(); var node = fixture.Input;
        var baseToken = NativeDomSync.ReadToken(fixture.Dom.Native);
        fixture.Host.DomSynchronizationProbe = () => throw new OutOfMemoryException("staging allocation failure");
        DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.SetEnabled(true);
        var before = GC.GetAllocatedBytesForCurrentThread(); var stagingFailed = false;
        try { node.State = ElementState.Hover; }
        catch (OutOfMemoryException) { stagingFailed = true; }
        finally { DomCommitAllocationLedger.SetEnabled(false); fixture.Host.DomSynchronizationProbe = null; }
        var stageBytes = GC.GetAllocatedBytesForCurrentThread() - before;
        var stage = DomCommitAllocationLedger.Read();
        Check(stagingFailed && !stage.Invalid && stage.Successful == 0 && stage.Failed == 0, "staging failure entered commit");
        Check(NativeDomSync.ReadToken(fixture.Dom.Native) == baseToken && node.State == ElementState.None, "staging failure wrote inputs");
        Console.WriteLine($"DOM managed staging failure: commitIntervals=0, wholeOperationBytes={stageBytes}; not a zero-commit success");

        var handle = fixture.Dom.GetNativeHandle(node);
        fixture.Host.DomSynchronizationProbe = () => fixture.Dom.Native.SetState(handle, (int)ElementState.Invalid);
        DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.SetEnabled(true); var commitFailed = false;
        try { node.State = ElementState.Hover; }
        catch (InvalidOperationException) { commitFailed = true; }
        finally { DomCommitAllocationLedger.SetEnabled(false); fixture.Host.DomSynchronizationProbe = null; }
        var failed = DomCommitAllocationLedger.Read();
        Check(commitFailed && !failed.Invalid && failed.Successful == 0 && failed.Failed == 1, "stale commit not reported as failure");
        Check(node.State == ElementState.Invalid, "rejected commit overwrote native R2");
        Console.WriteLine($"DOM managed rejected commit: failedIntervals={failed.Failed}, failedBytes={failed.FailedBytes}; not asserted zero and never counted as successful");

        var listenerError = new ApplicationException("post-commit notification");
        EventHandler listener = (_, _) => { AllocatePositive(); throw listenerError; };
        fixture.Host.RenderRequested += listener;
        DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.SetEnabled(true); var notificationFailed = false;
        try { node.State = ElementState.Hover; }
        catch (ApplicationException error) when (ReferenceEquals(error, listenerError)) { notificationFailed = true; }
        finally { DomCommitAllocationLedger.SetEnabled(false); fixture.Host.RenderRequested -= listener; }
        var after = DomCommitAllocationLedger.Read();
        Check(notificationFailed && !after.Invalid && after.Successful == 1 && after.Failed == 0 && after.SuccessfulBytes == 0,
            "notification was included in commit or called rollback");
        Check(node.State == ElementState.Hover, "notification failure reverted committed input");
    }

    private static void HoverAfterWarmup(int cards)
    {
        using var fixture = new Fixture(cards); var node = fixture.Input;
        node.State = ElementState.Hover; fixture.Host.Render(); node.State = ElementState.None; fixture.Host.Render();
        Measure($"warm hover {cards} cards", () =>
        {
            for (var i = 0; i < 4; i++) { node.State = ElementState.Hover; fixture.Host.Render(); node.State = ElementState.None; fixture.Host.Render(); }
        }, DomCommitAllocationKind.State, 8);
        Check(!fixture.Host.ManagedProjectionOutOfDate, "hover left stale projection");
    }

    private sealed class Fixture : IDisposable
    {
        private readonly Renderer _renderer = new();
        internal readonly UiDocumentHost Host;
        internal readonly DomInterop Dom;
        internal readonly ElementNode Input, Container;
        internal Fixture(int cards = 2)
        {
            Host = new(new Cards(cards), new ApproximateTextMeasurer(), _renderer, new UiHostOptions { Width = 400, Height = 300 });
            Host.Render(); Dom = Field<DomInterop>(Host, "_nativeDom");
            Input = Host.Document!.GetElementById("a")!; Container = Host.Document.GetElementById("box")!;
        }
        public void Dispose() { Host.Dispose(); _renderer.Dispose(); }
    }
    private sealed class Cards(int count) : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style id='s'>body{margin:0}article{height:20px}</style><body><input id='a'><div id='box'><span>text</span></div>");
            for (var i = 0; i < count; i++) writer.WriteLiteral($"<article id='card-{i}'>card</article>");
            writer.WriteLiteral("</body>");
        }
    }
    private sealed class Renderer : IRenderBackend
    {
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList display, ColorRgba color) { }
        public void Dispose() { }
    }
    private static T Field<T>(object owner, string name) => (T)owner.GetType().GetField(name, BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(owner)!;
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException("DOM commit allocation gate: " + message); }
}
#endif
