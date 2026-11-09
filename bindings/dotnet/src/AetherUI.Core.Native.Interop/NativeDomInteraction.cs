using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

// Finalizers only release handles. Logical retirement belongs to the owner-thread host,
// so neither an arbitrary finalizer thread nor a SafeHandle release can acknowledge loss.
internal sealed class NativeInteractionOwner : SafeHandle
{
    private NativeInteractionOwner() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal static NativeInteractionOwner Prepare(NativeDomDocument document, ulong host)
    {
        NativeMethods.InteractionOwnerPublication.EnsurePrepared();
        NativeMethods.InteractionEditPublication.EnsurePrepared();
        NativeMethods.InteractionQueryImports.EnsurePrepared();
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.interaction.owner-wrapper");
#endif
        var result = new NativeInteractionOwner();
        try
        {
            using var borrow = document.Borrow();
            NativeDomSync.Throw(NativeMethods.InteractionOwnerPrepare(borrow.Handle, host, out var value));
            result.SetHandle(value);
#if AETHER_DOM_FAULT_TESTING
            if (value != IntPtr.Zero) ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.InteractionOwner);
#endif
            return result;
        }
        catch { result.Dispose(); throw; }
    }
    internal NativeInteractionClaim PrepareClaim(IntPtr node, int bit, ulong registry)
    {
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.interaction.claim-wrapper");
#endif
        var claim = new NativeInteractionClaim();
        try
        {
            NativeDomSync.Throw(NativeMethods.InteractionOwnerPrepareClaim(this, node, bit, registry, out var value));
            claim.Initialize(value); return claim;
        }
        catch { claim.Dispose(); throw; }
    }
    internal void ValidatePrepared() => NativeDomSync.Throw(NativeMethods.InteractionOwnerPublication.ValidatePrepared(this));
    internal void Activate(IntPtr binding) => NativeDomSync.Throw(NativeMethods.InteractionOwnerPublication.Activate(this, binding));
    internal void Retire() => NativeDomSync.Throw(NativeMethods.InteractionOwnerDispose(this));
    protected override bool ReleaseHandle()
    {
        NativeMethods.InteractionOwnerRelease(handle);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.ResourceReleased(ManagedDomFault.Resource.InteractionOwner);
#endif
        return true;
    }
}
internal sealed class NativeInteractionClaim : SafeHandle
{
    internal NativeInteractionClaim() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal void Initialize(IntPtr value)
    {
        SetHandle(value);
#if AETHER_DOM_FAULT_TESTING
        if (value != IntPtr.Zero) ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.InteractionClaim);
#endif
    }
    internal NativeInteractionClaimInfo Read()
    { NativeDomSync.Throw(NativeMethods.InteractionQueryImports.InteractionClaimRead(this, out var info)); return info; }
    internal bool IsCurrent => !IsClosed && !IsInvalid && NativeMethods.InteractionQueryImports.InteractionClaimRead(this, out var info) == 0 && info.Current != 0;
    // Pure availability query: imports were linked before owner preparation; no
    // DOM import, decoding, acknowledgment or delivery occurs here. An
    // unexpected native status keeps work pending so the safe point reports it.
    internal bool HasPendingLoss => !IsClosed && !IsInvalid &&
        (NativeMethods.InteractionQueryImports.InteractionClaimReadLoss(this, out var present, out _) != 0 || present != 0);
    internal bool TryReadLoss(out NativeInteractionLoss loss)
    {
        NativeDomSync.Throw(NativeMethods.InteractionQueryImports.InteractionClaimReadLoss(this, out var present, out loss));
        return present != 0;
    }
    internal void Acknowledge(in NativeInteractionLoss loss) =>
        NativeDomSync.Throw(NativeMethods.InteractionClaimAck(this, loss.Claim.Generation, loss.RecordId, loss.RecordSerial));
    internal void Retire() => NativeDomSync.Throw(NativeMethods.InteractionClaimDispose(this));
    protected override bool ReleaseHandle()
    {
        NativeMethods.InteractionClaimRelease(handle);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.ResourceReleased(ManagedDomFault.Resource.InteractionClaim);
#endif
        return true;
    }
}
internal sealed class NativeInteractionEditHandle : SafeHandle
{
    internal NativeInteractionEditHandle() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal void Initialize(IntPtr value)
    {
        SetHandle(value);
#if AETHER_DOM_FAULT_TESTING
        if (value != IntPtr.Zero) ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.InteractionEdit);
#endif
    }
    protected override bool ReleaseHandle()
    {
        NativeMethods.InteractionEditRelease(handle);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.ResourceReleased(ManagedDomFault.Resource.InteractionEdit);
#endif
        return true;
    }
}
internal sealed class NativeInteractionEdit : IDisposable
{
    private readonly NativeInteractionEditHandle _handle = new();
    // Preallocated even for cleanup. Commit must not construct a managed SafeHandle.
    internal readonly NativeInteractionClaim Claim = new();
    internal NativeInteractionCandidate Candidate { get; private set; }
    private bool _transferred;
    private NativeInteractionEdit() { }
    internal static NativeInteractionEdit Acquire(NativeInteractionOwner owner, NativeDomToken token, IntPtr node, int bit, ulong registry)
    {
        NativeMethods.InteractionEditPublication.EnsurePrepared();
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.interaction.edit-acquire-wrapper");
#endif
        var edit = new NativeInteractionEdit();
        try
        {
            NativeDomSync.Throw(NativeMethods.InteractionPrepareAcquire(owner, in token, node, bit, registry, out var handle));
            edit.Initialize(handle); return edit;
        }
        catch { edit.Dispose(); throw; }
    }
    internal static NativeInteractionEdit Cleanup(NativeInteractionClaim claim, ulong generation, ulong recordId, ulong recordSerial)
    {
        NativeMethods.InteractionEditPublication.EnsurePrepared();
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.interaction.edit-cleanup-wrapper");
#endif
        var edit = new NativeInteractionEdit();
        try
        {
            NativeDomSync.Throw(NativeMethods.InteractionPrepareCleanup(claim, generation, recordId, recordSerial, out var handle));
            edit.Initialize(handle); return edit;
        }
        catch { edit.Dispose(); throw; }
    }
    private void Initialize(IntPtr handle)
    {
        _handle.Initialize(handle);
        NativeDomSync.Throw(NativeMethods.InteractionEditCandidate(_handle, out var candidate));
        Candidate = candidate;
    }
    internal void Validate() => NativeDomSync.Throw(NativeMethods.InteractionEditPublication.Validate(_handle));
    internal NativeDomToken Commit()
    {
        NativeDomSync.Throw(NativeMethods.InteractionEditPublication.Commit(_handle, out var claim, out var token));
        Claim.Initialize(claim); _transferred = claim != IntPtr.Zero;
        return token;
    }
    public void Dispose() { _handle.Dispose(); if (!_transferred) Claim.Dispose(); }
}
