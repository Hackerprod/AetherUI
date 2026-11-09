using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct NativeInteractionClaimInfo(IntPtr Node,
    ulong HostIdentity, ulong BindingIdentity, ulong OwnerIdentity, ulong Generation,
    ulong NodeIdentity, ulong DisconnectSerial, ulong StateBitWriteSerial, ulong RegistryIdentity,
    int Bit, int Current);
[StructLayout(LayoutKind.Sequential)]
internal struct NativeInteractionLoss
{
    internal NativeInteractionClaimInfo Claim;
    internal ulong RecordId, RecordSerial, DisconnectSerial, StateBitWriteSerial;
    internal int Reason, HandlerPresent;
    internal NativeDomString Handler;
}
[StructLayout(LayoutKind.Sequential)]
internal struct NativeInteractionCandidate
{
    internal NativeInteractionClaimInfo Claim;
    internal int BeforeState, AfterState, Applied, StateChanged;
}

internal static unsafe partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_owner_prepare_v1")]
    internal static partial int InteractionOwnerPrepare(IntPtr document, ulong host, out IntPtr owner);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_owner_prepare_claim_v1")]
    internal static partial int InteractionOwnerPrepareClaim(NativeInteractionOwner owner, IntPtr node, int bit, ulong registry, out IntPtr claim);
    internal static partial class InteractionOwnerPublication
    {
        private static int _preparation;
        internal static void EnsurePrepared() => NativePublicationImports.EnsurePrepared(ref _preparation, typeof(InteractionOwnerPublication), 2);
        [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_owner_validate_prepared_v1")]
        internal static partial int ValidatePrepared(NativeInteractionOwner owner);
        [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_owner_activate_v1")]
        internal static partial int Activate(NativeInteractionOwner owner, IntPtr binding);
    }
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_owner_dispose_v1")]
    internal static partial int InteractionOwnerDispose(NativeInteractionOwner owner);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_owner_release_v1")]
    internal static partial void InteractionOwnerRelease(IntPtr owner);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_prepare_acquire_v1")]
    internal static partial int InteractionPrepareAcquire(NativeInteractionOwner owner, in NativeDomToken expected, IntPtr node, int bit, ulong registry, out IntPtr edit);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_prepare_cleanup_v1")]
    internal static partial int InteractionPrepareCleanup(NativeInteractionClaim claim, ulong generation, ulong recordId, ulong recordSerial, out IntPtr edit);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_edit_candidate_v1")]
    internal static partial int InteractionEditCandidate(NativeInteractionEditHandle edit, out NativeInteractionCandidate candidate);
    internal static partial class InteractionEditPublication
    {
        private static int _preparation;
        internal static void EnsurePrepared() => NativePublicationImports.EnsurePrepared(ref _preparation, typeof(InteractionEditPublication), 2);
        [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_edit_validate_v1")]
        internal static partial int Validate(NativeInteractionEditHandle edit);
        [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_edit_commit_v1")]
        internal static partial int Commit(NativeInteractionEditHandle edit, out IntPtr claim, out NativeDomToken token);
    }
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_edit_release_v1")]
    internal static partial void InteractionEditRelease(IntPtr edit);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_claim_read_v1")]
    internal static partial int InteractionClaimRead(NativeInteractionClaim claim, out NativeInteractionClaimInfo info);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_claim_read_loss_v1")]
    internal static partial int InteractionClaimReadLoss(NativeInteractionClaim claim, out int present, out NativeInteractionLoss loss);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_claim_ack_v1")]
    internal static partial int InteractionClaimAck(NativeInteractionClaim claim, ulong generation, ulong recordId, ulong recordSerial);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_claim_dispose_v1")]
    internal static partial int InteractionClaimDispose(NativeInteractionClaim claim);
    [LibraryImport(Lib, EntryPoint = "aether_dom_interaction_claim_release_v1")]
    internal static partial void InteractionClaimRelease(IntPtr claim);
}
