using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

// Every mode is a separate process. Product imports are linked by real preparation,
// never by calling a query, rewriting readiness or resetting the runtime's cache.
internal static class ColdInteractionQueryTests
{
    internal static void Run(string mode)
    {
        var resolver = new Resolver();
        Check(resolver.State == 0, "query group was not cold at entry");
        if (mode == "public") PublicQuery(resolver);
        else if (mode is "read" or "current" or "pending" or "loss") DirectQuery(resolver, mode);
        else if (mode.StartsWith("owner-", StringComparison.Ordinal)) OwnerPreparation(resolver, mode[6..]);
        else if (mode.StartsWith("binding-", StringComparison.Ordinal)) BindingPreparation(resolver, mode[8..]);
        else if (mode.StartsWith("host-", StringComparison.Ordinal)) HostPreparation(resolver, mode[5..]);
        #if AETHER_DOM_FAULT_TESTING
        else if (mode.StartsWith("fault-", StringComparison.Ordinal)) FaultPreparation(resolver, mode[6..]);
#endif
        else throw new ArgumentException("Unknown cold query mode: " + mode);
    }

    private static void PublicQuery(Resolver resolver)
    {
        using var renderer = new Renderer();
        using var host = NewHost(new Component(), renderer);
        host.Render(); host.PointerMoved(new(10, 10)); host.Render();
        Check(resolver.State == 2 && resolver.QueryCalls == 2, "public preparation did not resolve both original query imports");
        var session = Field<object>(host, "_interactionSession");
        var claims = (System.Collections.IList)Field<object>(session, "Claims");
        Check(claims.Count > 0 && !Field<bool>(host, "_hasPendingPointerMove") &&
            !Field<bool>(host, "_renderDeferred") && !Field<bool>(host, "_markupDirty") &&
            Convert.ToByte(Field<object>(host, "_invalidation")) == 0, "public route did not leave clean hover claims");
        Measure(resolver, () => !host.NeedsRender, "first public NeedsRender");
        var claim = Field<NativeInteractionClaim>(claims[0]!, "Native");
        Check(claim.IsCurrent, "public route never created live native authority");
        var dom = Field<DomInterop>(host, "_nativeDom");
        var button = host.Document!.GetElementById("a")!;
        var handle = dom.GetNativeHandle(button); var parent = dom.GetNativeHandleCore(button.Parent!);
        Check(NativeMethods.NodeRemoveChild(parent, handle) == 0, "native disconnect failed");
        Check(host.NeedsRender && claim.HasPendingLoss, "query result did not observe actual native loss");
        var claimCount = claims.Count;
        host.Render(); // Reconcile the intentional native loss before normal host teardown.
        Console.WriteLine($"PASS cold query public: prepared=2; claims={claimCount}; first/repeated getterBytes=0; resolverCallsInGetter=0; native-loss control=true");
    }

    private static void DirectQuery(Resolver resolver, string mode)
    {
        using var document = NativeDomDocument.Parse("<body><button id='a'>button</button></body>");
        var node = document.EnumerateElementHandles().Last();
        document.SetState(node, (int)ElementState.Hover);
        using var runtime = new StyleRuntimeInterop(200, 200, 1);
        using var owner = NativeInteractionOwner.Prepare(document, 701);
        using var claim = owner.PrepareClaim(node, (int)ElementState.Hover, 702);
        using (var stage = runtime.PrepareDomBinding(document)) stage.Commit();
        owner.Activate(runtime.DomStyleBindingHandle);
        Check(resolver.State == 2 && resolver.QueryCalls == 2, "owner preparation did not resolve both original query imports");
        Func<bool> query = mode switch
        {
            "read" => () => { var info = claim.Read(); return info.Node == node && info.Current != 0 && info.RegistryIdentity == 702; },
            "current" => () => claim.IsCurrent,
            "pending" => () => !claim.HasPendingLoss,
            "loss" => () => !claim.TryReadLoss(out _),
            _ => throw new ArgumentException(mode),
        };
        Measure(resolver, query, "first direct " + mode);
        var parent = document.Body;
        Check(NativeMethods.NodeRemoveChild(parent, node) == 0, "direct native disconnect failed");
        Check(!claim.IsCurrent && claim.Read().Current == 0 && claim.HasPendingLoss && claim.TryReadLoss(out var loss) &&
            loss.Claim.Node == node, "native loss queries did not read actual state");
        claim.Dispose();
        var calls = resolver.Calls;
        Check(!claim.IsCurrent && !claim.HasPendingLoss, "closed claim boolean guard changed");
        Expect<ObjectDisposedException>(() => claim.Read());
        Expect<ObjectDisposedException>(() => claim.TryReadLoss(out _));
        Check(resolver.Calls == calls, "closed SafeHandle query resolved an import");
        owner.Retire();
        Console.WriteLine($"PASS cold query {mode}: prepared=2; first/repeated queryBytes=0; resolverCallsInQuery=0; native-loss and closed-SafeHandle controls=true");
    }

    private static void OwnerPreparation(Resolver resolver, string mode)
    {
        Check(mode is "retry" or "dispose" or "reentry" or "concurrent", "unknown owner mode");
        using var document = NativeDomDocument.Parse("<body><button>button</button></body>");
        var token = NativeDomSync.ReadToken(document);
        Check(NativeDomWorkCounters.TryEnableForCurrentThread(), "native counters unavailable");
        NativeDomWorkCounters.ResetForCurrentThread();
        var failure = new OutOfMemoryException("query second-import resolver failure");
        var reentered = false;
        resolver.OnQuery = ordinal =>
        {
            if (mode == "retry" && ordinal == 2) throw failure;
            if (ordinal != 1) return;
            if (mode == "dispose") document.Dispose();
            if (mode == "reentry")
            {
                Expect<InvalidOperationException>(() => NativeInteractionOwner.Prepare(document, 704));
                reentered = resolver.State == 1;
            }
            if (mode == "concurrent")
            {
                Exception? error = null;
                var worker = new Thread(() =>
                {
                    try { NativeMethods.InteractionQueryImports.EnsurePrepared(); }
                    catch (Exception caught) { error = caught; }
                }) { IsBackground = true };
                worker.Start();
                Check(worker.Join(TimeSpan.FromSeconds(10)), "concurrent preparation waited/deadlocked");
                reentered = error is InvalidOperationException && resolver.State == 1;
            }
        };
        NativeInteractionOwner? prepared = null;
        try
        {
            if (mode == "retry")
            {
                try { prepared = NativeInteractionOwner.Prepare(document, 703); throw new Exception("expected OOM"); }
                catch (OutOfMemoryException error) when (ReferenceEquals(error, failure)) { }
                Check(prepared is null && resolver.State == 0 && resolver.QueryCalls == 2 && NativeDomSync.ReadToken(document) == token,
                    "partial query failure exposed owner or poisoned readiness/inputs");
                CheckNoCommit();
                resolver.OnQuery = _ => { };
                prepared = NativeInteractionOwner.Prepare(document, 703);
                Check(resolver.State == 2 && resolver.QueryCalls == 3, "retry did not finish only the unresolved original import");
            }
            else if (mode == "dispose")
            {
                Expect<ObjectDisposedException>(() => prepared = NativeInteractionOwner.Prepare(document, 703));
                Check(prepared is null && resolver.State == 2 && resolver.QueryCalls == 2, "owner was exposed after resolver disposal");
            }
            else
            {
                prepared = NativeInteractionOwner.Prepare(document, 703);
                Check(reentered && resolver.State == 2 && resolver.QueryCalls == 2 && NativeDomSync.ReadToken(document) == token,
                    "busy caller changed preparation authority or document inputs");
            }
            CheckNoCommit();
            Console.WriteLine($"PASS cold query owner-{mode}: state={resolver.State}; queryResolutions={resolver.QueryCalls}; no premature owner; DOM commits=0");
        }
        finally { prepared?.Dispose(); NativeDomWorkCounters.DisableForCurrentThread(); }
    }

    private static void BindingPreparation(Resolver resolver, string mode)
    {
        Check(mode is "retry" or "dispose" or "reentry", "unknown binding mode");
        using var dom = DomInterop.Parse("<body><button id='a'>button</button></body>");
        var button = dom.Document.GetElementById("a")!;
        var handle = dom.GetNativeHandle(button);
        var subtree = new ElementNode("span"); subtree.AppendChild(new TextNode("before"));
        var token = NativeDomSync.ReadToken(dom.Native);
        var failure = new OutOfMemoryException("query binding resolver failure");
        var busy = false;
        Check(NativeDomWorkCounters.TryEnableForCurrentThread(), "native counters unavailable");
        NativeDomWorkCounters.ResetForCurrentThread();
        resolver.OnQuery = ordinal =>
        {
            if (mode == "retry" && ordinal == 2) throw failure;
            if (ordinal != 1) return;
            if (mode == "dispose") dom.Dispose();
            if (mode == "reentry")
            {
                Expect<InvalidOperationException>(() => dom.EnableSynchronization(new Bookkeeping()));
                busy = resolver.State == 1 && dom.BindingCore is null;
                dom.Native.SetAttribute(handle, "data-resolver", "new");
                subtree.TextContent = "after"; subtree.SetAttribute("data-resolver", "new");
            }
        };
        try
        {
            if (mode == "retry")
            {
                try { dom.EnableSynchronization(new Bookkeeping()); throw new Exception("expected OOM"); }
                catch (OutOfMemoryException error) when (ReferenceEquals(error, failure)) { }
                Check(dom.BindingCore is null && resolver.State == 0 && resolver.QueryCalls == 2 && NativeDomSync.ReadToken(dom.Native) == token,
                    "failed binding preparation published or changed captured inputs");
                CheckNoCommit(); resolver.OnQuery = _ => { };
                dom.EnableSynchronization(new Bookkeeping());
                Check(resolver.State == 2 && resolver.QueryCalls == 3, "binding explicit retry did not finish linking");
            }
            else if (mode == "dispose")
            {
                Expect<ObjectDisposedException>(() => dom.EnableSynchronization(new Bookkeeping()));
                Check(dom.BindingCore is null && resolver.QueryCalls == 2 && resolver.State == 2, "disposed binding was published");
                CheckNoCommit();
            }
            else
            {
                dom.EnableSynchronization(new Bookkeeping());
                Check(busy && resolver.QueryCalls == 2 && resolver.State == 2 && button.GetAttribute("data-resolver") == "new",
                    "binding captured stale inputs or reentrant initializer published");
                button.AppendChild(subtree);
                using var snapshot = NativeDomSnapshot.Acquire(dom.Native);
                var span = snapshot.Nodes.Single(node => node.Tag == "span");
                Check(span.Attributes["data-resolver"] == "new" && snapshot.Nodes[(int)span.Children[0]].Text == "after",
                    "detached subtree was captured before loader callback");
            }
            Console.WriteLine($"PASS cold query binding-{mode}: state={resolver.State}; queryResolutions={resolver.QueryCalls}; revalidated after loader callback");
        }
        finally { NativeDomWorkCounters.DisableForCurrentThread(); }
    }

    private static void HostPreparation(Resolver resolver, string mode)
    {
        Check(mode is "retry" or "dispose" or "reentry", "unknown host mode");
        using var renderer = new Renderer();
        var component = new Component();
        using var host = NewHost(component, renderer);
        Check(resolver.State == 0 && resolver.QueryCalls == 0, "constructor unexpectedly bootstrapped queries");
        Check(NativeDomWorkCounters.TryEnableForCurrentThread(), "native counters unavailable");
        NativeDomWorkCounters.ResetForCurrentThread();
        var failure = new OutOfMemoryException("query host resolver failure");
        var reentered = false;
        resolver.OnQuery = ordinal =>
        {
            if (mode == "retry" && ordinal == 2) throw failure;
            if (ordinal != 1) return;
            if (mode == "dispose") host.Dispose();
            if (mode == "reentry") Reenter();
        };
        void Reenter()
        {
            Check(host.Document is null && Field<object?>(host, "_interactionSession") is null, "query preparation started after owner exposure");
            component.Change(); host.SetViewport(210, 200, 1); host.Render();
            reentered = renderer.Frames == 0 && host.Document is null;
        }
        try
        {
            if (mode is "retry" or "dispose")
            {
                if (mode == "retry")
                {
                    try { host.Render(); throw new Exception("expected OOM"); }
                    catch (OutOfMemoryException error) when (ReferenceEquals(error, failure)) { }
                }
                else Expect<ObjectDisposedException>(host.Render);
                Check(host.Document is null && Field<object?>(host, "_interactionSession") is null && renderer.Frames == 0 &&
                    Field<bool>(host, "_styleRetryBlocked"), "failed early preparation exposed owner/frame or enabled automatic retry");
                CheckNoCommit();
                if (mode == "retry")
                {
                    Check(resolver.State == 0 && resolver.QueryCalls == 2, "host partial failure poisoned readiness");
                    resolver.OnQuery = _ => Reenter();
                    host.Render();
                    Check(reentered && renderer.Frames == 1 && resolver.State == 2 && resolver.QueryCalls == 3 &&
                        host.Document!.GetElementById("a")!.GetAttribute("data-version") == "after",
                        "retry captured inputs before callback or did not recover exactly once");
                }
                else Check(resolver.State == 2 && resolver.QueryCalls == 2, "disposal skipped actual query preparation");
            }
            else
            {
                host.Render();
                Check(reentered && renderer.Frames == 1 && resolver.State == 2 && resolver.QueryCalls == 2 &&
                    host.Document!.GetElementById("a")!.GetAttribute("data-version") == "after",
                    "host capture preceded loader callback or reentry published twice");
            }
            Console.WriteLine($"PASS cold query host-{mode}: state={resolver.State}; queryResolutions={resolver.QueryCalls}; frames={renderer.Frames}; loader-before-capture/retry-block verified");
        }
        finally { NativeDomWorkCounters.DisableForCurrentThread(); }
    }

#if AETHER_DOM_FAULT_TESTING
    private static void FaultPreparation(Resolver resolver, string mode)
    {
        if (mode == "inventory")
        {
            using var dom = DomInterop.Parse("<body><button>button</button></body>");
            var trace = ManagedDomFault.Arm();
            try { dom.EnableSynchronization(new Bookkeeping()); }
            finally { trace.Dispose(); }
            var metadata = trace.Sites.Take(trace.Count).Count(site => site == "interop.publication.import-metadata");
            var prelink = trace.Sites.Take(trace.Count).Count(site => site == "interop.publication.import-prelink");
            Check(metadata == 3 && prelink == 6 && resolver.QueryCalls == 2, "cold import inventory changed unexpectedly");
            Console.WriteLine($"PASS cold query fault-inventory: metadata={metadata}; prelink={prelink}; queryImports=2; new source-site IDs=0");
            return;
        }
        Check(mode is "metadata" or "prelink-first" or "prelink-second", "unknown query fault mode");
        using var document = NativeDomDocument.Parse("<body><button>button</button></body>");
        // Prepare only the OTHER groups. The actual query group stays cold and is
        // first attempted through the real owner preparation entry, never a query.
        NativeMethods.InteractionOwnerPublication.EnsurePrepared();
        NativeMethods.InteractionEditPublication.EnsurePrepared();
        Check(resolver.State == 0 && resolver.QueryCalls == 0, "fault fixture warmed query group");
        var token = NativeDomSync.ReadToken(document);
        var resources = ManagedDomFault.Resources;
        var site = mode == "metadata" ? "interop.publication.import-metadata" : "interop.publication.import-prelink";
        var occurrence = mode == "prelink-second" ? 2 : 1;
        Check(NativeDomWorkCounters.TryEnableForCurrentThread(), "native counters unavailable");
        NativeDomWorkCounters.ResetForCurrentThread();
        NativeInteractionOwner? prepared = null;
        try
        {
            var fault = ManagedDomFault.ArmSite(site, occurrence);
            Exception? error = null;
            try { prepared = NativeInteractionOwner.Prepare(document, 705); }
            catch (Exception caught) { error = caught; }
            finally { fault.Dispose(); }
            Check(ReferenceEquals(error, fault.Failure) && prepared is null && resolver.State == 0 &&
                NativeDomSync.ReadToken(document) == token && ManagedDomFault.Resources == resources,
                "query source fault published owner, changed inputs/resources or poisoned readiness");
            CheckNoCommit();
            prepared = NativeInteractionOwner.Prepare(document, 705);
            Check(resolver.State == 2 && resolver.QueryCalls == 2 && NativeDomSync.ReadToken(document) == token,
                "query source fault retry did not link the two real imports");
            CheckNoCommit();
            prepared.Dispose(); prepared = null;
            Check(ManagedDomFault.Resources == resources, "query fault retry leaked owner resources");
            Console.WriteLine($"PASS cold query fault-{mode}: exact source occurrence reached; retry prepared=2; DOM commits=0; resources balanced");
        }
        finally { prepared?.Dispose(); NativeDomWorkCounters.DisableForCurrentThread(); }
    }
#endif

    private static void Measure(Resolver resolver, Func<bool> query, string label)
    {
        var calls = resolver.Calls;
        var start = GC.GetAllocatedBytesForCurrentThread();
        var result = query();
        var first = GC.GetAllocatedBytesForCurrentThread() - start;
        Check(result && first == 0 && resolver.Calls == calls, label + " allocated or invoked a resolver");
        start = GC.GetAllocatedBytesForCurrentThread();
        for (var i = 0; i < 1000; i++) result &= query();
        var repeated = GC.GetAllocatedBytesForCurrentThread() - start;
        Check(result && repeated == 0 && resolver.Calls == calls, label + " repeated allocation/resolution");
    }
    private sealed class Resolver
    {
        private readonly FieldInfo _state = typeof(NativeMethods.InteractionQueryImports).GetField("_preparation", BindingFlags.Static | BindingFlags.NonPublic)!;
        internal int Calls, QueryCalls;
        private int _callbackDepth;
        internal Action<int>? OnQuery;
        internal int State => (int)_state.GetValue(null)!;
        internal Resolver()
        {
            var library = NativeLibrary.Load("aether_core", typeof(NativeDomDocument).Assembly, null);
            NativeLibrary.SetDllImportResolver(typeof(NativeDomDocument).Assembly, (name, _, _) =>
            {
                if (name != "aether_core") return IntPtr.Zero;
                ++Calls;
                // Positive control tied to the ACTUAL group state, not generated names.
                // Any callback after publication increments Calls and fails query purity.
                var queryPreparation = State == 1 && _callbackDepth == 0;
                ++_callbackDepth;
                try
                {
                    // Disposal/mutation callbacks can themselves resolve unrelated imports.
                    if (queryPreparation) { ++QueryCalls; OnQuery?.Invoke(QueryCalls); }
                    return library;
                }
                finally { --_callbackDepth; }
            });
        }
    }
    private static UiDocumentHost NewHost(Component component, Renderer renderer) =>
        new(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions { Width = 200, Height = 200 });
    private sealed class Component : UiComponent
    {
        private string _version = "before";
        internal void Change() { _version = "after"; StateHasChanged(); }
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(
            "<body style='margin:0'><button id='a' data-version='" + _version + "' style='width:100px;height:60px'>button</button></body>");
    }
    private sealed class Renderer : IRenderBackend
    {
        internal int Frames;
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList display, ColorRgba color) => ++Frames;
        public void Dispose() { }
    }
    private sealed class Bookkeeping : IDomHostBookkeeping
    {
        public IDomPreparedBookkeeping Prepare(DomEdit? edit, bool import) => new Prepared();
        public void NotifyMutationCompleted() { }
        private sealed class Prepared : IDomPreparedBookkeeping
        { public void Validate() { } public void Publish() { } public void Dispose() { } }
    }
    private static void CheckNoCommit() => Check(NativeDomWorkCounters.ReadForCurrentThread().DomEditsCommitted == 0, "preparation invoked a dummy DOM commit");
    private static T Field<T>(object owner, string name) => (T)owner.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)!.GetValue(owner)!;
    private static void Expect<T>(Action action) where T : Exception
    { try { action(); } catch (T) { return; } throw new InvalidOperationException("Expected " + typeof(T).Name); }
    private static void Check(bool value, string message) { if (!value) throw new InvalidOperationException("Cold interaction queries: " + message); }
}
