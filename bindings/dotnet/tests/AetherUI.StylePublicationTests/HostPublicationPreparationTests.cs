#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

// Each mode runs in a fresh process, before any initial binding publication.
// The resolver only intercepts the actual binding group's first preparation.
internal static class HostPublicationPreparationTests
{
    internal static void Run(string mode)
    {
        Check(mode is "retry" or "dispose", "unknown bootstrap mode");
        var library = NativeLibrary.Load("aether_core", typeof(NativeDomDocument).Assembly, null);
        var state = typeof(NativeMethods.BindingPublication).GetField("_preparation", BindingFlags.Static | BindingFlags.NonPublic)!;
        UiDocumentHost? host = null;
        using var renderer = new Renderer();
        var injection = 0;
        var injections = 0;
        var reentered = false;
        var failure = new OutOfMemoryException("binding import bootstrap resolver failure");
        NativeLibrary.SetDllImportResolver(typeof(NativeDomDocument).Assembly, (name, _, _) =>
        {
            if (name != "aether_core") return IntPtr.Zero;
            // Other legitimate read-only imports may resolve before component
            // bootstrap. Never confuse them with the certified preparation edge.
            if (injection != 0 && (int)state.GetValue(null)! == 1)
            {
                var current = injection; injection = 0; ++injections;
                if (current == 1) throw failure;
                if (current == 2)
                {
                    host!.Render();
                    reentered = renderer.Frames == 0 && host.Document is null;
                }
                if (current == 3) host!.Dispose();
            }
            return library;
        });
        host = new(new Component(), new ApproximateTextMeasurer(), renderer,
            new UiHostOptions { Width = 200, Height = 200 });
        try
        {
            Check(NativeDomWorkCounters.TryEnableForCurrentThread(), "required commit counters unavailable");
            NativeDomWorkCounters.ResetForCurrentThread();
            DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.SetEnabled(true);
            var rejected = false;
            injection = mode == "retry" ? 1 : 3;
            var start = GC.GetAllocatedBytesForCurrentThread();
            try { host.Render(); }
            catch (OutOfMemoryException error) when (ReferenceEquals(error, failure) && mode == "retry") { rejected = true; }
            catch (ObjectDisposedException) when (mode == "dispose") { rejected = true; }
            finally { DomCommitAllocationLedger.SetEnabled(false); }
            var failedOperationBytes = GC.GetAllocatedBytesForCurrentThread() - start;
            var counts = DomCommitAllocationLedger.Read();
            Check(injections == 1 && rejected && !counts.Invalid && counts.Successful == 0 && counts.Failed == 0,
                "bootstrap failure/disposal crossed a publication boundary or was not observed");
            Check(renderer.Frames == 0 && host.Document is null && Field<bool>(host, "_styleRetryBlocked"),
                "bootstrap failure published or left automatic retry enabled");
            Check(NativeDomWorkCounters.ReadForCurrentThread().DomEditsCommitted == 0, "bootstrap ran a dummy DOM commit");
            if (mode == "retry")
            {
                Check((int)state.GetValue(null)! == 0, "failed bootstrap poisoned readiness");
                injection = 2;
                DomCommitAllocationLedger.Reset(); DomCommitAllocationLedger.SetEnabled(true);
                start = GC.GetAllocatedBytesForCurrentThread();
                try { host.Render(); }
                finally { DomCommitAllocationLedger.SetEnabled(false); }
                var retryOperationBytes = GC.GetAllocatedBytesForCurrentThread() - start;
                counts = DomCommitAllocationLedger.Read();
                Check(injections == 2 && reentered && renderer.Frames == 1 && host.Document is not null,
                    "explicit retry/reentry did not publish exactly one complete frame");
                Check(!counts.Invalid && counts.Successful == 3 && counts.Failed == 0 && counts.SuccessfulBytes == 0 &&
                    counts.Import == 1 && counts.ComponentInitial == 1 && counts.StylePublication == 1,
                    "first publication after retry lost a scope or allocated");
                Console.WriteLine($"PASS host bootstrap retry/reentry: first publication scopes=3 commitBytes=0; failedWholeOperationBytes={failedOperationBytes}, retryWholeOperationBytes={retryOperationBytes}");
            }
            else Console.WriteLine($"PASS host bootstrap disposal: scopes=0 frames=0 retryBlocked=true; wholeOperationBytes={failedOperationBytes} (not a zero-commit success)");
        }
        finally { NativeDomWorkCounters.DisableForCurrentThread(); host.Dispose(); }
    }
    private sealed class Component : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral("<body><button id='a'>button</button></body>");
    }
    private sealed class Renderer : IRenderBackend
    {
        internal int Frames;
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList display, ColorRgba color) => ++Frames;
        public void Dispose() { }
    }
    private static T Field<T>(object owner, string name) => (T)owner.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(owner)!;
    private static void Check(bool value, string message) { if (!value) throw new InvalidOperationException("Host publication preparation: " + message); }
}
#endif
