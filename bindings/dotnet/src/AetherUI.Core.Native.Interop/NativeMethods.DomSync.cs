using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct NativeDomToken(ulong OwnerIdentity, ulong ObservationSerial, ulong InputEpoch, ulong StructureEpoch, int StickyStatus);
[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomString { internal IntPtr Data; internal nuint Length; }
[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomAttribute { internal NativeDomString Name, Value; }
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeDomSnapshotNode
{
    internal IntPtr Node;
    internal ulong Identity, OwnerIdentity, DisconnectSerial;
    internal int Type, State, Alive, Attached;
    internal long ParentIndex, StyleNodeId;
    internal NativeDomString Tag, Text, AuthoredValue, RuntimeValue;
    internal int AuthoredValuePresent, RuntimeValuePresent;
    internal nuint AttributeCount, ChildCount;
    internal fixed ulong StateBitWriteSerial[32];
}
[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomNodeSpec
{
    internal int Type, State;
    internal long ParentIndex;
    internal NativeDomString Tag, Text, RuntimeValue;
    internal int RuntimeValuePresent;
    internal IntPtr Attributes;
    internal nuint AttributeCount;
    internal IntPtr ExistingNode;
}
[StructLayout(LayoutKind.Sequential)]
internal struct NativeDomEditSpec
{
    internal int Kind;
    internal IntPtr Target, Child;
    internal long Index;
    internal NativeDomString Name, Value;
    internal int State, ValuePresent;
    internal IntPtr Nodes;
    internal nuint NodeCount;
}

internal static unsafe partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_dom_sync_version")]
    internal static partial uint DomSyncVersion();
    [LibraryImport(Lib, EntryPoint = "aether_dom_retain_node_v1")]
    internal static partial int DomRetainNode(IntPtr node, out IntPtr lease);
    [LibraryImport(Lib, EntryPoint = "aether_dom_node_lease_destroy_v1")]
    internal static partial void DomLeaseDestroy(IntPtr lease);
    [LibraryImport(Lib, EntryPoint = "aether_dom_node_lease_get_v1")]
    internal static partial int DomLeaseGet(NativeDomLease lease, out IntPtr node);
    [LibraryImport(Lib, EntryPoint = "aether_dom_read_node_identity_v1")]
    internal static partial int DomReadNodeIdentity(IntPtr node, out ulong identity);
    [LibraryImport(Lib, EntryPoint = "aether_dom_read_token_v1")]
    internal static partial int DomReadToken(IntPtr node, out NativeDomToken token);
    [LibraryImport(Lib, EntryPoint = "aether_dom_acquire_snapshot_v1")]
    internal static partial int DomAcquireSnapshot(IntPtr document, IntPtr* known, nuint count, out IntPtr snapshot);
    [LibraryImport(Lib, EntryPoint = "aether_dom_snapshot_destroy_v1")]
    internal static partial void DomSnapshotDestroy(IntPtr snapshot);
    [LibraryImport(Lib, EntryPoint = "aether_dom_snapshot_token_v1")]
    internal static partial int DomSnapshotToken(NativeDomSnapshotHandle snapshot, out NativeDomToken token);
    [LibraryImport(Lib, EntryPoint = "aether_dom_snapshot_count_v1")]
    internal static partial int DomSnapshotCount(NativeDomSnapshotHandle snapshot, out nuint count);
    [LibraryImport(Lib, EntryPoint = "aether_dom_snapshot_node_v1")]
    internal static partial int DomSnapshotNode(NativeDomSnapshotHandle snapshot, nuint index, out NativeDomSnapshotNode node);
    [LibraryImport(Lib, EntryPoint = "aether_dom_snapshot_attribute_v1")]
    internal static partial int DomSnapshotAttribute(NativeDomSnapshotHandle snapshot, nuint node, nuint attribute, out NativeDomAttribute value);
    [LibraryImport(Lib, EntryPoint = "aether_dom_snapshot_child_v1")]
    internal static partial int DomSnapshotChild(NativeDomSnapshotHandle snapshot, nuint node, nuint child, out nuint index);
    [LibraryImport(Lib, EntryPoint = "aether_dom_prepare_edit_v1")]
    internal static partial int DomPrepareEdit(in NativeDomToken expected, in NativeDomEditSpec spec, out IntPtr edit);
    [LibraryImport(Lib, EntryPoint = "aether_dom_edit_created_node_v1")]
    internal static partial int DomEditCreatedNode(NativeDomEditHandle edit, nuint index, out IntPtr node);
    internal static partial class DomEditPublication
    {
        private static int _preparation;
        internal static void EnsurePrepared() => NativePublicationImports.EnsurePrepared(ref _preparation, typeof(DomEditPublication), 2);
        [LibraryImport(Lib, EntryPoint = "aether_dom_edit_validate_v1")]
        internal static partial int Validate(NativeDomEditHandle edit);
        [LibraryImport(Lib, EntryPoint = "aether_dom_edit_commit_v1")]
        internal static partial int Commit(NativeDomEditHandle edit, out NativeDomToken token);
    }
    [LibraryImport(Lib, EntryPoint = "aether_dom_edit_destroy_v1")]
    internal static partial void DomEditDestroy(IntPtr edit);
}
