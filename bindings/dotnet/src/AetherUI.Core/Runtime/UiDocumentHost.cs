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
/// Coordinates a generated component, DOM, style resolution, layout, input routing, and rendering.
/// Platform hosts translate native messages into the methods exposed here.
/// </summary>
public sealed partial class UiDocumentHost : IDisposable
{
    private readonly UiComponent _component;
    private readonly UiHostOptions _options;
    private readonly IRenderBackend _renderer;
    private readonly LayoutInterop _layoutInterop;
    private readonly RenderingInterop _renderingInterop;
    private readonly StyleRuntimeInterop _nativeStyleRuntime;
    private readonly IImageResourceResolver? _imageResolver;
    private readonly SynchronizationContext? _hostSynchronizationContext;

    private DocumentNode? _document;
    private DomInterop? _nativeDom;
    private NativeStyleMap? _nativeLayoutStyleMap;
    private NativeStyleMap? _nativeVisualStyleMap;
    private NativeLayoutResult? _nativeLayout;
    private LayoutResult? _layout;
    private DisplayList? _displayList;
    private StyleMap? _styles;
    private SizeF _viewport;
    private float _dpiScale = 1;
    private bool _markupDirty = true;
    private bool _displayListContainsPatchedChunks;
    private VisualInvalidation _invalidation = VisualInvalidation.Style;
    private long _styleResolutionCount;
    private long _styleUpdatedElementCount;
    private long _layoutBuildCount;
    private long _displayListBuildCount;
    private long _paintChunkBuildCount;
    private long _scrollCompositionCount;
    private long _renderCallCount;
    private long _incrementalRenderCallCount;
    private long _visualStyleMapBuildCount;
    private long _visualStyleMapUpdateCount;
    private long _styleResolutionTimestampTicks;
    private long _displayListBuildTimestampTicks;
    private long _renderTimestampTicks;
    private NativeStyleImpact _lastStyleImpact;
    private bool _disposed;

    /// <summary>
    /// Live CSS transition runtime. Style-resolution passes call observe; the host clock drives
    /// <see cref="TickTransitions"/> while <see cref="HasActiveTransitions"/> is true.
    /// </summary>
    private TransitionRuntimeInterop? _transitionRuntime;

    /// <summary>
    /// Pre-rebuild computed styles keyed by stable element identity. RebuildDocument clears
    /// <see cref="_computedStyles"/> (ElementNode identity does not survive a rebuild), so the
    /// transition runtime still receives the previous style for state-triggered markup changes.
    /// </summary>
    private Dictionary<string, ComputedStyle> _previousStyleSnapshot = [];

    [Flags]
    private enum VisualInvalidation : byte
    {
        None = 0,
        // A style change is the strongest ordinary invalidation: it necessarily
        // produces a new layout and paint command stream.
        Style = 1 << 0,
        // A viewport/metric change reuses computed styles but needs fresh boxes.
        Layout = 1 << 1,
        // A paint-only change (for example an image becoming ready) retains layout.
        DisplayList = 1 << 2,
        PaintChunks = 1 << 3,
        // Scroll is intentionally independent. It is not consumed until the retained
        // scroll-composition path is installed; keeping it distinct now prevents a
        // future input path from accidentally escalating to cascade again.
        Scroll = 1 << 4,
    }

    public UiDocumentHost(
        UiComponent component,
        ITextMeasurer textMeasurer,
        IRenderBackend renderer,
        UiHostOptions? options = null)
    {
        _component = component ?? throw new ArgumentNullException(nameof(component));
        ArgumentNullException.ThrowIfNull(textMeasurer);
        _renderer = renderer ?? throw new ArgumentNullException(nameof(renderer));
        _options = options ?? new UiHostOptions();
        _hostSynchronizationContext = SynchronizationContext.Current;
        _imageResolver = _options.ImageResolver;
        _layoutInterop = new LayoutInterop(textMeasurer);
        _renderingInterop = new RenderingInterop(textMeasurer, source =>
            _imageResolver?.TryGetDimensions(source, out var dimensions) == true
                ? (dimensions.Width, dimensions.Height) : null);
        _viewport = new SizeF(_options.Width, _options.Height);
        _nativeStyleRuntime = new StyleRuntimeInterop(_viewport.Width, _viewport.Height, _dpiScale);
        _transitionRuntime = new TransitionRuntimeInterop();
        _renderingInterop.SetTransitionRuntime(_transitionRuntime.Handle);
        _component.Invalidated += OnComponentInvalidated;
        if (_imageResolver is not null) _imageResolver.Changed += OnImageResourceChanged;
    }

    public UiHostOptions Options => _options;
    public DocumentNode? Document => _document;
    public LayoutResult? Layout => _layout;
    public ElementNode? FocusedElement => _focused;
    public bool NeedsRender => _markupDirty || _invalidation != VisualInvalidation.None || _hasPendingPointerMove;
    /// <summary>True while the CSS transition runtime is animating one or more properties.</summary>
    public bool HasActiveTransitions => _transitionRuntime?.HasActive == true;

    /// <summary>
    /// Advances the CSS transition runtime to <paramref name="nowMs"/> and schedules a display-list
    /// rebuild so the interpolated opacity/transform overrides reach the renderer. Returns true while
    /// transitions remain active (the clock should keep ticking). The frame on which the last
    /// transition finishes still schedules one final render so the end value is painted exactly once.
    /// </summary>
    public bool TickTransitions(long nowMs)
    {
        if (_transitionRuntime is null || !_transitionRuntime.HasActive)
        {
            return false;
        }

        _transitionRuntime.Tick(nowMs);
        InvalidateDisplayList();
        return _transitionRuntime.HasActive;
    }
    public float DpiScale => _dpiScale;
    /// <summary>Pipeline counters for deterministic performance regression tests.</summary>
    public UiDocumentHostMetrics Metrics => new(
        Interlocked.Read(ref _styleResolutionCount),
        Interlocked.Read(ref _styleUpdatedElementCount),
        Interlocked.Read(ref _layoutBuildCount),
        Interlocked.Read(ref _displayListBuildCount),
        Interlocked.Read(ref _paintChunkBuildCount),
        Interlocked.Read(ref _scrollCompositionCount),
        Interlocked.Read(ref _renderCallCount),
        Interlocked.Read(ref _incrementalRenderCallCount),
        Interlocked.Read(ref _visualStyleMapBuildCount),
        Interlocked.Read(ref _visualStyleMapUpdateCount),
        Interlocked.Read(ref _styleResolutionTimestampTicks),
        Interlocked.Read(ref _displayListBuildTimestampTicks),
        Interlocked.Read(ref _renderTimestampTicks),
        _lastStyleImpact);

    public void ResetMetrics()
    {
        Interlocked.Exchange(ref _styleResolutionCount, 0);
        Interlocked.Exchange(ref _styleUpdatedElementCount, 0);
        Interlocked.Exchange(ref _layoutBuildCount, 0);
        Interlocked.Exchange(ref _displayListBuildCount, 0);
        Interlocked.Exchange(ref _paintChunkBuildCount, 0);
        Interlocked.Exchange(ref _scrollCompositionCount, 0);
        Interlocked.Exchange(ref _renderCallCount, 0);
        Interlocked.Exchange(ref _incrementalRenderCallCount, 0);
        Interlocked.Exchange(ref _visualStyleMapBuildCount, 0);
        Interlocked.Exchange(ref _visualStyleMapUpdateCount, 0);
        Interlocked.Exchange(ref _styleResolutionTimestampTicks, 0);
        Interlocked.Exchange(ref _displayListBuildTimestampTicks, 0);
        Interlocked.Exchange(ref _renderTimestampTicks, 0);
        _lastStyleImpact = NativeStyleImpact.None;
    }

    public void SetViewport(int pixelWidth, int pixelHeight, float dpiScale)
    {
        ThrowIfDisposed();
        if (pixelWidth <= 0 || pixelHeight <= 0 || !float.IsFinite(dpiScale) || dpiScale <= 0)
        {
            return;
        }

        CancelScrollbarDrag();
        _dpiScale = dpiScale;
        _viewport = new SizeF(pixelWidth / dpiScale, pixelHeight / dpiScale);
        _renderer.Resize(pixelWidth, pixelHeight, dpiScale);
        InvalidateLayout();
    }

    private void EnsureVisualTree()
    {
        if (_markupDirty)
        {
            RebuildDocument();
        }

        if (_invalidation == VisualInvalidation.None && _displayList is not null)
        {
            return;
        }

        SyncNativeRuntimeValues();

        ResolveStyles();

        if ((_invalidation & VisualInvalidation.Layout) != 0)
        {
            // Native layout results borrow their resolved style map for layout-only
            // nodes such as anonymous flex items. Destroy the dependent layout
            // before replacing the style map; reversing this order leaves the
            // layout destructor holding a dangling native style-map pointer.
            _nativeLayout?.Dispose();
            _nativeLayout = null;
            _nativeLayoutStyleMap?.Dispose();
            _nativeLayoutStyleMap = _nativeStyleRuntime.CreateResolvedStyleMap(_nativeDom!.Native);
            _nativeLayout = _layoutInterop.CreateNativeLayout(_nativeDom, _nativeLayoutStyleMap, _viewport);
            Interlocked.Increment(ref _layoutBuildCount);
            _layout = _layoutInterop.Project(_nativeLayout, _nativeDom, _styles!, _viewport);
            RebuildScrollBoxIndex();
            _invalidation &= ~VisualInvalidation.Layout;
            _invalidation |= VisualInvalidation.DisplayList;
        }

        // ResolveStyles has consumed and cleared only the previous state changes.
        // Blur can now enqueue :focus changes without losing them to that cleanup.
        if (_focused is not null && !IsEligibleForFocus(_focused))
        {
            Focus(null);
            EnsureVisualTree();
            return;
        }

        if ((_invalidation & VisualInvalidation.PaintChunks) != 0)
        {
            if ((_invalidation & VisualInvalidation.DisplayList) == 0 && !TryPatchPaintChunks())
            {
                _invalidation |= VisualInvalidation.DisplayList;
            }
            _paintChunkDirtyNodes.Clear();
            _invalidation &= ~VisualInvalidation.PaintChunks;
        }

        if (BuildDisplayList())
        {
            EnsureVisualTree();
            return;
        }

        // Scroll currently remains backed by layout coordinates. The retained scroll command
        // cut consumes this bit without entering either branch above.
        _invalidation &= ~VisualInvalidation.Scroll;
    }

    private void RebuildDocument()
    {
        CancelScrollbarDrag();
        var focusedId = _focused?.Id;
        var hoveredId = _hovered?.Id;
        var activeId = _active?.Id;
        var markup = _component.RenderMarkup();
        var nativeDom = DomInterop.Parse(markup);
        var previousNativeDom = _nativeDom;
        _nativeDom = nativeDom;
        _document = nativeDom.Document;
        _styles = null;
        _nativeLayout?.Dispose();
        _nativeLayout = null;
        _nativeLayoutStyleMap?.Dispose();
        _nativeLayoutStyleMap = null;
        _nativeVisualStyleMap?.Dispose();
        _nativeVisualStyleMap = null;
        _layout = null;
        _displayList = null;
        _displayListContainsPatchedChunks = false;

        // The previous tree's ElementNode instances are discarded on every rebuild — this engine
        // has no cross-render node identity yet. Clearing here drops the now-stale ComputedStyle
        // entries for nodes that no longer exist. The styles are snapshotted by stable identity
        // first so state-triggered markup changes can still start CSS transitions.
        _previousStyleSnapshot = BuildStyleSnapshot();
        _computedStyles.Clear();
        _stateDirtyElements.Clear();
        _hoverChain.Clear();
        _scrollOffsets.Clear();
        _scrollOffsetsByStyleNode.Clear();
        _scrollBoxesByStyleNode.Clear();

        var authorCss = string.Join('\n', _document.GetElementsByTagName("style").Select(static element => element.TextContent));
        // The UA defaults sheet is native-owned: the style runtime applies it at the
        // engine-defaults layer on creation, so the host passes no defaults text here.
        _nativeStyleRuntime.Load(nativeDom.Native, string.Empty, authorCss);
        previousNativeDom?.Dispose();

        foreach (var input in _document.GetElementsByTagName("input"))
        {
            if (input.GetAttribute("value") is { } controlledValue)
            {
                input.RuntimeValue = controlledValue;
                if (input.Id is { Length: > 0 } controlledId)
                {
                    _uncontrolledInputValues[controlledId] = controlledValue;
                }
            }
            else if (input.Id is { Length: > 0 } id && _uncontrolledInputValues.TryGetValue(id, out var saved))
            {
                input.RuntimeValue = saved;
            }
        }

        _focused = RestoreElement(focusedId, ElementState.Focus);
        _hovered = RestoreElement(hoveredId, ElementState.Hover);
        RestoreHoverChain(_hovered);
        _active = RestoreElement(activeId, ElementState.Active);
        _markupDirty = false;
        _invalidation |= VisualInvalidation.Style;
    }

    private ElementNode? RestoreElement(string? id, ElementState state)
    {
        if (string.IsNullOrEmpty(id))
        {
            return null;
        }

        var element = _document!.GetElementById(id);
        if (element is not null)
        {
            element.State |= state;
        }

        return element;
    }

    private void RestoreHoverChain(ElementNode? target)
    {
        if (target is null)
        {
            return;
        }

        foreach (var element in target.AncestorsAndSelf())
        {
            element.State |= ElementState.Hover;
            _hoverChain.Add(element);
        }
    }

    private static IEnumerable<ElementNode> EnumerateElements(Node node)
    {
        foreach (var child in node.Children)
        {
            if (child is not ElementNode element) continue;
            yield return element;
            foreach (var descendant in EnumerateElements(element)) yield return descendant;
        }
    }

    private void OnComponentInvalidated(object? sender, EventArgs e)
    {
        _markupDirty = true;
        _invalidation |= VisualInvalidation.Style;
        RequestRender();
    }

    /// <summary>
    /// Starts a CSS transition for <paramref name="nodeHandle"/> when the new style declares one and
    /// a previous style exists. The native runtime compares the before/after opacity and transform
    /// matrix internally and ignores transitions whose values did not change.
    /// </summary>
    private void ObserveTransition(IntPtr nodeHandle, ComputedStyle before, ComputedStyle after)
    {
        if (after.TransitionDuration <= 0f || after.TransitionProperties == 0)
        {
            return;
        }

        TransitionStyleMarshaler.Observe(_transitionRuntime!, nodeHandle, before, after, Environment.TickCount64);
    }

    /// <summary>Maps the current computed styles to a stable identity so a rebuild can recover the "before" style.</summary>
    private Dictionary<string, ComputedStyle> BuildStyleSnapshot()
    {
        if (_computedStyles.Count == 0)
        {
            return [];
        }

        var snapshot = new Dictionary<string, ComputedStyle>(_computedStyles.Count);
        foreach (var (element, style) in _computedStyles)
        {
            var key = ElementIdentityKey(element);
            if (key is not null)
            {
                snapshot[key] = style;
            }
        }

        return snapshot;
    }

    /// <summary>
    /// Identity that survives a full document rebuild: element id when present, otherwise the
    /// tag+sibling-index path from the document root. ElementNode references themselves are not
    /// stable across a rebuild.
    /// </summary>
    private static string? ElementIdentityKey(ElementNode element)
    {
        if (element.Id is { Length: > 0 } id)
        {
            return "id:" + id;
        }

        var segments = new List<string>();
        for (ElementNode? current = element; current is not null; current = current.Parent as ElementNode)
        {
            var index = 0;
            if (current.Parent is { } parent)
            {
                for (var i = 0; i < parent.Children.Count; i++)
                {
                    if (ReferenceEquals(parent.Children[i], current))
                    {
                        index = i;
                        break;
                    }
                }
            }

            segments.Add($"{current.TagName}[{index}]");
        }

        segments.Reverse();
        return string.Join('/', segments);
    }

    private void InvalidateVisual(bool requestRender = true)
    {
        // Existing callers represent state that can affect selectors, layout, or both.
        // Scroll is deliberately not routed here: retained scroll composition has its own bit.
        _invalidation |= VisualInvalidation.Style;
        if (requestRender) RequestRender();
    }

    private void InvalidateLayout()
    {
        _invalidation |= VisualInvalidation.Layout;
        RequestRender();
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _component.Invalidated -= OnComponentInvalidated;
        if (_imageResolver is not null) _imageResolver.Changed -= OnImageResourceChanged;
        try
        {
            _component.Dispose();
        }
        finally
        {
            _nativeLayout?.Dispose();
            _nativeLayoutStyleMap?.Dispose();
            _nativeVisualStyleMap?.Dispose();
            _nativeStyleRuntime.Dispose();
            _layoutInterop.Dispose();
            _renderingInterop.Dispose();
            _transitionRuntime?.Dispose();
            _nativeDom?.Dispose();
            _renderer.Dispose();
        }

        GC.SuppressFinalize(this);
    }
}
