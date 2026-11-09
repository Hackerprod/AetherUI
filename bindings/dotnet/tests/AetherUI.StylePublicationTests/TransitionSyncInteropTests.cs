using System.Runtime.InteropServices;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Runtime;

// Deliberately not registered in Program.cs by this worker. Run() uses the real
// native library, not a mock, and the private deterministic native fault seam.
internal static class TransitionSyncInteropTests
{
    [DllImport("aether_core", EntryPoint = "aether_transition_test_fail_after", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FailAfter(long allocations);

    internal static void Run()
    {
        using var document = NativeDomDocument.Parse("<div id='transition-node'></div>");
        var node = document.EnumerateElementHandles().Last();
        using var runtime = new TransitionRuntimeInterop();
        var before = new ComputedStyle { Opacity = 1 };
        var after = new ComputedStyle { Opacity = 0, TransitionProperties = 1, TransitionDuration = 1 };
        using var observation = TransitionStyleMarshaler.Prepare(node, before, after);
        using (var candidate = TransitionStyleMarshaler.PrepareBatch(runtime, new[] { observation }, [], 100))
        {
            Check(!runtime.HasActive, "prepare must not change live runtime");
            var handle = runtime.Handle;
            Check(candidate.Validate() == 0, "candidate validates");
            Check(candidate.TryCommit(out var published) == 0 && published.AllowsPublication, "candidate commits");
            Check(handle == runtime.Handle, "builder's borrowed runtime handle is preserved");
            Check(runtime.IsCurrent(published), "published token current");
            Check(candidate.TryCommit(out _) != 0, "candidate cannot commit twice");
        }
        Check(runtime.TryTick(600, out var active) == 0 && active, "half sample");
        Check(runtime.TryGetOpacityOverride(node, out var opacity) && Math.Abs(opacity - .5f) < .001f, "half opacity");
        var baseToken = runtime.ReadToken();
        using var stale = runtime.PreparePublication([], [], 600);
        try
        {
            FailAfter(0);
            Check(runtime.TryTick(1100, out active) == -3 && !active, "TryTick returns status, initialized output only");
        }
        finally { FailAfter(-1); }
        var errorToken = runtime.ReadToken();
        Check(errorToken.MutationSerial == baseToken.MutationSerial && errorToken.StatusRevision > baseToken.StatusRevision,
            "OOM only advances error revision");
        Check(errorToken.TickError == -3 && !runtime.IsCurrent(errorToken), "error token cannot certify presentation");
        Check(runtime.HasActive && runtime.TryGetOpacityOverride(node, out opacity) && opacity == .5f, "OOM preserves prior sample");
        observation.Apply(runtime, 600);
        Check(runtime.ReadToken().TickError == -3, "Observe cannot acknowledge TickError");
        Check(stale.TryCommit(out _) != 0, "old candidate rejected after error");
        Check(runtime.TryTick(1600, out active) == 0 && !active, "explicit complete sample acknowledges error");
        Check(runtime.ReadToken().TickError == 0 && runtime.ReadToken().StatusRevision > errorToken.StatusRevision,
            "success status has new revision");
        Check(stale.Validate() != 0, "failure then success cannot resurrect candidate");
        Check(runtime.TryGetOpacityOverride(node, out opacity) && opacity == 0, "finished endpoint retained");
        using (var empty = runtime.PreparePublication([], [], 1600))
        {
            var token = runtime.ReadToken();
            Check(empty.TryCommit(out var same) == 0 && same == token, "empty commit preserves serial");
        }
        using (var candidate = runtime.PreparePublication([], [], 1600))
        {
            var otherThreadStatus = 0;
            var thread = new Thread(() => otherThreadStatus = candidate.Validate());
            thread.Start(); thread.Join();
            Check(otherThreadStatus == -2, "wrong owner thread rejected");
        }
        // Managed allocations happen before native staging. Native preparation
        // failure cannot leak a handle or mutate active/finished/token state.
        var beforeFailure = runtime.ReadToken();
        try
        {
            FailAfter(0);
            try { using var ignored = runtime.PreparePublication([], [], 1600); throw new Exception("Expected OOM"); }
            catch (OutOfMemoryException) { }
        }
        finally { FailAfter(-1); }
        Check(runtime.ReadToken() == beforeFailure, "prepare failure does not change status or samples");
        using var retained = runtime.PreparePublication([], [], 1600);
        runtime.Dispose();
        Check(retained.Validate() == -4 && retained.TryCommit(out _) == -4, "candidate safely outlives runtime disposal");
        using var replacement = new TransitionRuntimeInterop();
        Check(replacement.ReadToken().InstanceId != beforeFailure.InstanceId, "instance identity never reuses disposed instance");
        Console.WriteLine("PASS transition sync native P/Invoke lifecycle, status, staging and OOM cases");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException("Transition sync interop: " + message);
    }
}
