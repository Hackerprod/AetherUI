using AetherUI.Core.Native;

namespace AetherUI.Dom;

internal sealed partial class NativeDomBinding
{
    internal void EnsureMaterializationInputs(Node target, Node child)
    {
        // This barrier runs before Node computes source links, indices or versions.
        // Existing subtree roots are opaque: only their owners are collected here.
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.freshness-owners");
#endif
        var owners = new HashSet<IDomBinding>();
        CollectOwners(MaterializationRoot(target));
        CollectOwners(child);
        owners.Add(this);
        foreach (var owner in owners) owner.EnsureCurrent();
        void CollectOwners(Node node)
        {
            if (node.BindingCore is { } owner) { owners.Add(owner); return; }
            foreach (var descendant in node.ChildrenCore) CollectOwners(descendant);
        }
    }
    private static Node MaterializationRoot(Node target)
    {
        while (target.ParentCore is { } parent) target = parent;
        return target;
    }

    private sealed class PreparedMaterialization : IDomPreparedEdit
    {
        private sealed record Domain(NativeDomBinding Owner, Dictionary<ulong, WeakReference<Node>> Nodes,
            Dictionary<IntPtr, WeakReference<Node>> Handles)
        {
            internal IDomPreparedBookkeeping? Bookkeeping;
        }
        private readonly NativeDomBinding _owner;
        private readonly NativeDomEdit _native;
        private readonly Domain[] _domains;
        private readonly Dictionary<ulong, WeakReference<Node>> _global;
        private readonly (Node Node, NativeDomNodeRecord Record)[] _created, _existing;
        private readonly NativeDomNodeRecord[] _disconnect;
        private readonly IDomHostBookkeeping[] _otherHosts;
        private readonly bool _document;
        private bool _committed;
        private PreparedMaterialization(NativeDomBinding owner, NativeDomEdit native, Domain[] domains,
            Dictionary<ulong, WeakReference<Node>> global, (Node, NativeDomNodeRecord)[] created,
            (Node, NativeDomNodeRecord)[] existing, NativeDomNodeRecord[] disconnect, bool document)
        {
            _owner = owner; _native = native; _domains = domains; _global = global;
            _created = created; _existing = existing; _disconnect = disconnect; _document = document;

#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.notification-array-aggregate");
#endif
            _otherHosts = domains.Where(domain => !ReferenceEquals(domain.Owner, owner)).Select(domain => domain.Owner._host).ToArray();
        }
        internal static IDomPreparedEdit Prepare(NativeDomBinding owner, DomEdit edit)
        {
            if (edit.Kind != DomEditKind.InsertChild || edit.Child is null)
                throw new InvalidOperationException("Materialization requires a compound insertion.");
            var root = MaterializationRoot(edit.Target);
            if (root is DocumentNode document) return PrepareImplicitDocument(owner, edit, document);
            return PrepareCore(owner, edit, root, document: false);
        }
        internal static PreparedMaterialization PrepareCore(NativeDomBinding owner, DomEdit edit, Node root, bool document)
        {
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.specs");
#endif
            var specs = new List<NativeDomNodeSpecData>();
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.wrappers");
#endif
            var wrappers = new List<Node>();
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.existing-roots");
#endif
            var existingRoots = new List<Node>();
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.owners-aggregate");
#endif
            var owners = new HashSet<NativeDomBinding> { owner };
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.empty-attributes");
#endif
            var emptyAttributes = new Dictionary<string, string>(StringComparer.Ordinal);
            if (document) CaptureChildren(root, -1); else Capture(root, -1);
            foreach (var domain in owners)
            {
                domain.CheckOwner();
                if (NativeDomSync.ReadToken(domain.Native) != domain._token)
                    throw new InvalidOperationException("Materialization inputs were not reconciled before staging.");
            }
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.domains");
#endif
            var domains = new List<Domain>();
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.created");
#endif
            var created = new List<(Node Node, NativeDomNodeRecord Record)>();
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.existing");
#endif
            var existing = new List<(Node Node, NativeDomNodeRecord Record)>();
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.disconnect");
#endif
            var disconnect = new List<NativeDomNodeRecord>();
            NativeDomEdit? native = null;
            foreach (var domain in owners) domain._synchronizing = true;
            try
            {
                using (var borrow = owner.Native.Borrow())
                    native = NativeDomEdit.Prepare(owner._token, new(document ? NativeDomEditKind.MaterializeDocument : NativeDomEditKind.MaterializeForest,
                        borrow.Handle, Nodes: specs));
#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.global-map");
#endif
                var global = new Dictionary<ulong, WeakReference<Node>>(owner._registry.Nodes.Count + wrappers.Count + 1);
                foreach (var pair in owner._registry.Nodes)
                    if (pair.Value.TryGetTarget(out _)) global.Add(pair.Key, pair.Value);
                if (document)
                {
#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.root-weak");
#endif
                    var rootWeak = new WeakReference<Node>(root);
                    global.Add(owner._token.OwnerIdentity, rootWeak);
                }
                foreach (var domain in owners)
                {
#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.owner-map-copy");
#endif
                    var nodes = new Dictionary<ulong, WeakReference<Node>>(domain._nodes);
#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.handle-map-copy");
#endif
                    var handles = new Dictionary<IntPtr, WeakReference<Node>>(domain._handles);
#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.domains-capacity");
#endif
                    domains.EnsureCapacity(domains.Count + 1);
#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.domain-object");
#endif
                    domains.Add(new(domain, nodes, handles));
                }
                var destination = domains.Single(domain => ReferenceEquals(domain.Owner, owner));
                for (var i = 0; i < wrappers.Count; i++)
                {
                    if (specs[i].ExistingNode != IntPtr.Zero) continue;
                    var wrapper = wrappers[i];
#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.created-capacity");
#endif
                    created.EnsureCapacity(created.Count + 1);
                    var handle = native.CreatedNode(i);
                    var identity = NativeDomSync.ReadIdentity(handle);
                    var lease = NativeDomLease.Retain(handle);
                    NativeDomNodeRecord record;
                    try
                    {
#if AETHER_DOM_FAULT_TESTING
                        AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.node-record");
#endif
                        record = new(owner, identity, lease);
                    }
                    catch { lease.Dispose(); throw; }
                    created.Add((wrapper, record));

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.global-weak");
#endif
                    var globalWeak = new WeakReference<Node>(wrapper);
                    // Global map preallocated for old entries + all specs + root.
                    global.Add(identity, globalWeak);

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.node-weak");
#endif
                    var nodeWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(destination.Nodes, identity, "dom.materialize.node-add");
#endif
                    destination.Nodes.Add(identity, nodeWeak);

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.handle-weak");
#endif
                    var handleWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(destination.Handles, handle, "dom.materialize.handle-add");
#endif
                    destination.Handles.Add(handle, handleWeak);
                }
                foreach (var subtree in existingRoots)
                {
                    var rootRecord = subtree.NativeRecordCore!;
                    var losesContinuity = rootRecord.Attached || !ReferenceEquals(rootRecord.Owner, owner);
                    foreach (var wrapper in SelfAndDescendants(subtree))
                    {
                        var record = wrapper.NativeRecordCore ?? throw new InvalidOperationException("Missing existing subtree mapping.");
#if AETHER_DOM_FAULT_TESTING
                        AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(existing, "dom.materialize.existing-add");
#endif
                        existing.Add((wrapper, record));
                        if (losesContinuity)
                        {
                            if (record.DisconnectSerial == ulong.MaxValue) throw new OverflowException("DOM disconnect serial exhausted.");
#if AETHER_DOM_FAULT_TESTING
                            AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(disconnect, "dom.materialize.disconnect-add");
#endif
                            disconnect.Add(record);
                        }
                        if (!ReferenceEquals(record.Owner, owner))
                        {
                            var source = domains.Single(domain => ReferenceEquals(domain.Owner, record.Owner));
                            source.Nodes.Remove(record.Identity); source.Handles.Remove(record.Handle);

#if AETHER_DOM_FAULT_TESTING
                            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.adopt-node-weak");
#endif
                            var nodeWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
                            AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(destination.Nodes, record.Identity, "dom.materialize.adopt-node-add");
#endif
                            destination.Nodes.Add(record.Identity, nodeWeak);

#if AETHER_DOM_FAULT_TESTING
                            AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.adopt-handle-weak");
#endif
                            var handleWeak = new WeakReference<Node>(wrapper);

#if AETHER_DOM_FAULT_TESTING
                            AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(destination.Handles, record.Handle, "dom.materialize.adopt-handle-add");
#endif
                            destination.Handles.Add(record.Handle, handleWeak);
                        }
                    }
                }
                foreach (var domain in domains)
                    domain.Bookkeeping = PrepareBookkeeping(domain.Owner, edit, created, materializedRoots: existingRoots);
#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.domains-array");
#endif
                var domainArray = domains.ToArray();

#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.created-array");
#endif
                var createdArray = created.ToArray();

#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.existing-array");
#endif
                var existingArray = existing.ToArray();

#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.disconnect-array");
#endif
                var disconnectArray = disconnect.ToArray();

#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.result");
#endif
                return new(owner, native, domainArray, global, createdArray, existingArray, disconnectArray, document);
            }
            catch
            {
                foreach (var domain in owners) domain._synchronizing = false;
                foreach (var domain in domains) domain.Bookkeeping?.Dispose();
                foreach (var pair in created) pair.Record.Lease.Dispose();
                native?.Dispose(); throw;
            }

            void Capture(Node node, int parentIndex)
            {
                if (node.BindingCore is not null)
                {
                    var record = node.NativeRecordCore ?? throw new InvalidOperationException("Foreign DOM binding cannot be materialized.");
                    owners.Add(record.Owner);
                    // Existing payload is never copied or even read here. The native edit
                    // borrows that live subtree after the operation's freshness barrier.

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.existing-spec");
#endif
                    var existingSpec = new NativeDomNodeSpecData(0, 0, parentIndex, string.Empty, string.Empty, null, emptyAttributes, record.Handle);

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(specs, "dom.materialize.existing-spec-add");
#endif
                    specs.Add(existingSpec);

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(wrappers, "dom.materialize.existing-wrapper-add");
#endif
                    wrappers.Add(node);

#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(existingRoots, "dom.materialize.existing-root-add");
#endif
                    existingRoots.Add(node);
                    return;
                }
                var type = node.GetType();
                if (type != typeof(ElementNode) && type != typeof(TextNode) && type != typeof(CommentNode))
                    throw new InvalidOperationException("Nested documents and custom nodes cannot be synchronized.");
                var element = node as ElementNode;
                var index = specs.Count;

#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("dom.materialize.new-spec");
#endif
                var newSpec = new NativeDomNodeSpecData((int)node.NodeType, (int)(element?.StateCore ?? 0), parentIndex, element?.TagNameCore ?? string.Empty,
                    node is TextNode text ? text.TextCore : node is CommentNode comment ? comment.TextCore : string.Empty,
                    element?.RuntimeValueCore, element?.AttributesCore ?? emptyAttributes);

#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(specs, "dom.materialize.new-spec-add");
#endif
                specs.Add(newSpec);

#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.BeforeListAdd(wrappers, "dom.materialize.new-wrapper-add");
#endif
                wrappers.Add(node);
                CaptureChildren(node, index);
            }
            void CaptureChildren(Node node, int parentIndex)
            {
                if (ReferenceEquals(node, edit.Target))
                {
                    var position = 0;
                    foreach (var child in node.ChildrenCore)
                    {
                        if (ReferenceEquals(child, edit.Child)) continue;
                        if (position++ == edit.Index) Capture(edit.Child!, parentIndex);
                        Capture(child, parentIndex);
                    }
                    if (position == edit.Index) Capture(edit.Child!, parentIndex);
                }
                else foreach (var child in node.ChildrenCore)
                    if (!ReferenceEquals(child, edit.Child)) Capture(child, parentIndex);
            }
        }
        private static IEnumerable<Node> SelfAndDescendants(Node node)
        {
            yield return node;
            foreach (var child in node.DescendantsCore()) yield return child;
        }
        internal IDomHostBookkeeping[] NotificationsExcept(NativeDomBinding owner) =>
            _domains.Where(domain => !ReferenceEquals(domain.Owner, owner)).Select(domain => domain.Owner._host).ToArray();
        public void Commit()
        {
            _native.Validate();
            foreach (var domain in _domains) domain.Bookkeeping!.Validate();
            var token = _native.Commit();
            foreach (var pair in _created)
            {
                pair.Node.BindingCore = _owner; pair.Node.NativeRecordCore = pair.Record;
                pair.Record.Attached = _document;
                pair.Record.StyleNodeId = _owner.Native.GetStyleNodeId(pair.Record.Handle);
            }
            foreach (var pair in _existing)
            {
                pair.Node.BindingCore = _owner; pair.Record.Owner = _owner; pair.Record.Attached = _document;
                pair.Record.StyleNodeId = _owner.Native.GetStyleNodeId(pair.Record.Handle);
            }
            foreach (var record in _disconnect) record.DisconnectSerial++;
            _owner._registry.Nodes = _global;
            foreach (var domain in _domains)
            {
                domain.Owner._nodes = domain.Nodes; domain.Owner._handles = domain.Handles;
                domain.Owner._token = ReferenceEquals(domain.Owner, _owner) ? token : NativeDomSync.ReadToken(domain.Owner.Native);
                domain.Bookkeeping!.Publish();
            }
            _owner._pendingOtherHosts = _otherHosts;
            _committed = true;
        }
        public void Dispose()
        {
            foreach (var domain in _domains) { domain.Owner._synchronizing = false; domain.Bookkeeping?.Dispose(); }
            if (!_committed) foreach (var pair in _created) pair.Record.Lease.Dispose();
            _native.Dispose();
        }
    }
}
