using AetherUI.Core.Native;

namespace AetherUI.Dom;

internal sealed partial class NativeDomBinding
{
    internal PreparedInteraction PrepareInteractionAcquire(NativeInteractionOwner owner, ElementNode element,
        ElementState bit, ulong registry)
    {
        EnsureCurrent();
        return PreparedInteraction.Prepare(this, element,
            NativeInteractionEdit.Acquire(owner, _token, GetHandleCore(element), (int)bit, registry));
    }
    internal PreparedInteraction PrepareInteractionCleanup(ElementNode element, NativeInteractionClaim claim,
        ulong generation, ulong recordId = 0, ulong recordSerial = 0)
    {
        EnsureCurrent();
        return PreparedInteraction.Prepare(this, element,
            NativeInteractionEdit.Cleanup(claim, generation, recordId, recordSerial));
    }

    internal sealed class PreparedInteraction : IDisposable
    {
        private readonly NativeDomBinding _owner;
        private readonly ElementNode _element;
        private readonly NativeInteractionEdit _native;
        private readonly IDomPreparedBookkeeping? _bookkeeping;
        private readonly DocumentVersionChange _version;
        private readonly NativeDomNodeRecord? _record;
        private readonly NativeDomToken _baseToken;
        private readonly ElementState _before, _after;
        private readonly bool _mirror;
        internal NativeInteractionClaim Claim => _native.Claim;
        internal NativeInteractionClaimInfo Info => _native.Candidate.Claim;
        internal bool StateChanged => _native.Candidate.StateChanged != 0;
        private PreparedInteraction(NativeDomBinding owner, ElementNode element, NativeInteractionEdit native,
            IDomPreparedBookkeeping? bookkeeping, DocumentVersionChange version, NativeDomNodeRecord? record)
        {
            _owner = owner; _element = element; _native = native; _bookkeeping = bookkeeping;
            _version = version; _record = record; _baseToken = owner._token;
            _before = element.StateCore; _after = (ElementState)native.Candidate.AfterState;
            _mirror = native.Candidate.Applied != 0;
        }
        internal static PreparedInteraction Prepare(NativeDomBinding owner, ElementNode element, NativeInteractionEdit native)
        {
            IDomPreparedBookkeeping? bookkeeping = null;
            try
            {
                owner.CheckOwner(); owner._synchronizing = true;
                var candidate = native.Candidate;
                var record = element.NativeRecordCore;
                if (candidate.Applied != 0 && (record is null || !ReferenceEquals(record.Owner, owner) ||
                    record.Identity != candidate.Claim.NodeIdentity || (int)element.StateCore != candidate.BeforeState))
                    throw new InvalidOperationException("Conditional interaction edit has no current managed mirror.");
                var version = default(DocumentVersionChange);
                if (candidate.StateChanged != 0)
                {
                    version = Node.PrepareVersions(element.OwnerDocumentCore, 1, false);
                    bookkeeping = owner._host.Prepare(new(DomEditKind.SetState, element, State: (ElementState)candidate.AfterState), import: false);
                }
                return new(owner, element, native, bookkeeping, version, record);
            }
            catch { owner._synchronizing = false; bookkeeping?.Dispose(); native.Dispose(); throw; }
        }
        internal void Commit()
        {
            _native.Validate(); _bookkeeping?.Validate();
            if (_owner._token != _baseToken || (_mirror && (_element.StateCore != _before ||
                !ReferenceEquals(_element.NativeRecordCore, _record) || !ReferenceEquals(_record!.Owner, _owner))))
                throw new InvalidOperationException("Interaction mirror changed during preparation.");
            var token = _native.Commit();
            // Native authority, CSS State, stamps, versions and host dirty bookkeeping are
            // one owner-thread publication. No setter, import, allocation or notification.
            if (_mirror)
            {
                if (StateChanged)
                {
                    _element.PublishImportedInputs(_element.AttributesCore, _after, _element.RuntimeValueCore);
                    var changed = (uint)(_before ^ _after);
                    for (var bit = 0; bit < 32; bit++)
                        if ((changed & (1u << bit)) != 0) _record!.StateBitWriteSerial[bit]++;
                    _version.Publish(); _bookkeeping!.Publish();
                }
                _owner._token = token;
            }
        }
        public void Dispose()
        {
            _owner._synchronizing = false;
            _bookkeeping?.Dispose(); _native.Dispose();
        }
    }
}
