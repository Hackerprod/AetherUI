using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

/// <summary>DOMsync work on the current thread, including work subsequently aborted.
/// Separate from the original work-counter ABI. Managed parse/rebind counts cover the
/// managed bridge call sites only. Snapshot traversal counts cover native snapshots only.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct NativeDomWorkCountersSnapshot
{
    public ulong Available;
    public ulong Enabled;
    public ulong DomCommitAllocationCoverage;
    public ulong DomTokenReads;
    public ulong DomSnapshotBuilds;
    public ulong DomSnapshotNodesVisited;
    public ulong DomSnapshotAttributesVisited;
    public ulong DomSnapshotChildrenVisited;
    public ulong DomNodesImported;
    public ulong DomEditPrepareAttempts;
    public ulong DomEditsPrepared;
    public ulong DomEditsCommitted;
    public ulong DomEditsAborted;
    public ulong DomCommitAllocations;
    public ulong TransitionCandidatesCloned;
    public ulong TransitionEntriesCopied;
    public ulong ManagedDomParseCalls;
    public ulong ManagedDomRebindCalls;
    public ulong ManagedDomAttributesVisited;
    public ulong ManagedDomChildrenVisited;
    public ulong DomImportInitialBinding;
    public ulong DomImportTokenChanged;

    /// <summary>False means DomCommitAllocations is unavailable, not measured zero.</summary>
    public readonly bool HasCommitAllocationMeasurement => DomCommitAllocationCoverage != 0;
}

internal enum NativeDomImportFallback { InitialBinding = 5, TokenChanged = 6 }

/// <summary>Opt-in diagnostic session. Disabled by default, thread-local, saturating.
/// Enable through this class when measuring managed imports. Reset never enables it.
/// All native counters share the original NativeWorkCounters enable/reset state.</summary>
public static class NativeDomWorkCounters
{
    [ThreadStatic] private static bool _managedHooksEnabled;

    public static bool TryEnableForCurrentThread()
    {
        try
        {
            // Verify the additive entry point before enabling any bridge hooks.
            if (NativeMethods.DomWorkCountersGet(out var snapshot) != 0 || snapshot.Available == 0) return false;
            _managedHooksEnabled = NativeWorkCounters.TryEnableForCurrentThread();
            return _managedHooksEnabled;
        }
        catch (EntryPointNotFoundException) { return false; }
    }

    public static void DisableForCurrentThread()
    {
        _managedHooksEnabled = false;
        NativeMethods.WorkCountersSetEnabled(0);
    }

    public static void ResetForCurrentThread() => NativeMethods.DomWorkCountersReset();

    public static NativeDomWorkCountersSnapshot ReadForCurrentThread()
    {
        var status = NativeMethods.DomWorkCountersGet(out var counters);
        if (status != 0) throw new InvalidOperationException($"DOM work counter read failed: {status}.");
        return counters;
    }

    internal static void RecordNodesImported(ulong count) => Record(0, count);
    internal static void RecordParse() => Record(1, 1);
    internal static void RecordRebind() => Record(2, 1);
    internal static void RecordTraversal(ulong attributes, ulong children)
    {
        if (attributes != 0) Record(3, attributes);
        if (children != 0) Record(4, children);
    }
    internal static void RecordImportFallback(NativeDomImportFallback reason) => Record((int)reason, 1);
    private static void Record(int kind, ulong count)
    {
        if (_managedHooksEnabled) NativeMethods.DomWorkCountersRecordManaged(kind, count);
    }
}

internal static unsafe partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_dom_work_counters_get_v1")]
    internal static partial int DomWorkCountersGet(out NativeDomWorkCountersSnapshot counters);
    [LibraryImport(Lib, EntryPoint = "aether_dom_work_counters_record_managed_v1")]
    internal static partial int DomWorkCountersRecordManaged(int kind, ulong count);
    [LibraryImport(Lib, EntryPoint = "aether_work_counters_reset")]
    internal static partial void DomWorkCountersReset();
}
