using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

internal static unsafe partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_create_v1")]
    internal static partial int DomStyleBindingCreate(IntPtr document, IntPtr runtime, out IntPtr binding);
    [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_prepare_create_v1")]
    internal static partial int DomStyleBindingPrepareCreate(IntPtr document, IntPtr runtime, out IntPtr transaction);
    [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_create_destroy_v1")]
    internal static partial void DomStyleBindingCreateDestroy(IntPtr transaction);
    [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_destroy_v1")]
    internal static partial void DomStyleBindingDestroy(IntPtr binding);
    [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_read_slot_v1")]
    internal static partial int DomStyleBindingReadSlot(IntPtr binding, out long handle, out int present, out ulong sheetEpoch);
    [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_prepare_rebind_v1")]
    internal static partial int DomStyleBindingPrepareRebind(IntPtr binding, IntPtr document, out IntPtr transaction);
    [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_rebind_destroy_v1")]
    internal static partial void DomStyleBindingRebindDestroy(IntPtr transaction);
    internal static partial class BindingPublication
    {
        private static int _preparation;
        internal static void EnsurePrepared() => NativePublicationImports.EnsurePrepared(ref _preparation, typeof(BindingPublication), 4);
        [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_validate_create_v1")]
        internal static partial int ValidateCreate(IntPtr transaction);
        [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_commit_create_v1")]
        internal static partial int CommitCreate(IntPtr transaction, out IntPtr binding);
        [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_validate_rebind_v1")]
        internal static partial int ValidateRebind(IntPtr transaction);
        [LibraryImport(Lib, EntryPoint = "aether_dom_style_binding_commit_rebind_v1")]
        internal static partial int CommitRebind(IntPtr transaction, out IntPtr binding);
    }
}
