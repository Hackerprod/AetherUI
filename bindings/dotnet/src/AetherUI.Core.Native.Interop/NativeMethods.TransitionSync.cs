using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeTransitionToken(
    ulong InstanceId, ulong MutationSerial, ulong StatusRevision, int TickError)
{
    public bool AllowsPublication => InstanceId != 0 && TickError == 0;
}

/// <summary>Borrowed native styles, valid only for the duration of PreparePublication.</summary>
[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeTransitionObservation(IntPtr Node, IntPtr Before, IntPtr After);

internal static unsafe partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_read_token")]
    internal static partial int TransitionRuntimeReadToken(IntPtr runtime, out NativeTransitionToken token);

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_prepare")]
    internal static partial int TransitionRuntimePrepare(IntPtr runtime,
        NativeTransitionObservation* observations, nuint observationCount,
        IntPtr* retiredNodes, nuint retirementCount, long nowMs, out IntPtr candidate);

    [LibraryImport(Lib, EntryPoint = "aether_transition_candidate_validate")]
    internal static partial int TransitionCandidateValidate(TransitionCandidateHandle candidate);

    [LibraryImport(Lib, EntryPoint = "aether_transition_candidate_commit")]
    internal static partial int TransitionCandidateCommit(TransitionCandidateHandle candidate,
        out NativeTransitionToken token);

    [LibraryImport(Lib, EntryPoint = "aether_transition_candidate_destroy")]
    internal static partial void TransitionCandidateDestroy(IntPtr candidate);

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_try_tick")]
    internal static partial int TransitionRuntimeTryTick(IntPtr runtime, long nowMs, out int active);
}

// Native staging retains its own authentic lifetime control and storage. This
// SafeHandle also pins the managed runtime wrapper, without delaying its explicit
// logical Dispose. Finalizer cleanup releases staging only, never mutates live.
internal sealed class TransitionCandidateHandle : SafeHandle
{
    private readonly TransitionRuntimeInterop _owner;
    internal TransitionCandidateHandle(TransitionRuntimeInterop owner) : base(IntPtr.Zero, true) => _owner = owner;
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal void Initialize(IntPtr value) => SetHandle(value);
    protected override bool ReleaseHandle()
    {
        NativeMethods.TransitionCandidateDestroy(handle);
        GC.KeepAlive(_owner);
        return true;
    }
}
