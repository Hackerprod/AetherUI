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
        var result = new NativeInteractionOwner();
        try
        {
            using var borrow = document.Borrow();
            NativeDomSync.Throw(NativeMethods.InteractionOwnerPrepare(borrow.Handle, host, out var value));
            result.SetHandle(value); return result;
        }
        catch { result.Dispose(); throw; }
    }
    internal NativeInteractionClaim PrepareClaim(IntPtr node, int bit, ulong registry)
    {
        var claim = new NativeInteractionClaim();
        try
        {
            NativeDomSync.Throw(NativeMethods.InteractionOwnerPrepareClaim(this, node, bit, registry, out var value));
            claim.Initialize(value); return claim;
        }
        catch { claim.Dispose(); throw; }
    }
    internal void ValidatePrepared() => NativeDomSync.Throw(NativeMethods.InteractionOwnerValidatePrepared(this));
    internal void Activate(IntPtr binding) => NativeDomSync.Throw(NativeMethods.InteractionOwnerActivate(this, binding));
    internal void Retire() => NativeDomSync.Throw(NativeMethods.InteractionOwnerDispose(this));
    protected override bool ReleaseHandle() { NativeMethods.InteractionOwnerRelease(handle); return true; }
}
internal sealed class NativeInteractionClaim : SafeHandle
{
    internal NativeInteractionClaim() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal void Initialize(IntPtr value) => SetHandle(value);
    internal NativeInteractionClaimInfo Read()
    { NativeDomSync.Throw(NativeMethods.InteractionClaimRead(this, out var info)); return info; }
    internal bool IsCurrent => !IsClosed && !IsInvalid && NativeMethods.InteractionClaimRead(this, out var info) == 0 && info.Current != 0;
    internal bool TryReadLoss(out NativeInteractionLoss loss)
    {
        NativeDomSync.Throw(NativeMethods.InteractionClaimReadLoss(this, out var present, out loss));
        return present != 0;
    }
    internal void Acknowledge(in NativeInteractionLoss loss) =>
        NativeDomSync.Throw(NativeMethods.InteractionClaimAck(this, loss.Claim.Generation, loss.RecordId, loss.RecordSerial));
    internal void Retire() => NativeDomSync.Throw(NativeMethods.InteractionClaimDispose(this));
    protected override bool ReleaseHandle() { NativeMethods.InteractionClaimRelease(handle); return true; }
}
internal sealed class NativeInteractionEditHandle : SafeHandle
{
    internal NativeInteractionEditHandle() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal void Initialize(IntPtr value) => SetHandle(value);
    protected override bool ReleaseHandle() { NativeMethods.InteractionEditRelease(handle); return true; }
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
    internal void Validate() => NativeDomSync.Throw(NativeMethods.InteractionEditValidate(_handle));
    internal NativeDomToken Commit()
    {
        NativeDomSync.Throw(NativeMethods.InteractionEditCommit(_handle, out var claim, out var token));
        Claim.Initialize(claim); _transferred = claim != IntPtr.Zero;
        return token;
    }
    public void Dispose() { _handle.Dispose(); if (!_transferred) Claim.Dispose(); }
}
