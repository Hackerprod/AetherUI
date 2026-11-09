using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

internal static partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_dom_owner_create_v1")]
    internal static partial int DomOwnerCreate(out IntPtr owner, out IntPtr document);
    [LibraryImport(Lib, EntryPoint = "aether_dom_owner_retire_v1")]
    internal static partial int DomOwnerRetire(NativeDomOwnerHandle owner);
    [LibraryImport(Lib, EntryPoint = "aether_dom_owner_release_v1")]
    internal static partial void DomOwnerRelease(IntPtr owner);
}

// ReleaseHandle deliberately does not logically dispose the DOM. External native
// derivatives independently retain the current owner/root through native relay pins.
internal sealed class NativeDomOwnerHandle : SafeHandle
{
    internal NativeDomOwnerHandle() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal void Initialize(IntPtr owner)
    {
        SetHandle(owner);
#if AETHER_DOM_FAULT_TESTING
        if (owner != IntPtr.Zero) ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.Owner);
#endif
    }
    protected override bool ReleaseHandle()
    {
        NativeMethods.DomOwnerRelease(handle);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.ResourceReleased(ManagedDomFault.Resource.Owner);
#endif
        return true;
    }
}

/// <summary>Scoped borrow, never copied or retained after Dispose. No allocation.</summary>
internal readonly struct NativeDomBorrow : IDisposable
{
    private readonly NativeDomOwnerHandle? _owner;
    internal IntPtr Handle { get; }
    internal NativeDomBorrow(NativeDomOwnerHandle? owner, IntPtr document)
    {
        if (owner is not null)
        {
            var success = false;
            owner.DangerousAddRef(ref success);
            if (!success) throw new ObjectDisposedException(nameof(NativeDomDocument));
        }
        _owner = owner; Handle = document;
    }
    public void Dispose() => _owner?.DangerousRelease();
}
