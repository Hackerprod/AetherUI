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
    private readonly Dictionary<ElementNode, ComputedStyle> _computedStyles = new();
    private readonly HashSet<ElementNode> _stateDirtyElements = [];
    private readonly Dictionary<long, IntPtr> _paintChunkDirtyNodes = [];

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

        _displayList = patched;
        _displayListContainsPatchedChunks = true;
        Interlocked.Add(ref _paintChunkBuildCount, replacements.Count);
        return true;
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
        if ((_invalidation & VisualInvalidation.Style) == 0)
        {
            return;
        }

        var styleResolutionStart = Stopwatch.GetTimestamp();
        NativeStyleImpact impact;
        var visibilityChanged = false;
        var paintDirtyNodes = new Dictionary<long, IntPtr>();
        var updatedNativeNodes = new List<IntPtr>();
        var changedHandles = Array.Empty<IntPtr>();
        var changedStates = Array.Empty<int>();
        var incrementalStateResolution = false;
        var attemptedIncrementalStateResolution = false;
        if (_stateDirtyElements.Count > 0 && _computedStyles.Count > 0 && _nativeDom is not null)
        {
            attemptedIncrementalStateResolution = true;
            var changedElements = _stateDirtyElements
                .Where(_computedStyles.ContainsKey)
                .ToArray();
            changedHandles = changedElements
                .Select(element => _nativeDom.GetNativeHandle(element))
                .ToArray();
            changedStates = changedElements
                .Select(static element => (int)element.State)
                .ToArray();
        }

        if (changedHandles.Length > 0)
        {
            incrementalStateResolution = true;
            var resolution = _nativeStyleRuntime.ResolveStateChanges(changedHandles, changedStates);
            impact = resolution.Impact;
            foreach (var update in resolution.Updates)
            {
                updatedNativeNodes.Add(update.Node);
                TrackPaintChunkDirtyNode(update, paintDirtyNodes);
                if (_nativeDom!.GetManagedElement(update.Node) is { } managed)
                {
                    _computedStyles.TryGetValue(managed, out var previousStyle);
                    var nextStyle = StyleRuntimeBridge.ToComputedStyle(update.Style, managed);
                    visibilityChanged |= previousStyle is not null && previousStyle.Visible != nextStyle.Visible;
                    _computedStyles[managed] = nextStyle;
                    if (previousStyle is not null)
                        ObserveTransition(update.Node, previousStyle, nextStyle);
                }
            }
            Interlocked.Add(ref _styleUpdatedElementCount, resolution.Updates.Count);
        }
        else if (attemptedIncrementalStateResolution)
        {
            impact = NativeStyleImpact.None;
        }
        else
        {
            var elements = EnumerateElements(_document!).ToArray();
            var states = elements.Select(static element => (int)element.State).ToArray();
            var resolution = _nativeStyleRuntime.Resolve(states, _viewport.Width, _viewport.Height, _dpiScale);
            impact = resolution.Impact;
            var nativeStyles = resolution.Styles;
            for (var i = 0; i < elements.Length; i++)
            {
                var element = elements[i];
                _computedStyles.TryGetValue(element, out var previousStyle);
                if (previousStyle is null)
                    _previousStyleSnapshot.TryGetValue(ElementIdentityKey(element), out previousStyle);
                var nextStyle = StyleRuntimeBridge.ToComputedStyle(nativeStyles[i], element);
                visibilityChanged |= previousStyle is not null && previousStyle.Visible != nextStyle.Visible;
                _computedStyles[element] = nextStyle;
                if (previousStyle is not null)
                    ObserveTransition(_nativeDom!.GetNativeHandle(element), previousStyle, nextStyle);
            }
            Interlocked.Add(ref _styleUpdatedElementCount, nativeStyles.Count);
        }

        _stateDirtyElements.Clear();
        _paintChunkDirtyNodes.Clear();
        foreach (var pair in paintDirtyNodes)
        {
            _paintChunkDirtyNodes[pair.Key] = pair.Value;
        }
        _lastStyleImpact = impact;
        _styles = new StyleMap(_computedStyles);
        if (incrementalStateResolution && updatedNativeNodes.Count > 0)
        {
            RefreshProjectedLayoutStyles(updatedNativeNodes);
            if (_nativeVisualStyleMap is not null)
            {
                _nativeStyleRuntime.UpdateResolvedStyleMap(_nativeVisualStyleMap, updatedNativeNodes);
                Interlocked.Increment(ref _visualStyleMapUpdateCount);
            }
            else
            {
                _nativeVisualStyleMap = _nativeStyleRuntime.CreateResolvedStyleMap(_nativeDom!.Native);
                Interlocked.Increment(ref _visualStyleMapBuildCount);
            }
        }
        else
        {
            RefreshProjectedLayoutStyles();
            _nativeVisualStyleMap?.Dispose();
            _nativeVisualStyleMap = _nativeStyleRuntime.CreateResolvedStyleMap(_nativeDom!.Native);
            Interlocked.Increment(ref _visualStyleMapBuildCount);
        }
        Interlocked.Add(ref _styleResolutionTimestampTicks, Stopwatch.GetTimestamp() - styleResolutionStart);
        Interlocked.Increment(ref _styleResolutionCount);
        _invalidation &= ~VisualInvalidation.Style;
        _invalidation |= InvalidationForStyleImpact(impact);
        if (visibilityChanged)
        {
            // Isolated chunks do not carry ancestor opacity/clip/transform context.
            // Rebuild paint only, retaining native layout and its geometry snapshot.
            _paintChunkDirtyNodes.Clear();
            _invalidation &= ~VisualInvalidation.PaintChunks;
            _invalidation |= VisualInvalidation.DisplayList;
            _pendingPointerTarget = null;
            _hasPendingPointerTarget = false;
            if (_hasPointerPosition)
            {
                _pendingPointerMove = _lastPointerPosition;
                _hasPendingPointerMove = true;
            }
        }
        if (_scrollbarDrag is { } drag && !IsCurrentlyVisible(drag.Container))
            CancelScrollbarDrag();
    }

}
