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
    private bool _forceFullProjectionRecovery;
    private bool _styleRetryBlocked;
    private bool _renderDeferred;
    private readonly Queue<Action> _deferredInput = new();
    private bool _drainingInput;
    private NativeStylePublicationToken _publishedStyleToken;
    private long _publishedDocumentVersion;
    private long _nativeSynchronizedDocumentVersion;
    private long _knownManagedInputVersion;
    private long _publishedDocumentInputVersion;
    private bool _inputReconciliationRequired;
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

    private void OnManagedInputsChanged()
    {
        NoteInputMutation();
        ProduceInvalidation(VisualInvalidation.Style);
        // No RenderRequested or user code inside a DOM mutation. Unknown writes
        // remain unacknowledged until the separately reviewed DOMsync bridge.
    }

    private void SetKnownElementState(ElementNode element, ElementState value)
    {
        var document = element.OwnerDocument;
        var before = document?.InputVersion ?? 0;
        var changed = element.State != value;
        element.State = value;
        AcknowledgeKnownManagedWrite(document, before, changed);
    }

    private void SetKnownRuntimeValue(ElementNode element, string? value)
    {
        var document = element.OwnerDocument;
        var before = document?.InputVersion ?? 0;
        var changed = !string.Equals(element.RuntimeValue, value, StringComparison.Ordinal);
        element.RuntimeValue = value;
        AcknowledgeKnownManagedWrite(document, before, changed);
        if (changed) _runtimeValuesDirty = true;
    }

    private void AcknowledgeKnownManagedWrite(DocumentNode? document, long before, bool changed)
    {
        // Acknowledge only this exact write. Never adopt an arbitrary latest
        // serial, which could hide an earlier external/reentrant mutation.
        var expected = before + (changed ? 1 : 0);
        if (ReferenceEquals(document, _document) && _knownManagedInputVersion == before &&
            document!.InputVersion == expected) _knownManagedInputVersion = expected;
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

    // Pure host-entry check. DOMsync may add its corresponding pure owner-token
    // check beside this one; neither path imports or emits RenderRequested.
    private bool TransitionProjectionNeedsRender() => _transitionRuntime is { } runtime &&
        (runtime.TryReadToken(out var token) != 0 || !token.AllowsPublication ||
         token != _publishedTransitionToken || token != _derivedTransitionToken);

    private bool HasCertifiedStyleSnapshot(bool requireDerived = true) => !_disposed && !_managedProjectionOutOfDate &&
        !_markupDirty && _document is not null && _document.Version == _publishedDocumentVersion &&
        _document.InputVersion == _publishedDocumentInputVersion &&
        _nativeStyleRuntime.IsCurrent(_publishedStyleToken) &&
        _transitionRuntime is { } runtime && runtime.IsCurrent(_publishedTransitionToken) &&
        (!requireDerived || _derivedTransitionToken == _publishedTransitionToken);

    private static void ThrowTransitionStatus(int status)
    {
        if (status == -3) throw new OutOfMemoryException("Transition sampling failed; no new frame or hit is certified.");
        if (status != 0) throw new InvalidOperationException($"Transition projection is unavailable (native status {status}).");
    }

    private void RequireHealthyTransitions(TransitionRuntimeInterop runtime)
    {
        var status = runtime.TryReadToken(out var token);
        if (status == 0) status = token.TickError;
        if (status == 0) return;
        RejectStyleProjection();
        _styleRetryBlocked = true;
        ThrowTransitionStatus(status);
    }

    private void ValidateDerivedTransition(TransitionRuntimeInterop runtime, NativeTransitionToken token)
    {
        if (!_disposed && ReferenceEquals(runtime, _transitionRuntime) && runtime.IsCurrent(token) &&
            HasCertifiedStyleSnapshot(requireDerived: false)) return;
        RejectStyleProjection();
        _styleRetryBlocked = true;
        ThrowIfDisposed();
        RequireHealthyTransitions(runtime);
        throw new InvalidOperationException("Inputs or transitions changed while preparing derived output; no new frame or hit is certified.");
    }

    private void RejectStyleProjection()
    {
        _managedProjectionOutOfDate = true;
        _forceFullProjectionRecovery = true;
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
