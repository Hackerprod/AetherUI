using AetherUI.Core.Native;

namespace AetherUI.Dom;

internal sealed partial class NativeDomBinding
{
    // This constructor prepares only private owner/mapping state. It must never
    // import into the caller's Document or publish its binding before commit.
    private NativeDomBinding(NativeDomDocument native, DocumentNode document, ThreadBindings registry,
        out NativeDomNodeRecord rootRecord)
    {
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.interop-call-aggregate");
#endif
        _interop = DomInterop.CreateStaged(native, document);
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.bookkeeping-wrapper");
#endif
        _host = new ImplicitDocumentBookkeeping(document);
        _registry = registry;
        _token = NativeDomSync.ReadToken(native);
        using var borrow = native.Borrow();
        var identity = NativeDomSync.ReadIdentity(borrow.Handle);
        var lease = NativeDomLease.Retain(borrow.Handle);
        try
        {
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.root-record");
#endif
            rootRecord = new NativeDomNodeRecord(this, identity, lease)
            {
                Attached = true,
                StyleNodeId = native.GetStyleNodeId(borrow.Handle),
            };

#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.root-weak");
#endif
            var rootWeak = new WeakReference<Node>(document);

#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(_nodes, identity, "dom.implicit.root-node-add");
#endif
            _nodes.Add(identity, rootWeak);

#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.root-handle-weak");
#endif
            var handleWeak = new WeakReference<Node>(document);

#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(_handles, borrow.Handle, "dom.implicit.root-handle-add");
#endif
            _handles.Add(borrow.Handle, handleWeak);
        }
        catch { lease.Dispose(); throw; }
    }

    private static IDomPreparedEdit PrepareImplicitDocument(NativeDomBinding completionOwner, DomEdit edit, DocumentNode document)
    {
        var native = NativeDomDocument.CreateEmpty();
        NativeDomNodeRecord? rootRecord = null;
        PreparedMaterialization? editCandidate = null;
        try
        {
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.binding");
#endif
            var destination = new NativeDomBinding(native, document, completionOwner._registry, out rootRecord);
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.owners-map");
#endif
            var owners = new Dictionary<ulong, WeakReference<NativeDomBinding>>(completionOwner._registry.Owners.Count + 1);
            foreach (var pair in completionOwner._registry.Owners)
                if (pair.Value.TryGetTarget(out var live) && !live._disposed) owners.Add(pair.Key, pair.Value);

#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.owner-weak");
#endif
            var ownerWeak = new WeakReference<NativeDomBinding>(destination);
            // Owners dictionary capacity is old Count+1, and pruning only shrinks it.
            owners.Add(destination._token.OwnerIdentity, ownerWeak);
            editCandidate = PreparedMaterialization.PrepareCore(destination, edit, document, document: true);
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.notification-array-aggregate");
#endif
            var notifications = editCandidate.NotificationsExcept(completionOwner);
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.implicit.result");
#endif
            return new ImplicitDocumentEdit(completionOwner, destination, rootRecord, owners, editCandidate, notifications);
        }
        catch
        {
            // The edit pins all domains until destroyed. Retire the private root
            // only after destroying uncommitted provisional nodes/registries.
            editCandidate?.Dispose();
            rootRecord?.Lease.Dispose();
            native.Dispose();
            throw;
        }
    }

    private sealed class ImplicitDocumentEdit(NativeDomBinding completionOwner, NativeDomBinding destination,
        NativeDomNodeRecord rootRecord, Dictionary<ulong, WeakReference<NativeDomBinding>> owners,
        PreparedMaterialization candidate, IDomHostBookkeeping[] notifications) : IDomPreparedEdit
    {
        private bool _committed;
        public void Commit()
        {
            candidate.Commit();
            // Native commit plus prepared child/owner mappings succeeded. These
            // remaining publications are references/scalars only, without callbacks.
            destination.Document.NativeRecordCore = rootRecord;
            destination.Document.BindingCore = destination;
            destination._interop.PublishStagedBinding(destination);
            destination._registry.Owners = owners;
            destination._pendingOtherHosts = null;
            // Node.InsertCore retains the original source binding until its final
            // safe point. Deliver destination notification through that binding,
            // after Node has published both sides' links and version counters.
            completionOwner._pendingOtherHosts = notifications;
            _committed = true;
        }
        public void Dispose()
        {
            candidate.Dispose();
            if (!_committed)
            {
                rootRecord.Lease.Dispose();
                destination.Native.Dispose();
            }
        }
    }

    private sealed class ImplicitDocumentBookkeeping(DocumentNode document) : IDomHostBookkeeping
    {
        private static readonly EmptyPrepared Instance = new();
        public IDomPreparedBookkeeping Prepare(DomEdit? edit, bool import) => Instance;
        public void NotifyMutationCompleted() => document.NotifyCommittedInputs();
        private sealed class EmptyPrepared : IDomPreparedBookkeeping
        {
            public void Validate() { }
            public void Publish() { }
            public void Dispose() { }
        }
    }
}
