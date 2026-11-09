using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;

namespace AetherUI.Runtime;

public sealed partial class UiDocumentHost
{
    // This registry is published with _document. Standalone component.RenderMarkup must
    // never replace it. Dispatch and retained lifecycle records pin this reference.
    private UiComponent.EventRegistry? _componentRegistry;
    private ComponentBookkeepingProxy? _componentBookkeepingProxy;

    // Test seam: staging phases may throw; "published" is explicitly AFTER the no-fail
    // segment and tests notification/retirement failure without claiming rollback.
    internal Action<string>? ComponentPublicationProbe { get; set; }

    private sealed class ComponentBookkeepingProxy : IDomHostBookkeeping
    {
        internal IDomHostBookkeeping? Target;
        private static readonly EmptyBookkeeping Empty = new();
        public IDomPreparedBookkeeping Prepare(DomEdit? edit, bool import) => Target?.Prepare(edit, import) ?? Empty;
        public IDomPreparedBookkeeping PrepareImport(NativeDomSnapshot snapshot) => Target?.PrepareImport(snapshot) ?? Empty;
        public IDomPreparedBookkeeping PrepareStructure(DomEdit edit, IReadOnlyList<IntPtr> elements) => Target?.PrepareStructure(edit, elements) ?? Empty;
        public void NotifyMutationCompleted() => Target?.NotifyMutationCompleted();
        private sealed class EmptyBookkeeping : IDomPreparedBookkeeping
        {
            public void Validate() { }
            public void Publish() { }
            public void Dispose() { }
        }
    }

    private void PublishComponentDocument()
    {
        // UA initialization is persistent, outside the candidate transaction and before
        // capturing any bases. A failed initial candidate does not install UA twice.
        _nativeStyleRuntime.EnsureDomDefaults();
        ThrowIfDisposed();
        _nativeDom?.BindingCore?.EnsureCurrent();
        var previousDom = _nativeDom;
        var previousDocument = _document;
        var previousProxy = _componentBookkeepingProxy;
        var previousInteractions = _interactionSession;
        var previousTransitions = _transitionRuntime;
        var previousTransitionToken = previousTransitions?.ReadToken() ?? default;
        var previousDomToken = previousDom is null ? default : NativeDomSync.ReadToken(previousDom.Native.Handle);
        var inputSerial = _hostInputSerial;
        var markupSerial = _markupInputSerial;
        var nextInputSerial = checked(inputSerial + 1);
        var nextStyleSerial = checked(_invalidationProduced[0] + 1);
        var focusedId = _focused?.Id;
        var hoveredId = _hovered?.Id;
        var activeId = _active?.Id;
        var previousStyles = BuildStyleSnapshot();
        var values = new Dictionary<string, string>(_uncontrolledInputValues, StringComparer.Ordinal);
        using var candidate = _component.PrepareRenderCandidate();
        var registry = candidate.Registry;
        var markup = candidate.Markup;
        ComponentPublicationProbe?.Invoke("built");
        DomInterop? nextDom = null;
        TransitionRuntimeInterop? nextTransitions = null;
        InteractionSession? nextInteractions = null;
        var published = false;
        try
        {
            nextDom = DomInterop.Parse(markup);
            ComponentPublicationProbe?.Invoke("parsed");
            var document = nextDom.Document;
            var proxy = new ComponentBookkeepingProxy();
            nextDom.EnableSynchronization(proxy);
            ComponentPublicationProbe?.Invoke("mapped");
            // Only the private B tree is modified. Its proxy has no live host target.
            foreach (var input in document.GetElementsByTagName("input"))
            {
                if (input.GetAttribute("value") is { } controlledValue)
                {
                    input.RuntimeValue = controlledValue;
                    if (input.Id is { Length: > 0 } controlledId) values[controlledId] = controlledValue;
                }
                else if (input.Id is { Length: > 0 } id && values.TryGetValue(id, out var saved))
                    input.RuntimeValue = saved;
            }
            ElementNode? Restore(string? id, ElementState state)
            {
                if (string.IsNullOrEmpty(id)) return null;
                var element = document.GetElementById(id);
                if (element is not null) element.State |= state;
                return element;
            }
            var focused = Restore(focusedId, ElementState.Focus);
            var hovered = Restore(hoveredId, ElementState.Hover);
            var active = Restore(activeId, ElementState.Active);
            var hoverChain = new HashSet<ElementNode>();
            if (hovered is not null)
                foreach (var element in hovered.AncestorsAndSelf())
                {
                    element.State |= ElementState.Hover;
                    hoverChain.Add(element);
                }
            var computed = new Dictionary<ElementNode, ComputedStyle>();
            var dirty = new HashSet<ElementNode>();
            var toSync = new HashSet<ElementNode>();
            var paintDirty = new Dictionary<long, IntPtr>();
            nextTransitions = new TransitionRuntimeInterop();
            var transitionHandle = nextTransitions.Handle;
            var version = document.Version;
            var inputVersion = document.InputVersion;
            var bookkeeping = CreateDomHostBookkeeping(nextDom);
            ComponentPublicationProbe?.Invoke("initialized");
            nextInteractions = PrepareInteractionSession(nextDom, registry, focused, active, hoverChain);
            using var binding = _nativeStyleRuntime.PrepareDomBinding(nextDom.Native);
            ComponentPublicationProbe?.Invoke("slot-prepared");
            var previousLayout = _nativeLayout;
            var previousLayoutMap = _nativeLayoutStyleMap;
            var previousVisualMap = _nativeVisualStyleMap;
            ComponentPublicationProbe?.Invoke("ready");

            // Last validation before native input publication. No user callback is permitted
            // between these checks, binding commit and the reference/scalar swaps below.
            ThrowIfDisposed();
            if (!_component.IsRenderCandidateCurrent(candidate) || _hostInputSerial != inputSerial ||
                _markupInputSerial != markupSerial || !ReferenceEquals(previousDom, _nativeDom) ||
                !ReferenceEquals(previousTransitions, _transitionRuntime) ||
                (previousDom is not null && NativeDomSync.ReadToken(previousDom.Native.Handle) != previousDomToken) ||
                (previousTransitions is not null && (previousTransitions.TryReadToken(out var currentTransition) != 0 || currentTransition != previousTransitionToken)))
                throw new InvalidOperationException("Component publication was superseded during preparation.");
            nextInteractions.Owner.ValidatePrepared();
            binding.Commit();
            PublishInteractionSession(nextInteractions, _nativeStyleRuntime.DomStyleBindingHandle);

            // Native DOM/source binding is now committed. All remaining publication work
            // uses preallocated data. No freshness getter, delegate or retirement is here.
            if (previousProxy is not null) previousProxy.Target = null;
            proxy.Target = bookkeeping;
            _componentBookkeepingProxy = proxy;
            _componentRegistry = registry;
            _nativeDom = nextDom;
            _document = document;
            _transitionRuntime = nextTransitions;
            _renderingInterop.SetTransitionRuntime(transitionHandle);
            _previousStyleSnapshot = previousStyles;
            _computedStyles = computed;
            _stateDirtyElements = dirty;
            _stateInputsToSync = toSync;
            _retainedStyleBatches = null;
            _paintChunkDirtyNodes = paintDirty;
            _uncontrolledInputValues = values;
            _hoverChain = hoverChain;
            _focused = focused;
            _hovered = hovered;
            _active = active;
            CancelScrollbarDrag(); // Capture is deliberately retained until release.
            _scrollOffsets.Clear();
            _scrollOffsetsByStyleNode.Clear();
            _scrollBoxesByStyleNode.Clear();
            _nativeLayout = null;
            _nativeLayoutStyleMap = null;
            _nativeVisualStyleMap = null;
            _styles = null;
            _layout = null;
            _displayList = null;
            _displayListContainsPatchedChunks = false;
            _publishedStyleToken = default;
            _publishedTransitionToken = default;
            _derivedTransitionToken = default;
            _publishedDocumentVersion = 0;
            _publishedDocumentInputVersion = 0;
            _nativeSynchronizedDocumentVersion = version;
            _knownManagedInputVersion = inputVersion;
            _inputReconciliationRequired = false;
            _nativeInitialStatesSynchronized = true;
            _runtimeValuesDirty = false;
            _markupDirty = false;
            _managedProjectionOutOfDate = true;
            _forceFullProjectionRecovery = true;
            _hasPendingPointerTarget = false;
            _pendingPointerTarget = null;
            _hostInputSerial = nextInputSerial;
            _invalidationProduced[0] = nextStyleSerial;
            _invalidation |= VisualInvalidation.Style;
            published = true;

            // Retire only after publication, still inside the host guard. If notification
            // or cleanup fails, B stays published and more recent user effects stay intact.
            try { previousInteractions?.Dispose(); ComponentPublicationProbe?.Invoke("published"); }
            finally
            {
                try { previousLayout?.Dispose(); }
                finally
                {
                    try { previousLayoutMap?.Dispose(); }
                    finally
                    {
                        try { previousVisualMap?.Dispose(); }
                        finally
                        {
                            try { previousTransitions?.Dispose(); }
                            finally
                            {
                                if (previousDocument is not null) previousDocument.InputsChanged -= OnManagedInputsChanged;
                                previousDom?.Dispose();
                            }
                        }
                    }
                }
            }
        }
        catch
        {
            // Rebuild failure must not become an automatic OOM/reentrancy retry loop.
            // Explicit Render can attempt the still-pending component again.
            _styleRetryBlocked = true;
            throw;
        }
        finally
        {
            if (!published)
            {
                try { nextInteractions?.Dispose(); }
                finally
                {
                    try { nextTransitions?.Dispose(); }
                    finally { nextDom?.Dispose(); }
                }
            }
        }
    }
}
