using AetherUI.Core.Native;

namespace AetherUI.Dom;

// Host implementation must stage serials, dirty sets, lifecycle records and notification
// flags before native commit. These are internal, non-user callbacks. No allocation or
// throwing is permitted in Publish. Import never dispatches notifications.
internal interface IDomHostBookkeeping
{
    IDomPreparedBookkeeping Prepare(DomEdit? edit, bool import);
    IDomPreparedBookkeeping PrepareImport(NativeDomSnapshot snapshot) => Prepare(null, import: true);
    IDomPreparedBookkeeping PrepareStructure(DomEdit edit, IReadOnlyList<IntPtr> elements) => Prepare(edit, import: false);
    void NotifyMutationCompleted();
}
internal interface IDomPreparedBookkeeping : IDisposable
{
    void Validate();
    void Publish();
}

internal sealed class NativeDomNodeRecord(NativeDomBinding owner, ulong identity, NativeDomLease lease)
{
    internal NativeDomBinding Owner = owner;
    internal readonly ulong Identity = identity;
    internal readonly NativeDomLease Lease = lease;
    internal readonly IntPtr Handle = lease.Node;
    internal ulong DisconnectSerial;
    internal bool Attached;
    internal long StyleNodeId;
    internal ulong[] StateBitWriteSerial =
#if AETHER_DOM_FAULT_TESTING
        ManagedDomFault.Array<ulong>(32, "dom.record.stamps");
#else
        new ulong[32];
#endif
}

/// <summary>
/// Opt-in DOM mirror adapter. Production host activation additionally requires its native
/// exclusive binding/author slot and prepared lifecycle bookkeeping; Parse does not enable it.
/// </summary>
internal sealed partial class NativeDomBinding : IDomBinding, IDisposable
{
    [ThreadStatic] private static ThreadBindings? _thread;
    private sealed class ThreadBindings
    {
        internal Dictionary<ulong, WeakReference<NativeDomBinding>> Owners =
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.Dictionary<ulong, WeakReference<NativeDomBinding>>(EqualityComparer<ulong>.Default, "dom.registry.owners");
#else
            [];
#endif
        internal Dictionary<ulong, WeakReference<Node>> Nodes =
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.Dictionary<ulong, WeakReference<Node>>(EqualityComparer<ulong>.Default, "dom.registry.nodes");
#else
            [];
#endif
    }
    private readonly DomInterop _interop;
    private readonly IDomHostBookkeeping _host;
    private readonly int _ownerThread = Environment.CurrentManagedThreadId;
    private readonly ThreadBindings _registry;
    private Dictionary<ulong, WeakReference<Node>> _nodes =
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.Dictionary<ulong, WeakReference<Node>>(EqualityComparer<ulong>.Default, "dom.binding.nodes");
#else
            [];
#endif
    private Dictionary<IntPtr, WeakReference<Node>> _handles =
#if AETHER_DOM_FAULT_TESTING
            ManagedDomFault.Dictionary<IntPtr, WeakReference<Node>>(EqualityComparer<IntPtr>.Default, "dom.binding.handles");
#else
            [];
#endif
    private NativeDomToken _token;
    private bool _disposed;
    private bool _synchronizing;
    private IDomHostBookkeeping[]? _pendingOtherHosts;
    internal NativeDomToken TokenCore => _token;
    internal bool HasCurrentInputs
    {
        get
        {
            if (_disposed || _ownerThread != Environment.CurrentManagedThreadId) return false;
            try
            {
                using var borrow = Native.Borrow();
                return NativeMethods.DomReadToken(borrow.Handle, out var current) == 0 && current.StickyStatus == 0 && current == _token;
            }
            catch (ObjectDisposedException) { return false; }
        }
    }
    internal DocumentNode Document => _interop.Document;
    internal NativeDomDocument Native => _interop.Native;

    internal NativeDomBinding(DomInterop interop, IDomHostBookkeeping host)
    {
        _interop = interop;
        _host = host;
        if (_thread is null)
        {
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.registry.object");
#endif
            _thread = new ThreadBindings();
        }
        _registry = _thread;
        var token = NativeDomSync.ReadToken(Native);
        if (_registry.Owners.TryGetValue(token.OwnerIdentity, out var weak) && weak.TryGetTarget(out var existing) && !existing._disposed)
            throw new InvalidOperationException("A managed DOM binding already owns this document.");

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.binding.owners-copy");
#endif
        var owners = new Dictionary<ulong, WeakReference<NativeDomBinding>>(_registry.Owners);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.binding.owner-weak");
#endif
        var ownerWeak = new WeakReference<NativeDomBinding>(this);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(owners, token.OwnerIdentity, "dom.binding.owner-map-insert-aggregate");
#endif
        owners[token.OwnerIdentity] = ownerWeak;
        // Retain original wrappers from exact initial parse; no reparse or serializer.
        var aliases = interop.InitialNodesCore;
        NativeDomWorkCounters.RecordImportFallback(NativeDomImportFallback.InitialBinding);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.binding.initial-owners-array");
#endif
        var initialOwners = new[] { this };
        Import(initialOwners, aliases);
        _registry.Owners = owners;
    }

    private void CheckOwner()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_ownerThread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("DOM binding accessed on the wrong owner thread.");
        if (_synchronizing) throw new InvalidOperationException("DOM synchronization cannot be reentered.");
    }
    public void EnsureCurrent()
    {
        CheckOwner();
        if (NativeDomSync.ReadToken(Native) == _token) return;
        // Slow path coordinates changed owners on this thread. This discovers a native
        // adoption whichever end is read first, before publishing reciprocal links.

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.owners-list-aggregate");
#endif
        var owners = new List<NativeDomBinding> { this };
        foreach (var weak in _registry.Owners.Values)
            if (weak.TryGetTarget(out var owner) && !ReferenceEquals(owner, this) && !owner._disposed &&
                NativeDomSync.ReadToken(owner.Native) != owner._token) owners.Add(owner);
        NativeDomWorkCounters.RecordImportFallback(NativeDomImportFallback.TokenChanged);
        Import(owners, null);
    }

    internal IntPtr GetHandleCore(Node node)
    {
        var record = node.NativeRecordCore;
        if (record is null || !ReferenceEquals(record.Owner, this) || !ReferenceEquals(node.BindingCore, this))
            throw new InvalidOperationException("The node does not belong to this live native DOM binding.");
        return record.Handle;
    }
    internal Node? GetNodeCore(IntPtr handle) => _handles.TryGetValue(handle, out var weak) && weak.TryGetTarget(out var node) ? node : null;

    private sealed record Imported(Node Node, NativeDomNodeData Data, NativeDomBinding Owner, NativeDomNodeRecord? Record, Node? Parent, Node[] Children);
    private sealed class OwnerImport(NativeDomBinding owner, NativeDomSnapshot snapshot, IDomPreparedBookkeeping bookkeeping,
        Dictionary<ulong, WeakReference<Node>> nodes, Dictionary<IntPtr, WeakReference<Node>> handles, DocumentVersionChange version) : IDisposable
    {
        internal readonly NativeDomBinding Owner = owner;
        internal readonly NativeDomSnapshot Snapshot = snapshot;
        internal readonly IDomPreparedBookkeeping Bookkeeping = bookkeeping;
        internal readonly Dictionary<ulong, WeakReference<Node>> Nodes = nodes;
        internal readonly Dictionary<IntPtr, WeakReference<Node>> Handles = handles;
        internal DocumentVersionChange Version = version;
        public void Dispose() { Bookkeeping.Dispose(); Snapshot.Dispose(); }
    }

    private void Import(IReadOnlyList<NativeDomBinding> owners, IReadOnlyDictionary<IntPtr, Node>? aliases)
    {
        #if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.stages");
        #endif
        var stages = new List<OwnerImport>();
        #if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.lease-list");
        #endif
        var newLeases = new List<NativeDomLease>();
        var committed = false;
        foreach (var owner in owners) owner._synchronizing = true;
        try
        {
            #if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.import.nodes-map");
            #endif
            var all = new Dictionary<ulong, (Node Node, NativeDomNodeData Data, NativeDomBinding Owner)>();
            #if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.import.global-copy");
            #endif
            var nextGlobal = new Dictionary<ulong, WeakReference<Node>>(_registry.Nodes);
            foreach (var owner in owners)
            {
                #if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.import.known-list");
                #endif
                var known = new List<NativeDomLease>();
                foreach (var weak in owner._nodes.Values)
                    if (weak.TryGetTarget(out var wrapper) && wrapper.NativeRecordCore is { } record && ReferenceEquals(record.Owner, owner)) known.Add(record.Lease);
                #if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.import.snapshot-call-aggregate");
                #endif
                var snapshot = NativeDomSnapshot.Acquire(owner.Native, known);
                IDomPreparedBookkeeping? bookkeeping = null;
                try
                {
                    #if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.import.bookkeeping-call-aggregate");
                    #endif
                    bookkeeping = owner._host.PrepareImport(snapshot);
                    var version = default(DocumentVersionChange);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.stage-nodes-map");
#endif
                    var stageNodes = new Dictionary<ulong, WeakReference<Node>>();

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.stage-handles-map");
#endif
                    var stageHandles = new Dictionary<IntPtr, WeakReference<Node>>();

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.owner-stage");
#endif
                    var stage = new OwnerImport(owner, snapshot, bookkeeping, stageNodes, stageHandles, version);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(stages, "dom.import.stage-list-add");
#endif
                    stages.Add(stage);
                    bookkeeping = null;
                }
                catch { bookkeeping?.Dispose(); snapshot.Dispose(); throw; }
                foreach (var data in snapshot.Nodes)
                {
                    // Known leases can name nodes adopted into another owner. Its own
                    // snapshot is authoritative, not a partial view from this registry.
                    if (data.Alive && data.OwnerIdentity != snapshot.Token.OwnerIdentity) continue;
                    if (all.ContainsKey(data.Identity)) continue;
                    Node? node = null;
                    if (nextGlobal.TryGetValue(data.Identity, out var weak)) weak.TryGetTarget(out node);
                    if (node is null && aliases is not null) aliases.TryGetValue(data.Handle, out node);
                    node ??= data.Type == (int)NodeType.Document && data.Identity == snapshot.Token.OwnerIdentity
                        ? owner.Document : CreateNode(data);
                    #if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(all, data.Identity, "dom.import.node-map-add");
                    #endif
                    all.Add(data.Identity, (node, data, owner));
                    if (data.Alive)
                    {
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.global-weak");
#endif
                        var nodeWeak = new WeakReference<Node>(node);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(nextGlobal, data.Identity, "dom.import.global-insert-aggregate");
#endif
                        nextGlobal[data.Identity] = nodeWeak;
                    }
                    else nextGlobal.Remove(data.Identity);
                }
            }
            #if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.import.records-list");
            #endif
            var imported = new List<Imported>(all.Count);
            foreach (var stage in stages)
            {
                var payload = stage.Snapshot.Nodes;
                foreach (var data in payload)
                {
                    if (!all.TryGetValue(data.Identity, out var item) || !ReferenceEquals(item.Owner, stage.Owner) || !ReferenceEquals(item.Data, data)) continue;
                    var node = item.Node;
                    NativeDomNodeRecord? record = null;
                    if (data.Alive)
                    {
                        record = node.NativeRecordCore;
                        if (record is null || record.Identity != data.Identity)
                        {
                            #if AETHER_DOM_FAULT_TESTING
                            AetherUI.Core.Native.ManagedDomFault.Before("dom.import.lease-capacity");
                            #endif
                            newLeases.EnsureCapacity(newLeases.Count + 1);
                            var lease = NativeDomLease.Retain(data.Handle); newLeases.Add(lease);
                            #if AETHER_DOM_FAULT_TESTING
                            AetherUI.Core.Native.ManagedDomFault.Before("dom.import.node-record");
                            #endif
                            record = new(stage.Owner, data.Identity, lease);
                        }

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.identity-weak");
#endif
                        var identityWeak = new WeakReference<Node>(node);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(stage.Nodes, data.Identity, "dom.import.identity-add");
#endif
                        stage.Nodes.Add(data.Identity, identityWeak);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.handle-weak");
#endif
                        var handleWeak = new WeakReference<Node>(node);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(stage.Handles, data.Handle, "dom.import.handle-add");
#endif
                        stage.Handles.Add(data.Handle, handleWeak);
                    }
                    #if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.import.children-array");
                    #endif
                    var children = new Node[data.Children.Length];
                    for (var i = 0; i < children.Length; i++) children[i] = Resolve(payload[data.Children[i]].Identity);
                    Node? parent = data.ParentIndex < 0 || (!data.Alive && payload[(int)data.ParentIndex].Alive)
                        ? null : Resolve(payload[(int)data.ParentIndex].Identity);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.node-stage");
#endif
                    var nodeStage = new Imported(node, data, stage.Owner, record, parent, children);
                    // List capacity is all.Count; each canonical item is emitted at most once.
                    imported.Add(nodeStage);
                    NativeDomWorkCounters.RecordNodesImported(1);
                    NativeDomWorkCounters.RecordTraversal((ulong)data.Attributes.Count, (ulong)data.Children.Length);
                }
            }
            foreach (var stage in stages)
            {
                if (stage.Owner._token.OwnerIdentity != 0)
                {
                    var treeChanged = stage.Snapshot.Token.StructureEpoch != stage.Owner._token.StructureEpoch || imported.Any(item => ReferenceEquals(item.Owner, stage.Owner) &&
                        (item.Data.Attached || ReferenceEquals(item.Node.OwnerDocumentCore, stage.Owner.Document)) && TreeChanged(item));
                    // Owner changes/removals may leave no current payload in this source snapshot.
                    treeChanged |= stage.Owner._nodes.Any(pair => pair.Value.TryGetTarget(out var node) &&
                        node.OwnerDocumentCore == stage.Owner.Document && !stage.Nodes.ContainsKey(pair.Key));
                    var attachedInputsChanged = stage.Snapshot.Token.InputEpoch != stage.Owner._token.InputEpoch ||
                        stage.Snapshot.Token.StructureEpoch != stage.Owner._token.StructureEpoch;
                    stage.Version = stage.Owner.Document.PrepareVersionChange(treeChanged ? 1 : 0, attachedInputsChanged ? 1 : 0);
                }
                if (NativeDomSync.ReadToken(stage.Owner.Native) != stage.Snapshot.Token)
                    throw new InvalidOperationException("Native DOM changed during snapshot preparation.");
                stage.Bookkeeping.Validate();
            }
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
            DomCommitAllocationLedger.Begin(DomCommitAllocationKind.Import);
            var allocationCommitSucceeded = false;
            try
            {
#endif
            // No callbacks, allocations, virtual node methods or fallible lookups below.
            foreach (var item in imported)
            {
                var node = item.Node; var data = item.Data;
                if (node is ElementNode element) element.PublishImportedInputs(data.Attributes, (ElementState)data.State, data.RuntimeValue);
                else if (node is TextNode text) text.PublishImportedText(data.Text);
                else if (node is CommentNode comment) comment.PublishImportedText(data.Text);
                node.PublishImportedLinks(item.Parent, item.Children, data.Alive ? item.Owner : null);
                node.NativeRecordCore = item.Record;
                if (item.Record is { } record)
                {
                    record.Owner = item.Owner; record.DisconnectSerial = data.DisconnectSerial;
                    record.Attached = data.Attached; record.StyleNodeId = data.StyleNodeId;
                    record.StateBitWriteSerial = data.StateBitWriteSerial;
                }
            }
            _registry.Nodes = nextGlobal;
            foreach (var stage in stages)
            {
                stage.Owner._nodes = stage.Nodes; stage.Owner._handles = stage.Handles;
                stage.Owner._token = stage.Snapshot.Token;
                stage.Version.Publish(); stage.Bookkeeping.Publish();
            }
            committed = true;
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
                allocationCommitSucceeded = true;
            }
            finally { DomCommitAllocationLedger.End(allocationCommitSucceeded); }
#endif

            Node Resolve(ulong identity) => all.TryGetValue(identity, out var found) ? found.Node :
                throw new InvalidOperationException("Snapshot references an uncoordinated native owner.");
        }
        finally
        {
            foreach (var owner in owners) owner._synchronizing = false;
            foreach (var stage in stages) stage.Dispose();
            if (!committed) foreach (var lease in newLeases) lease.Dispose();
        }
    }

    private static bool TreeChanged(Imported item)
    {
        var node = item.Node; var data = item.Data;
        if (!ReferenceEquals(node.ParentCore, item.Parent) || !node.ChildrenCore.SequenceEqual(item.Children)) return true;
        if (node is TextNode text) return text.TextCore != data.Text;
        if (node is CommentNode comment) return comment.TextCore != data.Text;
        if (node is ElementNode element)
            return element.AttributesCore.Count != data.Attributes.Count || element.AttributesCore.Any(pair =>
                !data.Attributes.TryGetValue(pair.Key, out var value) || pair.Value != value);
        return false;
    }

    private static Node CreateNode(NativeDomNodeData data)
    {
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.import.wrapper");
#endif
        return data.Type switch
        {
            (int)NodeType.Element => new ElementNode(data.Tag),
            (int)NodeType.Text => new TextNode(data.Text),
            (int)NodeType.Comment => new CommentNode(data.Text),
            _ => throw new InvalidOperationException("Nested/custom document nodes cannot be synchronized."),
        };
    }

    public IDomPreparedEdit PrepareEdit(DomEdit edit)
    {
        CheckOwner();
        return edit.Target.NativeRecordCore is null
            ? PreparedMaterialization.Prepare(this, edit) : PreparedEdit.Prepare(this, edit);
    }
    public void MutationCompleted()
    {
        CheckOwner();
        var others = _pendingOtherHosts;
        _pendingOtherHosts = null;
        try { _host.NotifyMutationCompleted(); }
        finally { if (others is not null) foreach (var other in others) other.NotifyMutationCompleted(); }
    }

    public void Dispose()
    {
        if (_disposed) return;
        CheckOwner();
        // The interop owns disposal. It imports first while the document is alive;
        // the resulting wrappers remain usable as independent managed data.
        EnsureCurrent();
        foreach (var weak in _nodes.Values)
            if (weak.TryGetTarget(out var node) && ReferenceEquals(node.BindingCore, this))
            { node.BindingCore = null; node.NativeRecordCore = null; }
        _disposed = true;
        _registry.Owners.Remove(_token.OwnerIdentity);
        foreach (var identity in _nodes.Keys) _registry.Nodes.Remove(identity);
        _nodes.Clear(); _handles.Clear();
    }

    private sealed class PreparedEdit : IDomPreparedEdit
    {
        // Implementation below keeps native staging and all new managed references alive
        // until the public Node operation has completed its no-fail mirror publication.
        private readonly NativeDomBinding _owner;
        private readonly DomEdit _edit;
        private readonly NativeDomEdit _native;
        private readonly NativeDomBinding? _source;
        private readonly IDomHostBookkeeping[] _otherHosts;
        private readonly IDomPreparedBookkeeping _bookkeeping;
        private readonly IDomPreparedBookkeeping? _sourceBookkeeping;
        private readonly Dictionary<ulong, WeakReference<Node>>? _global, _nodes, _sourceNodes;
        private readonly Dictionary<IntPtr, WeakReference<Node>>? _handles, _sourceHandles;
        private readonly (Node Node, NativeDomNodeRecord Record)[] _created;
        private readonly NativeDomNodeRecord[] _disconnect, _attach;
        private readonly Node[] _adoptedWrappers;
        private bool _committed;
        private PreparedEdit(NativeDomBinding owner, DomEdit edit, NativeDomEdit native, NativeDomBinding? source,
            IDomPreparedBookkeeping bookkeeping, IDomPreparedBookkeeping? sourceBookkeeping,
            Dictionary<ulong, WeakReference<Node>>? global, Dictionary<ulong, WeakReference<Node>>? nodes,
            Dictionary<IntPtr, WeakReference<Node>>? handles, Dictionary<ulong, WeakReference<Node>>? sourceNodes,
            Dictionary<IntPtr, WeakReference<Node>>? sourceHandles, (Node, NativeDomNodeRecord)[] created,
            NativeDomNodeRecord[] disconnect, NativeDomNodeRecord[] attach)
        {
            _owner = owner; _edit = edit; _native = native; _source = source; _bookkeeping = bookkeeping;
            if (source is not null && edit.Child is not null)
            {
#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.adopted-array-aggregate");
#endif
                _adoptedWrappers = SelfAndDescendants(edit.Child).ToArray();
            }
            else _adoptedWrappers = [];
            if (source is not null)
            {
#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.notifications-array");
#endif
                _otherHosts = [source._host];
            }
            else _otherHosts = [];
            _sourceBookkeeping = sourceBookkeeping; _global = global; _nodes = nodes; _handles = handles;
            _sourceNodes = sourceNodes; _sourceHandles = sourceHandles; _created = created; _disconnect = disconnect; _attach = attach;
        }
        internal static PreparedEdit Prepare(NativeDomBinding owner, DomEdit edit)
        {
            if (edit.Target.NativeRecordCore is not { } target || !ReferenceEquals(target.Owner, owner))
                throw new NotSupportedException("Materializing a new destination around an existing bound child requires the native compound-target operation.");
            NativeDomEdit? native = null;
            IDomPreparedBookkeeping? bookkeeping = null, sourceBookkeeping = null;
            #if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.created-list");
            #endif
            var created = new List<(Node Node, NativeDomNodeRecord Record)>();
            NativeDomBinding? lockedSource = null;
            var locked = false;
            try
            {
                var source = edit.Child?.NativeRecordCore?.Owner;
                if (ReferenceEquals(source, owner)) source = null;
                if (source is not null)
                {
                    source.CheckOwner();
                    if (NativeDomSync.ReadToken(source.Native) != source._token) throw new InvalidOperationException("Source DOM binding is not current.");
                }
                if (NativeDomSync.ReadToken(owner.Native) != owner._token) throw new InvalidOperationException("Destination DOM binding is not current.");
                owner._synchronizing = true; locked = true;
                if (source is not null) { source._synchronizing = true; lockedSource = source; }
                #if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.specs-list");
                #endif
                var specs = new List<NativeDomNodeSpecData>();

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.wrapper-list");
#endif
                var wrappers = new List<Node>();
                var childHandle = edit.Child?.NativeRecordCore?.Handle ?? IntPtr.Zero;
                NativeDomEditKind kind = edit.Kind switch
                {
                    DomEditKind.SetAttribute => NativeDomEditKind.SetAttribute,
                    DomEditKind.RemoveAttribute => NativeDomEditKind.RemoveAttribute,
                    DomEditKind.SetState => NativeDomEditKind.SetState,
                    DomEditKind.SetRuntimeValue => NativeDomEditKind.SetRuntimeValue,
                    DomEditKind.SetText => NativeDomEditKind.SetText,
                    DomEditKind.InsertChild => childHandle == IntPtr.Zero ? NativeDomEditKind.InsertSubtree : NativeDomEditKind.InsertChild,
                    DomEditKind.RemoveChild => NativeDomEditKind.RemoveChild,
                    DomEditKind.ClearChildren => NativeDomEditKind.ClearChildren,
                    DomEditKind.ReplaceText => NativeDomEditKind.ReplaceChildren,
                    _ => throw new InvalidOperationException("Unknown managed DOM operation."),
                };
                if ((kind is NativeDomEditKind.InsertSubtree or NativeDomEditKind.ReplaceChildren) && edit.Child is not null)
                    Capture(edit.Child, -1);
                var request = new NativeDomEditRequest(kind, target.Handle, childHandle, edit.Index, edit.Name,
                    edit.Text, (int)edit.State, edit.Text is not null, specs);
                #if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.native-prepare-call-aggregate");
                #endif
                native = NativeDomEdit.Prepare(owner._token, request);
                Dictionary<ulong, WeakReference<Node>>? global = null, nodes = null, sourceNodes = null;
                Dictionary<IntPtr, WeakReference<Node>>? handles = null, sourceHandles = null;
                if (wrappers.Count != 0 || source is not null)
                {

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.global-copy");
#endif
                    global = new(owner._registry.Nodes);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.owner-copy");
#endif
                    nodes = new(owner._nodes);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.handles-copy");
#endif
                    handles = new(owner._handles);
                    if (source is not null)
                    {
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.source-nodes-copy");
#endif
                        sourceNodes = new(source._nodes);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.source-handles-copy");
#endif
                        sourceHandles = new(source._handles);
                    }
                }
                foreach (var wrapper in wrappers)
                {
                    #if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.created-capacity");
                    #endif
                    created.EnsureCapacity(created.Count + 1);
                    var handle = native.CreatedNode(created.Count);
                    var identity = NativeDomSync.ReadIdentity(handle);
                    var lease = NativeDomLease.Retain(handle);
                    NativeDomNodeRecord record;
                    try
                    {
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.node-record");
#endif
                        record = new(owner, identity, lease);
                    }
                    catch { lease.Dispose(); throw; }
                    created.Add((wrapper, record));

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.global-weak");
#endif
                    var globalWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(global!, identity, "dom.edit.global-add");
#endif
                    global!.Add(identity, globalWeak);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.owner-weak");
#endif
                    var ownerWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(nodes!, identity, "dom.edit.owner-add");
#endif
                    nodes!.Add(identity, ownerWeak);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.handle-weak");
#endif
                    var handleWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(handles!, handle, "dom.edit.handle-add");
#endif
                    handles!.Add(handle, handleWeak);
                }
                #if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.disconnect-list");
                #endif
                var disconnect = new List<NativeDomNodeRecord>();

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.attach-list");
#endif
                var attach = new List<NativeDomNodeRecord>();
                if (edit.Kind == DomEditKind.RemoveChild && edit.Child is not null) AddRecords(edit.Child, disconnect);
                if (edit.Kind is DomEditKind.ClearChildren or DomEditKind.ReplaceText)
                    foreach (var child in edit.Target.ChildrenCore) AddRecords(child, disconnect);
                if (edit.Kind == DomEditKind.InsertChild && edit.Child is not null && childHandle != IntPtr.Zero)
                {
                    AddRecords(edit.Child, attach);
                    if (source is not null || edit.Child.NativeRecordCore!.Attached && !target.Attached)
                    {
#if AETHER_DOM_FAULT_TESTING
                        AetherUI.Core.Native.ManagedDomFault.BeforeListRange(disconnect, attach.Count, "dom.edit.disconnect-range");
#endif
                        disconnect.AddRange(attach);
                    }
                    if (source is not null)
                    {
                        foreach (var wrapper in SelfAndDescendants(edit.Child))
                        {
                            var record = wrapper.NativeRecordCore!;
                            sourceNodes!.Remove(record.Identity); sourceHandles!.Remove(record.Handle);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.adopt-node-weak");
#endif
                            var adoptedWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(nodes!, record.Identity, "dom.edit.adopt-node-add");
#endif
                            nodes!.Add(record.Identity, adoptedWeak);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.adopt-handle-weak");
#endif
                            var adoptedHandleWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(handles!, record.Handle, "dom.edit.adopt-handle-add");
#endif
                            handles!.Add(record.Handle, adoptedHandleWeak);
                        }
                    }
                }
                foreach (var record in disconnect) if (record.DisconnectSerial == ulong.MaxValue) throw new OverflowException("DOM disconnect serial exhausted.");
                #if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.bookkeeping-call-aggregate");
                #endif
                bookkeeping = PrepareBookkeeping(owner, edit, created);
                sourceBookkeeping = source is null ? null : PrepareBookkeeping(source, edit, created);
                #if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.created-array");
                #endif
                var createdArray = created.ToArray();

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.disconnect-array");
#endif
                var disconnectArray = disconnect.ToArray();

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.attach-array");
#endif
                var attachArray = attach.ToArray();

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.edit.result");
#endif
                return new(owner, edit, native, source, bookkeeping, sourceBookkeeping, global, nodes, handles,
                    sourceNodes, sourceHandles, createdArray, disconnectArray, attachArray);

                void Capture(Node node, int parent)
                {
                    if (node.BindingCore is not null) throw new InvalidOperationException("A materialized subtree cannot hide bound descendants.");
                    var type = node.GetType();
                    if (type != typeof(ElementNode) && type != typeof(TextNode) && type != typeof(CommentNode))
                        throw new InvalidOperationException("Nested documents and custom nodes cannot be synchronized.");
                    var element = node as ElementNode;
                    var index = specs.Count;
                    specs.Add(new((int)node.NodeType, (int)(element?.StateCore ?? 0), parent, element?.TagNameCore ?? string.Empty,
                        node is TextNode text ? text.TextCore : node is CommentNode comment ? comment.TextCore : string.Empty,
                        element?.RuntimeValueCore, element?.AttributesCore ?? new Dictionary<string, string>(StringComparer.Ordinal)));
                    #if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(wrappers, "dom.edit.wrapper-list-add");
                    #endif
                    wrappers.Add(node);
                    foreach (var child in node.ChildrenCore) Capture(child, index);
                }
            }
            catch
            {
                if (locked) owner._synchronizing = false;
                if (lockedSource is not null) lockedSource._synchronizing = false;
                sourceBookkeeping?.Dispose(); bookkeeping?.Dispose();
                foreach (var pair in created) pair.Record.Lease.Dispose();
                native?.Dispose(); throw;
            }
        }
        private static IEnumerable<Node> SelfAndDescendants(Node node)
        {
            yield return node;
            foreach (var child in node.DescendantsCore()) yield return child;
        }
        private static void AddRecords(Node node, List<NativeDomNodeRecord> records)
        {
            foreach (var wrapper in SelfAndDescendants(node))
                records.Add(wrapper.NativeRecordCore ?? throw new InvalidOperationException("Missing bound descendant identity."));
        }
        public void Commit()
        {
            _native.Validate();
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
            DomCommitAllocationLedger.MarkPhase(DomCommitAllocationPhase.NativeValidation);
#endif
            _bookkeeping.Validate(); _sourceBookkeeping?.Validate();
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
            DomCommitAllocationLedger.MarkPhase(DomCommitAllocationPhase.BookkeepingValidation);
#endif
            var token = _native.Commit();
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
            DomCommitAllocationLedger.MarkPhase(DomCommitAllocationPhase.NativeCommit);
#endif
            // All following operations are pure scalar writes/swaps over prepared storage.
            foreach (var pair in _created)
            {
                pair.Node.NativeRecordCore = pair.Record; pair.Node.BindingCore = _owner;
                pair.Record.Attached = _edit.Target.NativeRecordCore!.Attached;
                pair.Record.StyleNodeId = _owner.Native.GetStyleNodeId(pair.Record.Handle);
            }
            foreach (var record in _disconnect) { record.DisconnectSerial++; record.Attached = false; }
            foreach (var record in _attach)
            {
                record.Owner = _owner; record.Attached = _edit.Target.NativeRecordCore!.Attached;
                record.StyleNodeId = _owner.Native.GetStyleNodeId(record.Handle);
            }
            if (_source is not null && _edit.Child is not null)
            {
                // The moved wrapper array is staged before native commit. Dictionary.Values
                // would lazily allocate its collection on the newly prepared dictionary.
                foreach (var wrapper in _adoptedWrappers) wrapper.BindingCore = _owner;
            }
            if (_edit.Kind == DomEditKind.SetState && _edit.Target is ElementNode element)
            {
                var changed = (uint)(element.StateCore ^ _edit.State);
                var stamps = element.NativeRecordCore!.StateBitWriteSerial;
                for (var bit = 0; bit < 32; bit++) if ((changed & (1u << bit)) != 0) stamps[bit]++;
            }
            if (_global is not null) _owner._registry.Nodes = _global;
            if (_nodes is not null) _owner._nodes = _nodes;
            if (_handles is not null) _owner._handles = _handles;
            _owner._token = token;
            if (_source is not null)
            {
                _source._nodes = _sourceNodes!; _source._handles = _sourceHandles!;
                _source._token = NativeDomSync.ReadToken(_source.Native);
            }
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
            DomCommitAllocationLedger.MarkPhase(DomCommitAllocationPhase.ManagedMaps);
#endif
            _bookkeeping.Publish(); _sourceBookkeeping?.Publish();
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
            DomCommitAllocationLedger.MarkPhase(DomCommitAllocationPhase.BookkeepingPublish);
#endif
            _owner._pendingOtherHosts = _otherHosts;
            _committed = true;
        }
        public void Dispose()
        {
            _owner._synchronizing = false;
            if (_source is not null) _source._synchronizing = false;
            _sourceBookkeeping?.Dispose(); _bookkeeping.Dispose();
            if (!_committed) foreach (var pair in _created) pair.Record.Lease.Dispose();
            _native.Dispose();
        }
    }
}
