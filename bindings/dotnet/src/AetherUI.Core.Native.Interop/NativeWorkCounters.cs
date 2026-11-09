using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

/// <summary>Optional diagnostic work on the calling thread, including aborted work.
/// Not a per-host snapshot. No identities or input positions are collected.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct NativeWorkCountersSnapshot
{
    public ulong ComputeNodes;
    public ulong StateEntriesVisited;
    public ulong RevisionRecordsCreated;
    public ulong RevisionEntriesCreated;
    public ulong RevisionPrepares;
    public ulong RevisionPublications;
    public ulong StateFullFallbacks;
    public ulong MapEntriesCopied;
    public ulong TransitionContextEntriesVisited;
    public ulong TransitionContextBuilds;
    public ulong HitQueries;
    public ulong HitBoxesVisited;
    public ulong PaintOrderChildrenClassified;
}

public static class NativeWorkCounters
{
    public static bool TryEnableForCurrentThread()
    {
        try { return NativeMethods.WorkCountersSetEnabled(1) != 0; }
        catch (EntryPointNotFoundException) { return false; }
    }

    public static NativeWorkCountersSnapshot ReadForCurrentThread()
    {
        var status = NativeMethods.WorkCountersGet(out var counters);
        if (status != 0) throw new InvalidOperationException($"Work counter read failed: {status}.");
        return counters;
    }
}

internal static unsafe partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_work_counters_set_enabled")]
    internal static partial int WorkCountersSetEnabled(int enabled);

    [LibraryImport(Lib, EntryPoint = "aether_work_counters_get_v1")]
    internal static partial int WorkCountersGet(out NativeWorkCountersSnapshot counters);
}
