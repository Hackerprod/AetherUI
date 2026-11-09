using AetherUI.Css;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using System.Diagnostics;

namespace AetherUI.Runtime;

/// <summary>
/// Document host region: Style resolution, projected layout styles and paint-chunk patching.
/// </summary>
public sealed partial class UiDocumentHost
{
    /// <summary>
    /// Cleared only when the tree itself is rebuilt; repopulated in full from the native style
    /// runtime's resolve result on every visual pass.
    /// </summary>
    private Dictionary<ElementNode, ComputedStyle> _computedStyles = new();
    private HashSet<ElementNode> _stateDirtyElements = [];
    private HashSet<ElementNode> _stateInputsToSync = [];
    private RetainedStyleBatch? _retainedStyleBatches;
    private sealed class RetainedStyleBatch(HashSet<ElementNode> elements)
    {
        internal readonly HashSet<ElementNode> Elements = elements;
        internal RetainedStyleBatch? Next;
    }
    private bool _nativeInitialStatesSynchronized;
    private Dictionary<long, IntPtr> _paintChunkDirtyNodes = [];

    private void RefreshProjectedLayoutStyles()
    {
        if (_layout is null || _styles is null) return;
        foreach (var box in _layout.Root.DescendantsAndSelf())
        {
            if (_styles.TryGetValue(box.Element, out var style))
                box.Style = style;
        }
    }

    private void RefreshProjectedLayoutStyles(List<IntPtr> updatedNativeNodes)
    {
        if (_layout is null || _styles is null || _nativeDom is null || updatedNativeNodes.Count == 0) return;
        foreach (var node in updatedNativeNodes)
        {
            if (_nativeDom.GetManagedElement(node) is not { } element ||
                !_styles.TryGetValue(element, out var style))
            {
                continue;
            }

            if (_layout.FindPrimaryBox(element) is { } primary)
            {
                primary.Style = style;
            }

            foreach (var box in _layout.FindPaintBoxes(element))
                box.Style = style;
        }
    }

    private void TrackPaintChunkDirtyNode(NativeStyleUpdate update, Dictionary<long, IntPtr> paintDirtyNodes)
    {
        if (_nativeDom is null || !CanPatchStyleImpact(update.Impact))
        {
            return;
        }

        var styleNodeId = _nativeDom.Native.GetStyleNodeId(update.Node);
        if (styleNodeId > 0)
        {
            paintDirtyNodes[styleNodeId] = update.Node;
        }
    }

    private bool TryPatchPaintChunks()
    {
        if (_displayList is null || _nativeLayout is null || _nativeVisualStyleMap is null ||
            _nativeDom is null || _paintChunkDirtyNodes.Count == 0)
        {
            return false;
        }

        var runtime = _transitionRuntime!;
        var transitionToken = _publishedTransitionToken;
        ++_styleOperationDepth;
        try
        {
            var replacements = new Dictionary<long, DisplayList>(_paintChunkDirtyNodes.Count);
            foreach (var (styleNodeId, node) in _paintChunkDirtyNodes)
            {
                if (!_displayList.PaintChunks.Any(chunk => chunk.StyleNodeId == styleNodeId))
                {
                    return false;
                }

                var partial = AttachResolvedImages(_renderingInterop.BuildNode(_nativeLayout, _nativeVisualStyleMap,
                    node, _focused is null ? IntPtr.Zero : _nativeDom.GetNativeHandle(_focused)));
                replacements[styleNodeId] = partial;
            }

            if (!_displayList.TryReplaceChunks(_paintChunkDirtyNodes.Keys.ToArray(), replacements, out var patched))
            {
                return false;
            }

            ValidateDerivedTransition(runtime, transitionToken);
            _displayList = patched;
            _displayListContainsPatchedChunks = true;
            Interlocked.Add(ref _paintChunkBuildCount, replacements.Count);
            return true;
        }
        catch
        {
            RejectStyleProjection();
            _styleRetryBlocked = true;
            throw;
        }
        finally { LeaveStyleOperation(); }
    }

    private static VisualInvalidation InvalidationForStyleImpact(NativeStyleImpact impact)
        => (VisualInvalidation)NativeRenderPlanInterop.VisualInvalidationFromStyleImpact((uint)impact);

    private static bool CanPatchStyleImpact(NativeStyleImpact impact)
    {
        if ((impact & (NativeStyleImpact.Layout | NativeStyleImpact.TextLayout)) != 0)
        {
            return false;
        }

        return (impact & (NativeStyleImpact.Paint | NativeStyleImpact.Composite)) != 0;
    }

    private void ResolveStyles()
    {
        if ((_invalidation & VisualInvalidation.Style) == 0) return;
        if (_styleOperationDepth != 0) { _renderDeferred = true; return; }
        for (var attempt = 0; attempt < 2; ++attempt)
        {
            ThrowIfDisposed();
            if (_markupDirty) RebuildDocument();
            SyncNativeRuntimeValues();
            if (TryPublishStyles()) return;
            if (_disposed) ThrowIfDisposed();
        }
        _styleRetryBlocked = true;
        throw new InvalidOperationException("Style publication did not stabilize after one recovery. Pending inputs were retained; no frame was presented.");
    }

    private bool TryPublishStyles()
    {
        if (_inputReconciliationRequired || _document!.Version != _nativeSynchronizedDocumentVersion ||
            _document.InputVersion != _knownManagedInputVersion)
        {
            RejectStyleProjection();
            _styleRetryBlocked = true;
            throw new InvalidOperationException("Managed DOM synchronization is required before style publication; pending mutations were retained.");
        }
        var transitionRuntime = _transitionRuntime ?? throw new InvalidOperationException("Transition runtime is unavailable.");
        RequireHealthyTransitions(transitionRuntime);
        var transitionNowMs = Environment.TickCount64;
        var start = Stopwatch.GetTimestamp();
        // Allocate Bnext before removing B. Notifications never write into B.
        var nextBatch = new HashSet<ElementNode>();
        var batch = new RetainedStyleBatch(_stateDirtyElements);
        var retainedAtStart = _retainedStyleBatches;
        var invalidations = (long[])_invalidationProduced.Clone();
        var publishedInvalidations = new long[_invalidationProduced.Length];
        var inputSerial = _hostInputSerial;
        var publicationSerial = _hostPublicationSerial;
        var document = _document!;
        var dom = _nativeDom!;
        var documentVersion = document.Version;
        var documentInputVersion = document.InputVersion;
        var published = false;
        var transitions = new List<TransitionStyleMarshaler.PreparedObservation>();
        _stateDirtyElements = nextBatch;
        ++_styleOperationDepth;
        try
        {
            // Synchronize current input values once, never replay an old batch's state array.
            // A native R2 published during a callback must survive the recovery attempt.
            if (_nativeInitialStatesSynchronized)
            {
                foreach (var element in _stateInputsToSync)
                    if (ReferenceEquals(element.OwnerDocument, document))
                        dom.Native.SetState(dom.GetNativeHandle(element), (int)element.State);
            }
            var initialStates = !_nativeInitialStatesSynchronized
                ? EnumerateElements(document).Select(static element => (int)element.State).ToArray() : null;
            using var diagnostics = CssDiagnostics.BeginTransaction();
            using var native = _nativeStyleRuntime.PreparePublication(initialStates, _viewport.Width, _viewport.Height, _dpiScale, _nativeVisualStyleMap);
            _nativeInitialStatesSynchronized = true;
            _stateInputsToSync.Clear(); // No callbacks have run; Bnext state writes start below.
            if (_publishedStyleToken.DocumentIdentity == native.Token.DocumentIdentity &&
                _publishedStyleToken.StructureEpoch != native.Token.StructureEpoch)
                throw new InvalidOperationException("Native DOM structure changed without a certified managed mapping.");
            StylePublicationProbe?.Invoke("native-preview");
            var computed = new Dictionary<ElementNode, ComputedStyle>(native.Styles.Count);
            var boxes = new List<(LayoutBox Box, ComputedStyle Style)>();
            var paintDirty = new Dictionary<long, IntPtr>(_paintChunkDirtyNodes);
            var visibilityChanged = false;
            foreach (var update in native.Styles)
            {
                if (update.Impact != NativeStyleImpact.None) TrackPaintChunkDirtyNode(update, paintDirty);
                if (dom.GetManagedElement(update.Node) is not { } element) continue;
                _computedStyles.TryGetValue(element, out var previous);
                if (previous is null && ElementIdentityKey(element) is { } key) _previousStyleSnapshot.TryGetValue(key, out previous);
                var style = !update.ProjectionChanged && previous is not null
                    ? previous : StyleRuntimeBridge.ToComputedStyle(update.Style, element);
                computed.Add(element, style);
                visibilityChanged |= previous is not null && previous.Visible != style.Visible;
                if (update.ProjectionChanged && previous is not null) transitions.Add(TransitionStyleMarshaler.Prepare(update.Node, previous, style));
            }
            var styles = new StyleMap(computed);
            if (_layout is not null)
                foreach (var box in _layout.Root.DescendantsAndSelf())
                    if (computed.TryGetValue(box.Element, out var style) && !ReferenceEquals(style, box.Style)) boxes.Add((box, style));
            var reorder = visibilityChanged || native.StackingOrderChanged;
            if (reorder) paintDirty = new Dictionary<long, IntPtr>();
            using var transitionCandidate = TransitionStyleMarshaler.PrepareBatch(transitionRuntime, transitions, [], transitionNowMs);
            diagnostics.Seal();
            StylePublicationProbe?.Invoke("managed-staged");
            // No candidate is installed if staging itself called back and changed inputs.
            if (!HostInputsMatch()) return Reject();
            var nativeCurrent = native.Commit();
            if (!nativeCurrent && _hostInputSerial != inputSerial)
                _inputReconciliationRequired = true; // Mixed native/managed chronology needs DOMsync, never blind replay.
            if (!nativeCurrent || !HostInputsMatch() || !_nativeStyleRuntime.IsCurrent(native.Token)) return Reject();
            RequireHealthyTransitions(transitionRuntime);
            if (transitionCandidate.Validate() != 0) return Reject();

            // Certified owner-thread publication. All fallible .NET allocations happened above.
            if (transitionCandidate.TryCommit(out var transitionPublishedToken) != 0) return Reject();
            var previousMap = _nativeVisualStyleMap;
            _nativeVisualStyleMap = native.Publish();
            _computedStyles = computed;
            _styles = styles;
            foreach (var (box, style) in boxes) box.Style = style;
            _paintChunkDirtyNodes = paintDirty;
            _lastStyleImpact = native.Impact;
            _publishedStyleToken = native.Token;
            _publishedTransitionToken = transitionPublishedToken;
            _publishedDocumentVersion = documentVersion;
            _publishedDocumentInputVersion = documentInputVersion;
            ++_hostPublicationSerial;
            _managedProjectionOutOfDate = false;
            AcknowledgeInvalidation(VisualInvalidation.Style, invalidations);
            ProduceInvalidation(InvalidationForStyleImpact(native.Impact));
            if (_forceFullProjectionRecovery)
            {
                // Native deltas are relative to its current R2, not this host's
                // older projection. Recovery must rebuild geometry/paint even
                // when preparing R3 from R2 reports no further visual change.
                ProduceInvalidation(VisualInvalidation.Layout | VisualInvalidation.DisplayList);
                _forceFullProjectionRecovery = false;
            }
            if (reorder)
            {
                // Do not wipe Bnext; new notifications have not begun yet.
                AcknowledgeInvalidation(VisualInvalidation.PaintChunks);
                ProduceInvalidation(VisualInvalidation.DisplayList);
                _pendingPointerTarget = null;
                _hasPendingPointerTarget = false;
                if (_hasPointerPosition)
                {
                    _pendingPointerMove = _lastPointerPosition;
                    _hasPendingPointerMove = true;
                }
            }
            if (ReferenceEquals(_retainedStyleBatches, retainedAtStart)) _retainedStyleBatches = null;
            published = true;
            previousMap?.Dispose();
            Interlocked.Add(ref _styleUpdatedElementCount, native.ChangedStyleCount);
            if (native.MapPatched) Interlocked.Increment(ref _visualStyleMapUpdateCount);
            else Interlocked.Increment(ref _visualStyleMapBuildCount);
            Interlocked.Increment(ref _styleResolutionCount);
            Interlocked.Add(ref _styleResolutionTimestampTicks, Stopwatch.GetTimestamp() - start);

            Array.Copy(_invalidationProduced, publishedInvalidations, publishedInvalidations.Length);
            // Listeners are external effects, not part of rollback. They write Bnext.
            diagnostics.Replay();
            if (!_disposed) RequireHealthyTransitions(transitionRuntime);
            var replayNativeCurrent = _nativeStyleRuntime.IsCurrent(native.Token);
            if (!replayNativeCurrent && _hostInputSerial != inputSerial) _inputReconciliationRequired = true;
            if (_disposed || _hostInputSerial != inputSerial || _hostPublicationSerial != publicationSerial + 1 ||
                !ReferenceEquals(_document, document) || !ReferenceEquals(_nativeDom, dom) ||
                document.Version != documentVersion || document.InputVersion != documentInputVersion || !InvalidationsMatch(publishedInvalidations) || !replayNativeCurrent ||
                !ReferenceEquals(_transitionRuntime, transitionRuntime) || !transitionRuntime.IsCurrent(transitionPublishedToken))
                return Reject();
            if (_scrollbarDrag is { } drag && !IsCurrentlyVisible(drag.Container)) CancelScrollbarDrag();
            return true;

            bool HostInputsMatch() => !_disposed && !_markupDirty && _hostInputSerial == inputSerial &&
                _hostPublicationSerial == publicationSerial && ReferenceEquals(_transitionRuntime, transitionRuntime) && ReferenceEquals(_document, document) &&
                ReferenceEquals(_nativeDom, dom) && document.Version == documentVersion &&
                document.InputVersion == documentInputVersion && InvalidationsMatch(invalidations);
            bool InvalidationsMatch(long[] serials)
            {
                for (var i = 0; i < serials.Length; ++i)
                    if (_invalidationProduced[i] != serials[i]) return false;
                return true;
            }
            bool Reject() { RejectStyleProjection(); return false; }
        }
        catch
        {
            // A native commit may already be real. Never claim rollback of it or a listener.
            RejectStyleProjection();
            _styleRetryBlocked = true; // OOM/notification faults do not start a retry loop.
            throw;
        }
        finally
        {
            if (!published || _managedProjectionOutOfDate)
            {
                batch.Next = _retainedStyleBatches;
                _retainedStyleBatches = batch; // Retain B without a fallible merge during exception unwind.
            }
            foreach (var transition in transitions) transition.Dispose();
            LeaveStyleOperation();
        }
    }
}
