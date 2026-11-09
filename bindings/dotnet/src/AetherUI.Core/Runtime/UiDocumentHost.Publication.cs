using AetherUI.Core.Native;
using AetherUI.Dom;

namespace AetherUI.Runtime;

public sealed partial class UiDocumentHost
{
    private long _hostInputSerial;
    private long _hostPublicationSerial;
    private readonly long[] _invalidationProduced = [1, 0, 0, 0, 0];
    private readonly long[] _invalidationAcknowledged = new long[5];
    private int _styleOperationDepth;
    private int _renderDepth;
    private bool _disposeResourcesPending;
    private bool _managedProjectionOutOfDate = true;
    private bool _styleRetryBlocked;
    private bool _renderDeferred;
    private readonly Queue<Action> _deferredInput = new();
    private bool _drainingInput;
    private NativeStylePublicationToken _publishedStyleToken;
    private long _publishedDocumentVersion;
    private long _nativeSynchronizedDocumentVersion;
    // Fault probes are internal and unset in production. They run only in fallible staging.
    internal Action<string>? StylePublicationProbe { get; set; }

    public long HostInputSerial => _hostInputSerial;
    public long HostPublicationSerial => _hostPublicationSerial;
    public bool ManagedProjectionOutOfDate => _managedProjectionOutOfDate;

    private void NoteInputMutation()
    {
        ++_hostInputSerial;
        _styleRetryBlocked = false;
        _managedProjectionOutOfDate = true;
        _hasPendingPointerTarget = false;
        _pendingPointerTarget = null;
    }

    private void ProduceInvalidation(VisualInvalidation flags)
    {
        for (var i = 0; i < _invalidationProduced.Length; ++i)
            if (((int)flags & (1 << i)) != 0) ++_invalidationProduced[i];
        _invalidation |= flags;
    }

    private void AcknowledgeInvalidation(VisualInvalidation flags, long[]? captured = null)
    {
        for (var i = 0; i < _invalidationProduced.Length; ++i)
        {
            var flag = (VisualInvalidation)(1 << i);
            if ((flags & flag) == 0) continue;
            _invalidationAcknowledged[i] = Math.Max(_invalidationAcknowledged[i], captured?[i] ?? _invalidationProduced[i]);
            if (_invalidationAcknowledged[i] == _invalidationProduced[i]) _invalidation &= ~flag;
        }
    }

    private bool DeferInput(Action action)
    {
        if (_styleOperationDepth == 0) return false;
        _deferredInput.Enqueue(action);
        _renderDeferred = true;
        return true;
    }

    private void DrainDeferredInput()
    {
        if (_styleOperationDepth != 0 || _drainingInput || _disposed || _managedProjectionOutOfDate) return;
        _drainingInput = true;
        try
        {
            // Like Chromium DispatchEvents, callbacks cannot extend this drain forever.
            var count = _deferredInput.Count;
            while (count-- > 0 && !_disposed)
            {
                var next = _deferredInput.Peek();
                _deferredInput.Dequeue();
                next();
            }
        }
        finally { _drainingInput = false; }
    }

    private bool HasCertifiedStyleSnapshot() => !_disposed && !_managedProjectionOutOfDate &&
        !_markupDirty && _document is not null && _document.Version == _publishedDocumentVersion &&
        _nativeStyleRuntime.IsCurrent(_publishedStyleToken);

    private void RejectStyleProjection()
    {
        _managedProjectionOutOfDate = true;
        _hasPendingPointerTarget = false;
        _pendingPointerTarget = null;
        ProduceInvalidation(VisualInvalidation.Style);
    }

    private void LeaveStyleOperation()
    {
        --_styleOperationDepth;
        if (_styleOperationDepth == 0 && _disposeResourcesPending) DisposeResources();
    }
}
