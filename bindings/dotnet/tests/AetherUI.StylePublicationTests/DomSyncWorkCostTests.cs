using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

// Structural work gate, not a CPU benchmark. Chromium reference:
// 2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9, element.cc:13552,
// Element::SetHovered changes user-action state and declares local invalidation;
// it does not rebuild/import the DOM. This test exercises Aether's own bridge.
internal static class DomSyncWorkCostTests
{
    internal static void Run()
    {
        Check(Marshal.SizeOf<NativeWorkCountersSnapshot>() == 13 * sizeof(ulong), "original ABI layout changed");
        Check(Marshal.SizeOf<NativeDomWorkCountersSnapshot>() == 22 * sizeof(ulong), "DOM counter ABI layout mismatch");
        if (!NativeDomWorkCounters.TryEnableForCurrentThread())
        {
            Console.WriteLine("SKIP DOMsync work-cost gate: native diagnostic counters unavailable (build with AETHERUI_ENABLE_WORK_COUNTERS=ON)");
            return;
        }
        try
        {
            DiagnosticControls();
            PositiveDomWorkControls();
            PositiveTransitionWorkControls();
            foreach (var cards in new[] { 100, 500, 1000 }) HoverStateAfterWarmup(cards);
            Console.WriteLine("PASS DOMsync snapshot/import/managed-call work gates; commit allocations UNMEASURED; no CPU or Windows performance claim");
        }
        finally
        {
            NativeDomWorkCounters.DisableForCurrentThread();
            NativeDomWorkCounters.ResetForCurrentThread();
        }
    }

    private static void DiagnosticControls()
    {
        NativeDomWorkCounters.DisableForCurrentThread();
        NativeDomWorkCounters.ResetForCurrentThread();
        using (var document = NativeDomDocument.Parse("<div></div>")) NativeDomSync.ReadToken(document);
        NativeDomWorkCounters.RecordNodesImported(17);
        var disabled = Read();
        Check(disabled.Available == 1 && disabled.Enabled == 0, "disabled session metadata");
        Check(disabled.DomTokenReads == 0 && disabled.ManagedDomParseCalls == 0 && disabled.DomNodesImported == 0,
            "reset enabled collection or disabled hooks counted work");
        Check(!disabled.HasCommitAllocationMeasurement, "allocation coverage must remain explicitly unavailable without an observer");
        Check(NativeMethods.DomWorkCountersRecordManaged(-1, 1) != 0, "invalid managed counter kind accepted");
        Check(NativeDomWorkCounters.TryEnableForCurrentThread(), "diagnostic session could not reenable");
        NativeDomWorkCounters.RecordNodesImported(ulong.MaxValue);
        NativeDomWorkCounters.RecordNodesImported(1);
        Check(Read().DomNodesImported == ulong.MaxValue, "counter wrapped instead of saturating");
        NativeDomWorkCounters.ResetForCurrentThread();
        Check(Read().Enabled == 1 && Read().DomNodesImported == 0, "reset changed enable state");
        Exception? workerFailure = null;
        var thread = new Thread(() =>
        {
            try
            {
                Check(Read().Enabled == 0 && Read().DomNodesImported == 0, "new thread inherited diagnostics");
                NativeDomWorkCounters.RecordNodesImported(7);
                Check(Read().DomNodesImported == 0, "managed hooks enabled on a new thread");
                Check(NativeDomWorkCounters.TryEnableForCurrentThread(), "worker enable failed");
                NativeDomWorkCounters.RecordNodesImported(3);
                Check(Read().DomNodesImported == 3, "worker did not collect its own work");
            }
            catch (Exception error) { workerFailure = error; }
            finally { NativeDomWorkCounters.DisableForCurrentThread(); }
        });
        thread.Start(); thread.Join();
        if (workerFailure is not null) throw workerFailure;
        Check(Read().Enabled == 1 && Read().DomNodesImported == 0, "worker changed caller counters");
    }

    private static void PositiveDomWorkControls()
    {
        NativeDomWorkCounters.ResetForCurrentThread();
        using var dom = DomInterop.Parse("<div id='a' title='first'><span>payload</span></div>");
        dom.EnableSynchronization(new Bookkeeping());
        var element = dom.Document.GetElementById("a")!;
        var initial = Read();
        Check(initial.ManagedDomParseCalls == 1 && initial.DomSnapshotBuilds > 0 && initial.DomNodesImported > 0,
            "real parse/snapshot/import did not increment counters");
        Check(initial.DomSnapshotNodesVisited > 0 && initial.DomSnapshotAttributesVisited > 0 && initial.DomSnapshotChildrenVisited > 0,
            "native snapshot traversal hooks did not observe actual payload");
        Check(initial.ManagedDomAttributesVisited > 0 && initial.ManagedDomChildrenVisited > 0 && initial.DomImportInitialBinding == 1,
            "managed traversal/initial-binding hooks did not observe import");
        NativeDomWorkCounters.ResetForCurrentThread();
        dom.Native.SetAttribute(dom.GetNativeHandle(element), "title", "native R2");
        Check(element.GetAttribute("title") == "native R2", "positive R2 import failed");
        var imported = Read();
        Check(imported.DomImportTokenChanged == 1 && imported.DomSnapshotBuilds > 0 && imported.DomNodesImported > 0,
            "token-mismatch fallback did not record actual import");
        NativeDomWorkCounters.ResetForCurrentThread();
        element.State = ElementState.Hover;
        using (var abandoned = NativeDomEdit.Prepare(NativeDomSync.ReadToken(dom.Native),
            new(NativeDomEditKind.SetState, dom.GetNativeHandle(element), State: 0))) { }
        var edits = Read();
        Check(edits.DomEditPrepareAttempts == 2 && edits.DomEditsPrepared == 2 && edits.DomEditsCommitted == 1 && edits.DomEditsAborted == 1,
            "committed and abandoned edits were not measured separately");
        Check(element.State == ElementState.Hover, "abandoned edit published its input");
        NativeDomWorkCounters.ResetForCurrentThread();
        var rejected = false;
        try
        {
            using var invalid = NativeDomEdit.Prepare(NativeDomSync.ReadToken(dom.Native),
                new((NativeDomEditKind)999, dom.GetNativeHandle(element)));
        }
        catch (InvalidOperationException) { rejected = true; }
        var failed = Read();
        Check(rejected && failed.DomEditPrepareAttempts == 1 && failed.DomEditsPrepared == 0 &&
            failed.DomEditsCommitted == 0 && failed.DomEditsAborted == 1, "failed staging was not recorded as aborted work");

        using var renderer = new Renderer();
        var component = new Cards(2);
        using var host = New(component, renderer);
        host.Render();
        NativeDomWorkCounters.ResetForCurrentThread();
        component.Rebuild(); host.Render();
        Check(Read().ManagedDomParseCalls == 1 && Read().ManagedDomRebindCalls == 1,
            "actual component replacement did not count managed parse/rebind calls");
    }

    private static void PositiveTransitionWorkControls()
    {
        using var document = NativeDomDocument.Parse("<div></div>");
        var node = document.EnumerateElementHandles().Last();
        using var runtime = new TransitionRuntimeInterop();
        var before = new ComputedStyle { Opacity = 1 };
        var after = new ComputedStyle { Opacity = 0, TransitionProperties = 1, TransitionDuration = 1 };
        using var observation = TransitionStyleMarshaler.Prepare(node, before, after);
        NativeDomWorkCounters.ResetForCurrentThread();
        using (var candidate = TransitionStyleMarshaler.PrepareBatch(runtime, [observation], [], 100))
            Check(candidate.TryCommit(out _) == 0, "positive transition candidate failed");
        Check(Read().TransitionCandidatesCloned == 1, "real transition storage clone not counted");
        NativeDomWorkCounters.ResetForCurrentThread();
        Check(runtime.TryTick(600, out var active) == 0 && active, "positive transition tick failed");
        Check(Read().TransitionCandidatesCloned == 1 && Read().TransitionEntriesCopied == 1,
            "active transition vector copy not counted");
        NativeDomWorkCounters.ResetForCurrentThread();
        using (var candidate = runtime.PreparePublication([], [], 600))
            Check(candidate.TryCommit(out _) == 0, "no-op transition candidate failed");
        Check(Read().TransitionCandidatesCloned == 0 && Read().TransitionEntriesCopied == 0,
            "no-op candidate cloned active transition storage");
    }

    private static void HoverStateAfterWarmup(int cards)
    {
        using var renderer = new Renderer();
        using var host = New(new Cards(cards), renderer);
        host.Render();
        var first = host.Document!.GetElementById("card-0")!;
        var second = host.Document.GetElementById("card-1")!;
        var dom = Field<DomInterop>(host, "_nativeDom");
        var firstHandle = dom.GetNativeHandle(first);
        var documentHandle = dom.Native.Handle;
        // No hover rules or transitions, with all nodes/attributes materialized first.
        first.State |= ElementState.Hover; host.Render();
        first.State &= ~ElementState.Hover; host.Render();
        NativeDomWorkCounters.ResetForCurrentThread();
        for (var iteration = 0; iteration < 2; iteration++)
        {
            first.State |= ElementState.Hover; host.Render();
            first.State &= ~ElementState.Hover; host.Render();
            second.State |= ElementState.Hover; host.Render();
            second.State &= ~ElementState.Hover; host.Render();
        }
        var work = Read(); // Stop measuring before verification traverses the document.
        Check(work.DomTokenReads > 0, "warm path bypassed its current-input certificate");
        Check(work.DomEditPrepareAttempts == 8 && work.DomEditsPrepared == 8 && work.DomEditsCommitted == 8 && work.DomEditsAborted == 0,
            "hover State did not use exactly its eight typed edits");
        Check(work.DomSnapshotBuilds == 0 && work.DomNodesImported == 0, "warm hover rebuilt or imported the DOM");
        Check(work.ManagedDomParseCalls == 0 && work.ManagedDomRebindCalls == 0, "warm hover reparsed or rebound the managed DOM");
        Check(work.DomSnapshotNodesVisited == 0 && work.DomSnapshotAttributesVisited == 0 && work.DomSnapshotChildrenVisited == 0 &&
            work.ManagedDomAttributesVisited == 0 && work.ManagedDomChildrenVisited == 0,
            "warm hover scanned snapshot/import attributes or children");
        Check(work.TransitionCandidatesCloned == 0 && work.TransitionEntriesCopied == 0, "no-op hover cloned transition storage");
        Check(work.DomImportInitialBinding == 0 && work.DomImportTokenChanged == 0, "warm hover entered an import fallback");
        Check(ReferenceEquals(first, host.Document.GetElementById("card-0")) && dom.GetNativeHandle(first) == firstHandle && dom.Native.Handle == documentHandle,
            "warm hover replaced an existing wrapper or native document");
        Check(!host.ManagedProjectionOutOfDate && first.State == ElementState.None && second.State == ElementState.None,
            "warm hover left stale style projection or state");
        Console.WriteLine($"DOMsync cards={cards} edits={work.DomEditsCommitted} tokenReads={work.DomTokenReads} snapshots={work.DomSnapshotBuilds} imports={work.DomNodesImported} managedParse={work.ManagedDomParseCalls} managedRebind={work.ManagedDomRebindCalls} transitionClones={work.TransitionCandidatesCloned} transitionCopies={work.TransitionEntriesCopied} fallbackInitial={work.DomImportInitialBinding} fallbackToken={work.DomImportTokenChanged} commitAllocations=UNMEASURED");
    }

    private static NativeDomWorkCountersSnapshot Read() => NativeDomWorkCounters.ReadForCurrentThread();
    private static UiDocumentHost New(Cards component, Renderer renderer) =>
        new(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions { Width = 400, Height = 300 });
    private static T Field<T>(object owner, string name) =>
        (T)owner.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(owner)!;
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException("DOMsync work gate: " + message);
    }
    private sealed class Cards(int count) : UiComponent
    {
        internal void Rebuild() => StateHasChanged();
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style>body{margin:0}article{display:block;height:20px}span{color:black}</style><body>");
            for (var i = 0; i < count; i++)
                writer.WriteLiteral($"<article id='card-{i}' title='card'><span>payload</span></article>");
            writer.WriteLiteral("</body>");
        }
    }
    private sealed class Renderer : IRenderBackend
    {
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList display, ColorRgba color) { }
        public void Dispose() { }
    }
    private sealed class Bookkeeping : IDomHostBookkeeping
    {
        public IDomPreparedBookkeeping Prepare(DomEdit? edit, bool import) => new Prepared();
        public void NotifyMutationCompleted() { }
        private sealed class Prepared : IDomPreparedBookkeeping
        {
            public void Validate() { }
            public void Publish() { }
            public void Dispose() { }
        }
    }
}
