using System.Runtime.InteropServices;
using System.Text;

namespace AetherUI.Core.Native;

internal static class NativeDomSync
{
    internal static void PreparePublicationImports()
    {
        NativeMethods.DomEditPublication.EnsurePrepared();
        NativeMethods.InteractionEditPublication.EnsurePrepared();
        NativeMethods.InteractionQueryImports.EnsurePrepared();
    }

    internal static void PrepareComponentPublicationImports()
    {
        NativeMethods.BindingPublication.EnsurePrepared();
        NativeMethods.InteractionOwnerPublication.EnsurePrepared();
        NativeMethods.InteractionQueryImports.EnsurePrepared();
        NativeMethods.TransitionPublication.EnsurePrepared();
    }
    internal static void PrepareStylePublicationImports() => NativeMethods.TransitionPublication.EnsurePrepared();
    internal static void Throw(int status)
    {
        if (status == -3) throw new OutOfMemoryException("Native DOM synchronization allocation failed.");
        if (status != 0) throw new InvalidOperationException($"Native DOM synchronization failed with status {status}.");
    }
    internal static ulong ReadIdentity(IntPtr node)
    {
        Throw(NativeMethods.DomReadNodeIdentity(node, out var identity));
        return identity;
    }
    internal static NativeDomToken ReadToken(NativeDomDocument document)
    {
        using var borrow = document.Borrow();
        return ReadToken(borrow.Handle);
    }
    internal static ulong ReadIdentity(NativeDomDocument document)
    {
        using var borrow = document.Borrow();
        return ReadIdentity(borrow.Handle);
    }
    internal static NativeDomToken ReadToken(IntPtr node)
    {
        Throw(NativeMethods.DomReadToken(node, out var token));
        return token;
    }
}

internal static class NativeDomCodec
{
    private static readonly UTF8Encoding Strict = new(false, true);
    internal static void Validate(string value)
    {
        if (value.Contains('\0')) throw new ArgumentException("Bound DOM strings cannot contain U+0000.", nameof(value));
        _ = Strict.GetByteCount(value);
    }
    internal static byte[] Encode(string value)
    {
        Validate(value);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.utf8.encode");
#endif
        return Strict.GetBytes(value);
    }
    internal static unsafe string Decode(NativeDomString value)
    {
        if (value.Length > int.MaxValue || value.Data == IntPtr.Zero)
            throw new InvalidOperationException("Invalid native DOM string buffer.");
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.utf8.decode");
#endif
        var result = Strict.GetString(new ReadOnlySpan<byte>((void*)value.Data, (int)value.Length));
        Validate(result);
        return result;
    }
}

internal sealed class NativeDomLease : SafeHandle
{
    private NativeDomLease() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal static NativeDomLease Retain(IntPtr node)
    {
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.lease.wrapper");
#endif
        var result = new NativeDomLease();
        try
        {
            NativeDomSync.Throw(NativeMethods.DomRetainNode(node, out var value)); result.SetHandle(value);
#if AETHER_DOM_FAULT_TESTING
            if (value != IntPtr.Zero) ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.NodeLease);
#endif
            return result;
        }
        catch { result.Dispose(); throw; }
    }
    internal IntPtr Node { get { NativeDomSync.Throw(NativeMethods.DomLeaseGet(this, out var node)); return node; } }
    internal NativeDomToken ReadToken()
    {
        var success = false;
        DangerousAddRef(ref success);
        try
        {
            if (!success) throw new ObjectDisposedException(nameof(NativeDomLease));
            return NativeDomSync.ReadToken(Node);
        }
        finally { if (success) DangerousRelease(); }
    }
    protected override bool ReleaseHandle()
    {
        NativeMethods.DomLeaseDestroy(handle);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.ResourceReleased(ManagedDomFault.Resource.NodeLease);
#endif
        return true;
    }
}
internal sealed class NativeDomSnapshotHandle : SafeHandle
{
    internal NativeDomSnapshotHandle() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal void Initialize(IntPtr value)
    {
        SetHandle(value);
#if AETHER_DOM_FAULT_TESTING
        if (value != IntPtr.Zero) ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.Snapshot);
#endif
    }
    protected override bool ReleaseHandle()
    {
        NativeMethods.DomSnapshotDestroy(handle);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.ResourceReleased(ManagedDomFault.Resource.Snapshot);
#endif
        return true;
    }
}
internal sealed class NativeDomEditHandle : SafeHandle
{
    internal NativeDomEditHandle() : base(IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;
    internal void Initialize(IntPtr value)
    {
        SetHandle(value);
#if AETHER_DOM_FAULT_TESTING
        if (value != IntPtr.Zero) ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.Edit);
#endif
    }
    protected override bool ReleaseHandle()
    {
        NativeMethods.DomEditDestroy(handle);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.ResourceReleased(ManagedDomFault.Resource.Edit);
#endif
        return true;
    }
}

internal sealed record NativeDomNodeData(IntPtr Handle, ulong Identity, ulong OwnerIdentity,
    ulong DisconnectSerial, int Type, int State, bool Alive, bool Attached, long ParentIndex,
    long StyleNodeId, string Tag, string Text, string? AuthoredValue, string? RuntimeValue,
    Dictionary<string, string> Attributes, int[] Children, ulong[] StateBitWriteSerial);

internal sealed class NativeDomSnapshot : IDisposable
{
    private readonly NativeDomSnapshotHandle _handle;
    internal NativeDomToken Token { get; }
    internal NativeDomNodeData[] Nodes { get; }
    private NativeDomSnapshot(NativeDomSnapshotHandle handle, NativeDomToken token, NativeDomNodeData[] nodes)
    { _handle = handle; Token = token; Nodes = nodes; }
    internal static NativeDomSnapshot Acquire(NativeDomDocument document, IReadOnlyList<NativeDomLease>? known = null)
    {
        using var borrow = document.Borrow();
        return Acquire(borrow.Handle, known);
    }
    internal static unsafe NativeDomSnapshot Acquire(IntPtr document, IReadOnlyList<NativeDomLease>? known = null)
    {
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.snapshot.handle");
#endif
        var handle = new NativeDomSnapshotHandle();
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.snapshot.known-array");
#endif
        var raw = new IntPtr[known?.Count ?? 0];
        var acquired = 0;
        try
        {
            for (var i = 0; i < raw.Length; i++)
            {
                var success = false;
                known![i].DangerousAddRef(ref success);
                if (!success) throw new ObjectDisposedException(nameof(NativeDomLease));
                acquired++;
                raw[i] = known[i].DangerousGetHandle();
            }
            fixed (IntPtr* pointers = raw)
            {
                NativeDomSync.Throw(NativeMethods.DomAcquireSnapshot(document, pointers, (nuint)raw.Length, out var value));
                handle.Initialize(value);
            }
            NativeDomSync.Throw(NativeMethods.DomSnapshotToken(handle, out var token));
            NativeDomSync.Throw(NativeMethods.DomSnapshotCount(handle, out var count));
            if (count > int.MaxValue) throw new OverflowException("Native DOM snapshot is too large.");
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.Before("interop.snapshot.nodes-array");
#endif
            var nodes = new NativeDomNodeData[(int)count];
            for (var i = 0; i < nodes.Length; i++)
            {
                NativeDomSync.Throw(NativeMethods.DomSnapshotNode(handle, (nuint)i, out var node));
                if (node.AttributeCount > int.MaxValue || node.ChildCount > int.MaxValue || node.ParentIndex >= nodes.Length || node.ParentIndex < -1)
                    throw new InvalidOperationException("Invalid native DOM snapshot indices.");
#if AETHER_DOM_FAULT_TESTING
                ManagedDomFault.Before("interop.snapshot.attributes");
#endif
                var attributes = new Dictionary<string, string>((int)node.AttributeCount, StringComparer.Ordinal);
                for (nuint a = 0; a < node.AttributeCount; a++)
                {
                    NativeDomSync.Throw(NativeMethods.DomSnapshotAttribute(handle, (nuint)i, a, out var attribute));
                    attributes.Add(NativeDomCodec.Decode(attribute.Name), NativeDomCodec.Decode(attribute.Value));
                }
#if AETHER_DOM_FAULT_TESTING
                ManagedDomFault.Before("interop.snapshot.children-array");
#endif
                var children = new int[(int)node.ChildCount];
                for (var c = 0; c < children.Length; c++)
                {
                    NativeDomSync.Throw(NativeMethods.DomSnapshotChild(handle, (nuint)i, (nuint)c, out var index));
                    if (index >= count) throw new InvalidOperationException("Invalid native DOM child index.");
                    children[c] = (int)index;
                }
#if AETHER_DOM_FAULT_TESTING
                ManagedDomFault.Before("interop.snapshot.stamps-array");
#endif
                var stamps = new ulong[32];
                for (var bit = 0; bit < stamps.Length; bit++) stamps[bit] = node.StateBitWriteSerial[bit];
#if AETHER_DOM_FAULT_TESTING
                ManagedDomFault.Before("interop.snapshot.node-record");
#endif
                nodes[i] = new(node.Node, node.Identity, node.OwnerIdentity, node.DisconnectSerial, node.Type, node.State,
                    node.Alive != 0, node.Attached != 0, node.ParentIndex, node.StyleNodeId, NativeDomCodec.Decode(node.Tag),
                    NativeDomCodec.Decode(node.Text), node.AuthoredValuePresent != 0 ? NativeDomCodec.Decode(node.AuthoredValue) : null,
                    node.RuntimeValuePresent != 0 ? NativeDomCodec.Decode(node.RuntimeValue) : null, attributes, children, stamps);
            }
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.Before("interop.snapshot.wrapper");
#endif
            return new(handle, token, nodes);
        }
        catch { handle.Dispose(); throw; }
        finally { for (var i = 0; i < acquired; i++) known![i].DangerousRelease(); }
    }
    public void Dispose() => _handle.Dispose();
}

internal enum NativeDomEditKind { SetAttribute = 1, RemoveAttribute, SetState, SetRuntimeValue, SetText, InsertChild, RemoveChild, ClearChildren, InsertSubtree, ReplaceChildren, MaterializeForest, LegacySetAttribute = 12, MaterializeDocument = 13 }
internal sealed record NativeDomNodeSpecData(int Type, int State, long ParentIndex, string Tag, string Text,
    string? RuntimeValue, IReadOnlyDictionary<string, string> Attributes, IntPtr ExistingNode = default);
internal readonly record struct NativeDomEditRequest(NativeDomEditKind Kind, IntPtr Target, IntPtr Child = default,
    long Index = -1, string? Name = null, string? Value = null, int State = 0, bool ValuePresent = false,
    IReadOnlyList<NativeDomNodeSpecData>? Nodes = null);

internal sealed class NativeDomEdit : IDisposable
{
    private readonly NativeDomEditHandle _handle;
    private NativeDomEdit(NativeDomEditHandle handle) => _handle = handle;
    internal static NativeDomEdit Prepare(NativeDomToken expected, NativeDomEditRequest request)
    {
        NativeMethods.DomEditPublication.EnsurePrepared();
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.edit.handle");
#endif
        var handle = new NativeDomEditHandle();
        try
        {
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.Before("interop.edit.arena-wrapper");
#endif
            using var arena = new NativeDomArena();
            var spec = new NativeDomEditSpec
            {
                Kind = (int)request.Kind, Target = request.Target, Child = request.Child, Index = request.Index,
                Name = arena.String(request.Name ?? string.Empty), Value = arena.String(request.Value ?? string.Empty),
                State = request.State, ValuePresent = request.ValuePresent ? 1 : 0,
            };
            if (request.Nodes is { } nodes)
            {
#if AETHER_DOM_FAULT_TESTING
                ManagedDomFault.Before("interop.edit.specs-array");
#endif
                var specs = new NativeDomNodeSpec[nodes.Count];
                for (var i = 0; i < nodes.Count; i++)
                {
                    var node = nodes[i];
#if AETHER_DOM_FAULT_TESTING
                    ManagedDomFault.Before("interop.edit.attributes-array");
#endif
                    var attributes = new NativeDomAttribute[node.Attributes.Count];
                    var a = 0;
                    foreach (var pair in node.Attributes) attributes[a++] = new() { Name = arena.String(pair.Key), Value = arena.String(pair.Value) };
                    specs[i] = new()
                    {
                        Type = node.Type, State = node.State, ParentIndex = node.ParentIndex,
                        Tag = arena.String(node.Tag), Text = arena.String(node.Text),
                        RuntimeValue = arena.String(node.RuntimeValue ?? string.Empty), RuntimeValuePresent = node.RuntimeValue is null ? 0 : 1,
                        Attributes = arena.Array<NativeDomAttribute>(attributes), AttributeCount = (nuint)attributes.Length, ExistingNode = node.ExistingNode,
                    };
                }
                spec.Nodes = arena.Array<NativeDomNodeSpec>(specs); spec.NodeCount = (nuint)specs.Length;
            }
            NativeDomSync.Throw(NativeMethods.DomPrepareEdit(in expected, in spec, out var value));
            handle.Initialize(value);
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.Before("interop.edit.wrapper");
#endif
            return new(handle);
        }
        catch { handle.Dispose(); throw; }
    }
    internal IntPtr CreatedNode(int index) { NativeDomSync.Throw(NativeMethods.DomEditCreatedNode(_handle, (nuint)index, out var node)); return node; }
    internal void Validate() => NativeDomSync.Throw(NativeMethods.DomEditPublication.Validate(_handle));
    internal NativeDomToken Commit() { NativeDomSync.Throw(NativeMethods.DomEditPublication.Commit(_handle, out var token)); return token; }
    public void Dispose() => _handle.Dispose();
}

internal sealed class NativeDomArena : IDisposable
{
    private readonly List<IntPtr> _allocations = [];
    private IntPtr Allocate(int length)
    {
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.BeforeListAdd(_allocations, "interop.arena.ensure-capacity");
#endif
        _allocations.EnsureCapacity(_allocations.Count + 1);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Before("interop.arena.native-buffer");
#endif
        var pointer = Marshal.AllocHGlobal(length);
        _allocations.Add(pointer);
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.NativeBuffer);
#endif
        return pointer;
    }
    internal NativeDomString String(string text)
    {
        var bytes = NativeDomCodec.Encode(text);
        if (bytes.Length == 0) return default;
        var pointer = Allocate(bytes.Length);
        Marshal.Copy(bytes, 0, pointer, bytes.Length);
        return new() { Data = pointer, Length = (nuint)bytes.Length };
    }
    internal unsafe IntPtr Array<T>(ReadOnlySpan<T> values) where T : unmanaged
    {
        if (values.IsEmpty) return IntPtr.Zero;
        var pointer = Allocate(checked(values.Length * sizeof(T)));
        values.CopyTo(new Span<T>((void*)pointer, values.Length));
        return pointer;
    }
    public void Dispose()
    {
        foreach (var pointer in _allocations)
        {
            Marshal.FreeHGlobal(pointer);
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.ResourceReleased(ManagedDomFault.Resource.NativeBuffer);
#endif
        }
        _allocations.Clear();
    }
}
