using AetherUI.Core.Native;
using AetherUI.Dom;

namespace AetherUI.Runtime;

public sealed partial class UiDocumentHost
{
    private bool _domNotificationPending;
    internal Action? DomSynchronizationProbe { get; set; }

    private IDomHostBookkeeping CreateDomHostBookkeeping(DomInterop candidate) => new DomHostBookkeeping(this, candidate);

    // Pure checks never import, allocate collections or invoke a notification.
    private bool DomInputsNeedImport() => _nativeDom?.BindingCore is { } binding && !binding.HasCurrentInputs;

    private void EnsureDomInputsCurrent()
    {
        if (_nativeDom?.BindingCore is not { } binding) return;
        try
        {
            binding.EnsureCurrent();
            _nativeSynchronizedDocumentVersion = _document!.Version;
            _knownManagedInputVersion = _document.InputVersion;
            _inputReconciliationRequired = false;
        }
        catch
        {
            RejectStyleProjection();
            _styleRetryBlocked = true;
            throw;
        }
    }

    private void DrainDomNotification()
    {
        if (!_domNotificationPending || _disposed) return;
        if (_styleOperationDepth != 0 || _renderDepth != 0) { _renderDeferred = true; return; }
        _domNotificationPending = false;
        RequestRender();
    }

    private sealed class DomHostBookkeeping(UiDocumentHost host, DomInterop dom) : IDomHostBookkeeping
    {
        public IDomPreparedBookkeeping Prepare(DomEdit? edit, bool import) => new Prepared(host, dom, edit, null);
        public IDomPreparedBookkeeping PrepareImport(NativeDomSnapshot snapshot) => new Prepared(host, dom, null, snapshot);
        public IDomPreparedBookkeeping PrepareStructure(DomEdit edit, IReadOnlyList<IntPtr> elements) => new Prepared(host, dom, edit, null, elements);
        public void NotifyMutationCompleted()
        {
            if (host._disposed || !ReferenceEquals(host._nativeDom, dom)) return;
            // Node has completed its compound mirror/version publication before this call.
            host._nativeSynchronizedDocumentVersion = dom.Document.Version;
            host._knownManagedInputVersion = dom.Document.InputVersion;
            host.DrainDomNotification();
        }

        private sealed class Prepared : IDomPreparedBookkeeping
        {
            private readonly UiDocumentHost _host;
            private readonly DomInterop _dom;
            private readonly long _baseInput, _basePublication, _nextInput;
            private readonly long[] _baseInvalidations, _nextInvalidations;
            private readonly HashSet<ElementNode>? _nextDirty;
            private readonly Dictionary<string, string>? _nextValues;
            private readonly bool _affectsHost, _full;
            private readonly StyleRuntimeInterop.PreparedDomIndex? _index;

            internal Prepared(UiDocumentHost host, DomInterop dom, DomEdit? edit, NativeDomSnapshot? snapshot, IReadOnlyList<IntPtr>? elements = null)
            {
                _host = host; _dom = dom;
                if (host._disposed || !ReferenceEquals(host._nativeDom, dom))
                    throw new InvalidOperationException("DOM bookkeeping belongs to an inactive host document.");
                _baseInput = host._hostInputSerial; _basePublication = host._hostPublicationSerial;
                _baseInvalidations = (long[])host._invalidationProduced.Clone();
                _nextInvalidations = (long[])_baseInvalidations.Clone();
                _affectsHost = snapshot is not null
                    ? dom.BindingCore is not { } binding || snapshot.Token.InputEpoch != binding.TokenCore.InputEpoch ||
                      snapshot.Token.StructureEpoch != binding.TokenCore.StructureEpoch
                    : edit is { } operation && (OwnedAttached(operation.Target) ||
                      operation.Child is { } child && OwnedAttached(child) ||
                      operation.SourceParent is { } source && OwnedAttached(source));
                _nextInput = _affectsHost ? checked(_baseInput + 1) : _baseInput;
                if (_affectsHost) _nextInvalidations[0] = checked(_nextInvalidations[0] + 1);
                _full = snapshot is not null || edit?.Kind is not DomEditKind.SetState;
                if (_affectsHost && edit is { Kind: DomEditKind.SetState, Target: ElementNode element })
                {
                    _nextDirty = new(host._stateDirtyElements) { element };
                }
                if (_affectsHost && elements is not null) _index = host._nativeStyleRuntime.PrepareDomIndex(dom.Native, elements);
                // Preserve historical ids. Only the current effective value is saved;
                // never use this cache to reinstall an override during synchronization.
                if (_affectsHost && snapshot is not null)
                {
                    _index = host._nativeStyleRuntime.PrepareDomIndex(snapshot);
                    _nextValues = new(host._uncontrolledInputValues, StringComparer.Ordinal);
                    foreach (var data in snapshot.Nodes)
                        if (data.Alive && data.Attached && data.OwnerIdentity == snapshot.Token.OwnerIdentity &&
                            data.Tag == "input" && data.Attributes.TryGetValue("id", out var id) && id.Length != 0)
                            _nextValues[id] = data.RuntimeValue ?? data.AuthoredValue ?? string.Empty;
                }
                else if (_affectsHost && edit is { Target: ElementNode input } valueEdit && input.TagNameCore == "input" &&
                         (valueEdit.Kind == DomEditKind.SetRuntimeValue ||
                          valueEdit.Kind is DomEditKind.SetAttribute or DomEditKind.RemoveAttribute && valueEdit.Name is "id" or "value"))
                {
                    var id = input.GetAttributeCore("id");
                    var authored = input.GetAttributeCore("value");
                    var runtime = input.RuntimeValueCore;
                    if (valueEdit.Kind == DomEditKind.SetRuntimeValue) runtime = valueEdit.Text;
                    else if (valueEdit.Name == "id") id = valueEdit.Kind == DomEditKind.RemoveAttribute ? null : valueEdit.Text;
                    else authored = valueEdit.Kind == DomEditKind.RemoveAttribute ? null : valueEdit.Text;
                    if (!string.IsNullOrEmpty(id))
                    {
                        _nextValues = new(host._uncontrolledInputValues, StringComparer.Ordinal)
                        { [id] = runtime ?? authored ?? string.Empty };
                    }
                }
                host.DomSynchronizationProbe?.Invoke();
                Validate();
                bool OwnedAttached(Node node) => node.NativeRecordCore is { Attached: true } record &&
                    ReferenceEquals(record.Owner, dom.BindingCore);
            }
            public void Validate()
            {
                _index?.Validate();
                if (_host._disposed || !ReferenceEquals(_host._nativeDom, _dom) ||
                    _host._hostInputSerial != _baseInput || _host._hostPublicationSerial != _basePublication)
                    throw new InvalidOperationException("Host changed while preparing a DOM edit.");
                for (var i = 0; i < _baseInvalidations.Length; i++)
                    if (_host._invalidationProduced[i] != _baseInvalidations[i])
                        throw new InvalidOperationException("Host invalidation changed while preparing a DOM edit.");
            }
            public void Publish()
            {
                if (!_affectsHost) return;
                _index?.Publish();
                _host._hostInputSerial = _nextInput;
                Array.Copy(_nextInvalidations, _host._invalidationProduced, _nextInvalidations.Length);
                _host._invalidation |= VisualInvalidation.Style;
                _host._managedProjectionOutOfDate = true;
                _host._styleRetryBlocked = false;
                _host._forceFullProjectionRecovery |= _full;
                _host._hasPendingPointerTarget = false;
                _host._pendingPointerTarget = null;
                if (_nextDirty is not null) _host._stateDirtyElements = _nextDirty;
                if (_nextValues is not null) _host._uncontrolledInputValues = _nextValues;
                _host._domNotificationPending = true;
            }
            public void Dispose() { }
        }
    }
}
