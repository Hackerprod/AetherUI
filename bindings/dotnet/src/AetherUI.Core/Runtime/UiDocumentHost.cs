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
public sealed class UiDocumentHost : IDisposable
{
    /// <summary>
    /// The native style runtime ships no hidden user-agent sheet by design — the host decides what
    /// default rules exist. This mirrors the defaults the old hand-rolled StyleResolver used to
    /// bake in, minus the "display: br" hack (line breaks are now special-cased by tag name in
    /// <see cref="StyleRuntimeBridge"/>, since "br" was never a real CSS display keyword).
    /// </summary>
    private const string EngineDefaultsCss = """
        html, body { display: block; }
        body { margin: 8px; color: #202124; font-family: "Segoe UI"; font-size: 16px; line-height: 1.25; }
        head, title, meta, link, style, script { display: none; }
        div, form, p, section, article, header, footer, main, nav, h1, h2, h3, h4, h5, h6 { display: block; }
        table { display: table; }
        tr { display: table-row; }
        td, th { display: table-cell; }
        span, label, strong, em, a { display: inline; }
        h1 { font-size: 2em; font-weight: 700; margin: .67em 0; }
        h2 { font-size: 1.5em; font-weight: 700; margin: .83em 0; }
        h3 { font-size: 1.17em; font-weight: 700; margin: 1em 0; }
        h4 { font-size: 1em; font-weight: 700; margin: 1.33em 0; }
        p { margin: 1em 0; }
        input, button { display: inline-block; font-family: "Segoe UI"; font-size: 14px; box-sizing: border-box; }
        input { min-width: 180px; padding: 6px 9px; border: 1px solid #9aa0a6; border-radius: 4px; background-color: white; color: #202124; cursor: text; }
        input:focus { border-color: #2563eb; }
        input:invalid { border-color: #dc2626; }
        button { min-width: 84px; padding: 6px 14px; border: 1px solid #9aa0a6; border-radius: 4px; background-color: white; cursor: pointer; text-align: center; }
        button:hover { background-color: #f1f5f9; }
        button:active { background-color: #e2e8f0; }
        button:disabled { opacity: .55; cursor: default; }
        """;

    private readonly UiComponent _component;
    private readonly UiHostOptions _options;
    private readonly IRenderBackend _renderer;
    private readonly LayoutInterop _layoutInterop;
    private readonly RenderingInterop _renderingInterop;
    private readonly StyleRuntimeInterop _nativeStyleRuntime;
    private readonly Dictionary<string, string> _uncontrolledInputValues = new(StringComparer.Ordinal);
    private readonly IImageResourceResolver? _imageResolver;
    private readonly SynchronizationContext? _hostSynchronizationContext;

    /// <summary>
    /// Two-axis scroll offset per scroll-container element. Most real markup scrolls a plain
    /// <c>&lt;div class="..."&gt;</c> with no id, so this is keyed by <see cref="ElementNode"/>
    /// instance (like <see cref="_computedStyles"/>) rather than by id — which means, like every
    /// other node-identity-dependent piece of state here, it resets on a markup rebuild instead of
    /// surviving it. Cleared alongside <see cref="_computedStyles"/> in RebuildDocument.
    /// </summary>
    private readonly Dictionary<ElementNode, ScrollOffset> _scrollOffsets = new();
    private readonly Dictionary<long, ScrollVisualOffset> _scrollOffsetsByStyleNode = new();
    private readonly Dictionary<long, LayoutBox> _scrollBoxesByStyleNode = new();

    private ScrollbarDrag? _scrollbarDrag;
    // A scrollbar press belongs to user-agent chrome, not DOM content. Keep this
    // state until release even if a resize/rebuild cancels the live drag.
    private bool _scrollbarPointerCaptured;

    /// <summary>
    /// Cleared only when the tree itself is rebuilt; repopulated in full from the native style
    /// runtime's resolve result on every visual pass.
    /// </summary>
    private readonly Dictionary<ElementNode, ComputedStyle> _computedStyles = new();
    private readonly HashSet<ElementNode> _stateDirtyElements = [];
    private readonly Dictionary<long, IntPtr> _paintChunkDirtyNodes = [];

    private DocumentNode? _document;
    private DomInterop? _nativeDom;
    private NativeStyleMap? _nativeLayoutStyleMap;
    private NativeStyleMap? _nativeVisualStyleMap;
    private NativeLayoutResult? _nativeLayout;
    private LayoutResult? _layout;
    private DisplayList? _displayList;
    private StyleMap? _styles;
    private ElementNode? _hovered;
    private readonly HashSet<ElementNode> _hoverChain = [];
    private ElementNode? _active;
    private ElementNode? _focused;
    private SizeF _viewport;
    private PointF _pendingPointerMove;
    private ElementNode? _pendingPointerTarget;
    private float _dpiScale = 1;
    private bool _markupDirty = true;
    private bool _runtimeValuesDirty;
    private bool _hasPendingPointerMove;
    private bool _hasPendingPointerTarget;
    private bool _pointerRenderPending;
    private bool _displayListContainsPatchedChunks;
    private CursorKind _currentCursor = CursorKind.Default;
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

    private enum ScrollAxis : byte
    {
        Horizontal,
        Vertical,
    }

    private sealed record ScrollbarDrag(ElementNode Container, ScrollAxis Axis, float GrabOffset);

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
        _component.Invalidated += OnComponentInvalidated;
        if (_imageResolver is not null) _imageResolver.Changed += OnImageResourceChanged;
    }

    public event EventHandler? RenderRequested;

    public UiHostOptions Options => _options;
    public DocumentNode? Document => _document;
    public LayoutResult? Layout => _layout;
    public ElementNode? FocusedElement => _focused;
    public bool NeedsRender => _markupDirty || _invalidation != VisualInvalidation.None || _hasPendingPointerMove;
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

    public void Render()
    {
        ThrowIfDisposed();
        var initialPlan = EvaluateRenderPlan();
        var scrollInvalidated = (initialPlan.NormalizedInvalidation & (uint)VisualInvalidation.Scroll) != 0;
        var scrollOnly = initialPlan.UseScrollComposition != 0;
        var pointerRenderRequested = _pointerRenderPending;
        var pointerOnlyRender = pointerRenderRequested && initialPlan.PointerOnlyRender != 0;
        var displayListBuildsBefore = Interlocked.Read(ref _displayListBuildCount);
        var paintChunkBuildsBefore = Interlocked.Read(ref _paintChunkBuildCount);
        EnsureVisualTree();
        if (ApplyPendingPointerMove())
        {
            scrollOnly = false;
            EnsureVisualTree();
        }
        var renderPlan = EvaluateRenderPlan();

        _pointerRenderPending = false;
        var displayListChanged =
            Interlocked.Read(ref _displayListBuildCount) != displayListBuildsBefore ||
            Interlocked.Read(ref _paintChunkBuildCount) != paintChunkBuildsBefore;
        if ((pointerOnlyRender || pointerRenderRequested) && !displayListChanged && !scrollOnly)
        {
            _options.MetricsSink?.Invoke(Metrics);
            return;
        }

        if (_imageResolver is not null)
            foreach (var image in _displayList!.Commands.OfType<DrawBackgroundImageCommand>())
                _imageResolver.Request(image.Source);
        _renderer.SetScrollVisualStateProvider(ScrollVisualStateFor);
        // Only a pure scroll frame may use retained scroll composition. A frame that also
        // rebuilt or patched paint must first publish a canonical full frame, otherwise the
        // renderer would composite new scroll offsets over a retained bitmap from a different
        // property state. This mirrors Chromium's split between active compositor property
        // updates and main-thread commits without pretending that a flat command list is a
        // complete property tree.
        _renderer.SetScrollCompositionMode(scrollOnly);
        var renderStart = Stopwatch.GetTimestamp();
        if (!scrollInvalidated &&
            !scrollOnly &&
            renderPlan.AllowIncrementalRender != 0 &&
            _renderer is IIncrementalRenderBackend incrementalRenderer &&
            incrementalRenderer.TryRenderIncremental(_displayList!, _options.ClearColor))
        {
            Interlocked.Increment(ref _incrementalRenderCallCount);
        }
        else
        {
            _renderer.Render(_displayList!, _options.ClearColor);
        }
        Interlocked.Add(ref _renderTimestampTicks, Stopwatch.GetTimestamp() - renderStart);
        Interlocked.Increment(ref _renderCallCount);
        if (scrollOnly) Interlocked.Increment(ref _scrollCompositionCount);
        _options.MetricsSink?.Invoke(Metrics);
    }

    public CursorKind PointerMoved(PointF point)
    {
        ThrowIfDisposed();
        if (_scrollbarDrag is not null)
        {
            EnsureVisualTree();
            UpdateScrollbarDrag(point);
            return _currentCursor;
        }

        if (CanHitTestCurrentVisualTree())
        {
            var target = HitTest(point);
            _currentCursor = CursorFor(target);
            if (ReferenceEquals(target, _hovered))
            {
                _hasPendingPointerMove = false;
                _hasPendingPointerTarget = false;
                _pendingPointerTarget = null;
                return _currentCursor;
            }

            _pendingPointerTarget = target;
            _hasPendingPointerTarget = true;
        }
        else
        {
            _pendingPointerTarget = null;
            _hasPendingPointerTarget = false;
        }

        _pendingPointerMove = point;
        _hasPendingPointerMove = true;
        RequestPointerRender();
        return _currentCursor;
    }

    private bool CanHitTestCurrentVisualTree() =>
        !_markupDirty &&
        (_invalidation & VisualInvalidation.Layout) == 0 &&
        _layout is not null &&
        _styles is not null;

    private bool ApplyPendingPointerMove()
    {
        if (!_hasPendingPointerMove)
        {
            return false;
        }

        var point = _pendingPointerMove;
        var target = _hasPendingPointerTarget ? _pendingPointerTarget : HitTest(point);
        _hasPendingPointerMove = false;
        _hasPendingPointerTarget = false;
        _pendingPointerTarget = null;
        _currentCursor = CursorFor(target);
        return UpdateHoverTarget(target, point, requestRender: false);
    }

    private bool UpdateHoverTarget(ElementNode? target, PointF point, bool requestRender)
    {
        if (ReferenceEquals(target, _hovered))
        {
            return false;
        }

        var previous = _hovered;
        var nextChain = target is null
            ? Array.Empty<ElementNode>()
            : target.AncestorsAndSelf().ToArray();
        var nextSet = nextChain.Length == 0
            ? null
            : new HashSet<ElementNode>(nextChain);

        var stateChanged = false;
        foreach (var element in _hoverChain.ToArray())
        {
            if (nextSet is not null && nextSet.Contains(element))
            {
                continue;
            }

            stateChanged |= SetElementState(element, ElementState.Hover, enabled: false);
            _hoverChain.Remove(element);
        }

        foreach (var element in nextChain)
        {
            if (_hoverChain.Add(element))
            {
                stateChanged |= SetElementState(element, ElementState.Hover, enabled: true);
            }
        }

        if (previous is not null)
        {
            Dispatch("mouseout", previous, point);
        }

        _hovered = target;
        if (_hovered is not null)
        {
            Dispatch("mouseover", _hovered, point);
        }

        if (stateChanged)
        {
            InvalidateVisual(requestRender);
        }

        return stateChanged;
    }

    public void PointerLeft()
    {
        ThrowIfDisposed();
        _hasPendingPointerMove = false;
        _hasPendingPointerTarget = false;
        _pendingPointerTarget = null;
        _currentCursor = CursorKind.Default;
        if (_hovered is null)
        {
            return;
        }

        var previous = _hovered;
        _hovered = null;
        var stateChanged = false;
        foreach (var element in _hoverChain.ToArray())
        {
            stateChanged |= SetElementState(element, ElementState.Hover, enabled: false);
            _hoverChain.Remove(element);
        }
        Dispatch("mouseout", previous, default);
        if (stateChanged)
        {
            InvalidateVisual();
        }
    }

    public void PointerPressed(PointF point)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        if (TryBeginScrollbarInteraction(point))
        {
            return;
        }
        var target = HitTest(point);
        if (target is null || target.IsDisabled)
        {
            Focus(null);
            return;
        }

        if (_active is not null)
        {
            SetElementState(_active, ElementState.Active, enabled: false);
        }

        _active = target;
        SetElementState(_active, ElementState.Active, enabled: true);
        Focus(target.IsFocusable ? target : null);
        Dispatch("mousedown", target, point);
        InvalidateVisual();
    }

    public void PointerReleased(PointF point)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        if (_scrollbarPointerCaptured)
        {
            _scrollbarDrag = null;
            _scrollbarPointerCaptured = false;
            InvalidateScroll();
            return;
        }
        var target = HitTest(point);
        var pressed = _active;
        if (pressed is not null)
        {
            SetElementState(pressed, ElementState.Active, enabled: false);
            Dispatch("mouseup", pressed, point);
        }

        _active = null;
        if (pressed is not null && ReferenceEquals(pressed, target) && !pressed.IsDisabled)
        {
            var click = Dispatch("click", pressed, point);
            if (!click.IsDefaultPrevented && pressed.TagName == "button" &&
                !string.Equals(pressed.GetAttribute("type"), "button", StringComparison.OrdinalIgnoreCase))
            {
                SubmitClosestForm(pressed);
            }
        }

        InvalidateVisual();
    }

    public void TextInput(char character)
    {
        ThrowIfDisposed();
        if (_focused is not { TagName: "input" } input || input.IsDisabled || char.IsControl(character))
        {
            return;
        }

        var current = input.RuntimeValue ?? input.GetAttribute("value") ?? string.Empty;
        var maxLength = ParsePositiveInt(input.GetAttribute("maxlength"));
        if (maxLength is not null && current.Length >= maxLength.Value)
        {
            return;
        }

        SetInputValue(input, current + character);
        var uiEvent = Dispatch("input", input, default, character: character);
        if (uiEvent.IsDefaultPrevented)
        {
            SetInputValue(input, current);
        }

        InvalidateLayout();
    }

    public void KeyPressed(UiKey key)
    {
        ThrowIfDisposed();
        if (_focused is null)
        {
            return;
        }

        var keyEvent = Dispatch("keydown", _focused, default, key);
        if (keyEvent.IsDefaultPrevented)
        {
            return;
        }

        if (key == UiKey.Tab)
        {
            FocusNext();
            return;
        }

        if (_focused.TagName != "input" || _focused.IsDisabled)
        {
            if (key is UiKey.Enter or UiKey.Space && _focused.TagName == "button")
            {
                var click = Dispatch("click", _focused, default, key);
                if (!click.IsDefaultPrevented &&
                    !string.Equals(_focused.GetAttribute("type"), "button", StringComparison.OrdinalIgnoreCase))
                {
                    SubmitClosestForm(_focused);
                }
            }

            return;
        }

        var input = _focused;
        var current = input.RuntimeValue ?? input.GetAttribute("value") ?? string.Empty;
        if (key is UiKey.Backspace or UiKey.Delete)
        {
            if (current.Length == 0)
            {
                return;
            }

            SetInputValue(input, current[..^1]);
            Dispatch("input", input, default, key);
            InvalidateLayout();
        }
        else if (key == UiKey.Enter)
        {
            SubmitClosestForm(input);
        }
    }

    public CursorKind CursorAt(PointF point)
    {
        ThrowIfDisposed();
        if (_hasPendingPointerMove)
        {
            return _currentCursor;
        }

        if (!CanHitTestCurrentVisualTree())
        {
            return CursorKind.Default;
        }

        return CursorFor(HitTest(point));
    }

    private CursorKind CursorFor(ElementNode? target) =>
        target is not null && _styles is not null && _styles.TryGetValue(target, out var style)
            ? style.Cursor
            : CursorKind.Default;

    private void SubmitClosestForm(ElementNode source)
    {
        var form = source.Closest("form");
        if (form is null)
        {
            return;
        }

        if (!ValidateForm(form))
        {
            InvalidateVisual();
            return;
        }

        Dispatch("submit", form, default);
    }

    private bool ValidateForm(ElementNode form)
    {
        var valid = true;
        foreach (var input in form.DescendantElements().Where(static element => element.TagName == "input" && element.IsRequired))
        {
            var value = input.RuntimeValue ?? input.GetAttribute("value") ?? string.Empty;
            var invalid = string.IsNullOrWhiteSpace(value);
            if (invalid)
            {
                SetElementState(input, ElementState.Invalid, enabled: true);
                Dispatch("invalid", input, default);
                valid = false;
            }
            else
            {
                SetElementState(input, ElementState.Invalid, enabled: false);
            }
        }

        return valid;
    }

    private UiEvent Dispatch(string type, ElementNode target, PointF point, UiKey key = UiKey.None, char character = '\0')
    {
        var uiEvent = new UiEvent(type, target)
        {
            PointerPosition = point,
            Key = key,
            Character = character,
            Value = target.RuntimeValue ?? target.GetAttribute("value"),
        };

        for (ElementNode? current = target; current is not null; current = current.Parent as ElementNode)
        {
            uiEvent.CurrentTarget = current;
            var token = current.GetAttribute($"on{type}");
            if (token is { Length: > 0 } && _component.TryGetEventHandler(token, out var handler))
            {
                handler(uiEvent);
            }

            if (uiEvent.IsPropagationStopped)
            {
                break;
            }
        }

        return uiEvent;
    }

    private void Focus(ElementNode? target)
    {
        if (ReferenceEquals(target, _focused))
        {
            return;
        }

        var previous = _focused;
        if (previous is not null)
        {
            SetElementState(previous, ElementState.Focus, enabled: false);
            Dispatch("blur", previous, default);
        }

        _focused = target;
        if (_focused is not null)
        {
            SetElementState(_focused, ElementState.Focus, enabled: true);
            Dispatch("focus", _focused, default);
        }

        InvalidateVisual();
    }

    private void FocusNext()
    {
        EnsureVisualTree();
        var focusable = _document!.DescendantElements()
            .Where(static element => element.IsFocusable && !element.IsDisabled)
            .ToArray();
        if (focusable.Length == 0)
        {
            Focus(null);
            return;
        }

        var index = Array.IndexOf(focusable, _focused);
        Focus(focusable[(index + 1 + focusable.Length) % focusable.Length]);
    }

    private void SetInputValue(ElementNode input, string value)
    {
        input.RuntimeValue = value;
        _runtimeValuesDirty = true;
        if (input.Id is { Length: > 0 } id)
        {
            _uncontrolledInputValues[id] = value;
        }

        if (input.IsRequired && !string.IsNullOrWhiteSpace(value))
        {
            if (SetElementState(input, ElementState.Invalid, enabled: false))
                InvalidateVisual(requestRender: false);
        }
    }

    private bool SetElementState(ElementNode element, ElementState flag, bool enabled)
    {
        var previous = element.State;
        var next = enabled ? previous | flag : previous & ~flag;
        if (next == previous)
        {
            return false;
        }

        element.State = next;
        _stateDirtyElements.Add(element);
        return true;
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

        if (_runtimeValuesDirty && _nativeDom is not null &&
            (_invalidation & (VisualInvalidation.Style | VisualInvalidation.Layout | VisualInvalidation.DisplayList)) != 0)
        {
            _nativeDom.SyncRuntimeValues();
            _runtimeValuesDirty = false;
        }

        if ((_invalidation & VisualInvalidation.Style) != 0)
        {
            var styleResolutionStart = Stopwatch.GetTimestamp();
            NativeStyleImpact impact;
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
                        _computedStyles[managed] = StyleRuntimeBridge.ToComputedStyle(update.Style, managed);
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
                    _computedStyles[elements[i]] = StyleRuntimeBridge.ToComputedStyle(nativeStyles[i], elements[i]);
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
        }

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

        if ((_invalidation & VisualInvalidation.PaintChunks) != 0)
        {
            if (!TryPatchPaintChunks())
            {
                _invalidation |= VisualInvalidation.DisplayList;
            }
            _paintChunkDirtyNodes.Clear();
            _invalidation &= ~VisualInvalidation.PaintChunks;
        }

        if ((_invalidation & VisualInvalidation.DisplayList) != 0)
        {
            if (_nativeLayout is null)
            {
                _invalidation |= VisualInvalidation.Layout;
                EnsureVisualTree();
                return;
            }

            if (_nativeVisualStyleMap is null)
                _nativeVisualStyleMap = _nativeStyleRuntime.CreateResolvedStyleMap(_nativeDom!.Native);
            var displayListStart = Stopwatch.GetTimestamp();
            _displayList = AttachResolvedImages(_renderingInterop.Build(_nativeLayout, _nativeVisualStyleMap,
                _focused is null ? IntPtr.Zero : _nativeDom!.GetNativeHandle(_focused)));
            _displayListContainsPatchedChunks = false;
            Interlocked.Add(ref _displayListBuildTimestampTicks, Stopwatch.GetTimestamp() - displayListStart);
            Interlocked.Increment(ref _displayListBuildCount);
            _invalidation &= ~VisualInvalidation.DisplayList;
        }

        // Scroll currently remains backed by layout coordinates. The retained scroll command
        // cut consumes this bit without entering either branch above.
        _invalidation &= ~VisualInvalidation.Scroll;
    }

    private ScrollOffset ScrollOffsetFor(ElementNode element) =>
        _scrollOffsets.TryGetValue(element, out var offset) ? offset : default;

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

    public ScrollOffset GetScrollOffset(ElementNode element)
    {
        ArgumentNullException.ThrowIfNull(element);
        return ScrollOffsetFor(element);
    }

    private ElementNode? HitTest(PointF point)
    {
        if (_nativeLayout is not null && _nativeDom is not null)
        {
            var nativeHit = _layoutInterop.HitTest(_nativeLayout, _nativeVisualStyleMap, _nativeDom, point, ScrollOffsetFor);
            if (nativeHit is not null) return nativeHit;
        }

        return _layout?.HitTest(point, ScrollOffsetFor);
    }

    private ScrollVisualState ScrollVisualStateFor(long styleNodeId)
    {
        var offset = _scrollOffsetsByStyleNode.GetValueOrDefault(styleNodeId);
        var chrome = _scrollBoxesByStyleNode.TryGetValue(styleNodeId, out var box)
            ? ToScrollbarChrome(_layoutInterop.GetScrollbarGeometryAtOffset(
                box,
                new ScrollOffset(offset.X, offset.Y)))
            : default;
        return new ScrollVisualState(offset, chrome);
    }

    private void RebuildScrollBoxIndex()
    {
        _scrollBoxesByStyleNode.Clear();
        var liveScrollElements = new HashSet<ElementNode>();
        if (_layout is null) return;
        foreach (var box in _layout.Root.DescendantsAndSelf())
        {
            if (box.StyleNodeId == 0) continue;
            if (IsScrollContainer(box))
            {
                _scrollBoxesByStyleNode[box.StyleNodeId] = box;
                liveScrollElements.Add(box.Element);
                if (_scrollOffsets.TryGetValue(box.Element, out var offset))
                {
                    offset = new ScrollOffset(
                        Math.Clamp(offset.X, 0f, box.ScrollableOverflowX),
                        Math.Clamp(offset.Y, 0f, box.ScrollableOverflowY));
                    _scrollOffsets[box.Element] = offset;
                    _scrollOffsetsByStyleNode[box.StyleNodeId] = new ScrollVisualOffset(offset.X, offset.Y);
                }
            }
        }

        foreach (var element in _scrollOffsets.Keys.Where(element => !liveScrollElements.Contains(element)).ToArray())
        {
            _scrollOffsets.Remove(element);
            if (_nativeDom is not null) _scrollOffsetsByStyleNode.Remove(_nativeDom.GetStyleNodeId(element));
        }
    }

    private static ScrollbarChrome ToScrollbarChrome(ScrollbarGeometry geometry) => new(
        geometry.HorizontalVisible,
        geometry.VerticalVisible,
        geometry.HorizontalTrack,
        geometry.HorizontalThumb,
        geometry.VerticalTrack,
        geometry.VerticalThumb,
        geometry.Corner);

    private DisplayList AttachResolvedImages(DisplayList displayList)
    {
        if (_imageResolver is null) return displayList;
        return new DisplayList(displayList.Commands.Select(command => command is DrawBackgroundImageCommand image &&
            _imageResolver.TryGetResource(image.Source, out var resource)
                ? image with { Resource = resource }
                : command).ToArray(), displayList.PaintChunks);
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

    /// <summary>
    /// Routes a mouse-wheel notch to the nearest scrollable ancestor of whatever's under the
    /// cursor (walking up like <see cref="Dispatch"/> does for bubbling DOM events), matching how
    /// a real browser picks which scroll container a wheel event affects.
    /// </summary>
    public void MouseWheelScrolled(PointF point, float notches)
    {
        ScrollByWheel(point, ScrollAxis.Vertical, -notches * 48f);
    }

    /// <summary>Routes a horizontal wheel gesture, including Shift+wheel, through nested overflow.</summary>
    public void MouseHorizontalWheelScrolled(PointF point, float notches)
    {
        // WM_MOUSEHWHEEL reports positive deltas for motion towards the right.
        ScrollByWheel(point, ScrollAxis.Horizontal, notches * 48f);
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
        // entries for nodes that no longer exist.
        _computedStyles.Clear();
        _stateDirtyElements.Clear();
        _hoverChain.Clear();
        _scrollOffsets.Clear();
        _scrollOffsetsByStyleNode.Clear();
        _scrollBoxesByStyleNode.Clear();

        var authorCss = string.Join('\n', _document.GetElementsByTagName("style").Select(static element => element.TextContent));
        _nativeStyleRuntime.Load(nativeDom.Native, EngineDefaultsCss, authorCss);
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

    private void ScrollByWheel(PointF point, ScrollAxis axis, float delta)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        for (var current = HitTest(point); current is not null; current = current.Parent as ElementNode)
        {
            var box = _layout!.FindPrimaryBox(current);
            if (box is null) continue;
            if (!IsUserScrollable(box.Style, axis)) continue;

            var range = ScrollRange(box, axis);
            if (range <= 0) continue;
            var currentOffset = ScrollOffsetOnAxis(current, axis);
            var next = Math.Clamp(currentOffset + delta, 0f, range);
            // At a limit this container cannot consume the gesture: chain it to
            // the nearest ancestor rather than swallowing it.
            if (MathF.Abs(next - currentOffset) < .001f) continue;

            SetScrollOffset(current, box, axis, next);
            InvalidateScroll();
            return;
        }
    }

    private bool TryBeginScrollbarInteraction(PointF point)
    {
        foreach (var box in _layout!.PaintOrder.Reverse())
        {
            var scrollbar = ScrollbarGeometryForPointer(box);
            if (scrollbar.HorizontalVisible && scrollbar.HorizontalThumb.Contains(point))
            {
                BeginScrollbarDrag(box, ScrollAxis.Horizontal, point.X - scrollbar.HorizontalThumb.X);
                return true;
            }
            if (scrollbar.VerticalVisible && scrollbar.VerticalThumb.Contains(point))
            {
                BeginScrollbarDrag(box, ScrollAxis.Vertical, point.Y - scrollbar.VerticalThumb.Y);
                return true;
            }
            if (scrollbar.HorizontalVisible && scrollbar.HorizontalTrack.Contains(point))
            {
                PageScroll(box, ScrollAxis.Horizontal, point.X);
                return true;
            }
            if (scrollbar.VerticalVisible && scrollbar.VerticalTrack.Contains(point))
            {
                PageScroll(box, ScrollAxis.Vertical, point.Y);
                return true;
            }
        }
        return false;
    }

    private void BeginScrollbarDrag(LayoutBox box, ScrollAxis axis, float grabOffset)
    {
        _scrollbarDrag = new ScrollbarDrag(box.Element, axis, grabOffset);
        _scrollbarPointerCaptured = true;
    }

    private void UpdateScrollbarDrag(PointF point)
    {
        var drag = _scrollbarDrag;
        if (drag is null || _layout?.FindPrimaryBox(drag.Container) is not { } box)
        {
            CancelScrollbarDrag();
            return;
        }

        var scrollbar = ScrollbarGeometryForPointer(box);
        var track = drag.Axis == ScrollAxis.Horizontal ? scrollbar.HorizontalTrack : scrollbar.VerticalTrack;
        var thumb = drag.Axis == ScrollAxis.Horizontal ? scrollbar.HorizontalThumb : scrollbar.VerticalThumb;
        var trackStart = drag.Axis == ScrollAxis.Horizontal ? track.X : track.Y;
        var trackLength = drag.Axis == ScrollAxis.Horizontal ? track.Width : track.Height;
        var thumbLength = drag.Axis == ScrollAxis.Horizontal ? thumb.Width : thumb.Height;
        var pointer = drag.Axis == ScrollAxis.Horizontal ? point.X : point.Y;
        var available = Math.Max(0f, trackLength - thumbLength);
        var thumbStart = Math.Clamp(pointer - drag.GrabOffset, trackStart, trackStart + available);
        var progress = available > 0 ? (thumbStart - trackStart) / available : 0;
        SetScrollOffset(drag.Container, box, drag.Axis, progress * ScrollRange(box, drag.Axis));
        InvalidateScroll();
    }

    private void PageScroll(LayoutBox box, ScrollAxis axis, float pointer)
    {
        var scrollbar = ScrollbarGeometryForPointer(box);
        var thumb = axis == ScrollAxis.Horizontal ? scrollbar.HorizontalThumb : scrollbar.VerticalThumb;
        var thumbStart = axis == ScrollAxis.Horizontal ? thumb.X : thumb.Y;
        var thumbEnd = axis == ScrollAxis.Horizontal ? thumb.Right : thumb.Bottom;
        var direction = pointer < thumbStart ? -1f : pointer > thumbEnd ? 1f : 0f;
        if (direction == 0) return;

        var page = axis == ScrollAxis.Horizontal ? box.ContentBox.Width : box.ContentBox.Height;
        var current = ScrollOffsetOnAxis(box.Element, axis);
        var next = Math.Clamp(current + direction * page, 0f, ScrollRange(box, axis));
        if (MathF.Abs(next - current) < .001f) return;
        _scrollbarPointerCaptured = true;
        SetScrollOffset(box.Element, box, axis, next);
        InvalidateScroll();
    }

    private void SetScrollOffset(ElementNode element, LayoutBox box, ScrollAxis axis, float value)
    {
        var current = ScrollOffsetFor(element);
        _scrollOffsets[element] = axis == ScrollAxis.Horizontal
            ? new ScrollOffset(value, current.Y)
            : new ScrollOffset(current.X, value);
        var offset = _scrollOffsets[element];
        _scrollOffsetsByStyleNode[_nativeDom!.GetStyleNodeId(element)] = new ScrollVisualOffset(offset.X, offset.Y);
    }

    private static float ScrollRange(LayoutBox box, ScrollAxis axis) =>
        axis == ScrollAxis.Horizontal ? box.ScrollableOverflowX : box.ScrollableOverflowY;

    private static bool IsScrollContainer(LayoutBox box) =>
        (IsUserScrollable(box.Style, ScrollAxis.Horizontal) && box.ScrollableOverflowX > 0) ||
        (IsUserScrollable(box.Style, ScrollAxis.Vertical) && box.ScrollableOverflowY > 0) ||
        box.Scrollbar.HorizontalVisible ||
        box.Scrollbar.VerticalVisible;

    private static bool IsUserScrollable(ComputedStyle style, ScrollAxis axis)
    {
        var overflow = axis == ScrollAxis.Horizontal ? style.OverflowX : style.OverflowY;
        return overflow is OverflowKind.Auto or OverflowKind.Scroll;
    }

    private float ScrollOffsetOnAxis(ElementNode element, ScrollAxis axis)
    {
        var offset = ScrollOffsetFor(element);
        return axis == ScrollAxis.Horizontal ? offset.X : offset.Y;
    }

    private ScrollbarGeometry ScrollbarGeometryForPointer(LayoutBox box) =>
        TranslateScrollbarGeometry(
            _layoutInterop.GetScrollbarGeometryAtOffset(box, ScrollOffsetFor(box.Element)),
            AncestorScrollTranslation(box.Element));

    private PointF AncestorScrollTranslation(ElementNode element)
    {
        var x = 0f;
        var y = 0f;
        for (var ancestor = element.Parent as ElementNode; ancestor is not null; ancestor = ancestor.Parent as ElementNode)
        {
            var offset = ScrollOffsetFor(ancestor);
            x -= offset.X;
            y -= offset.Y;
        }

        return new PointF(x, y);
    }

    private static ScrollbarGeometry TranslateScrollbarGeometry(ScrollbarGeometry geometry, PointF delta) => new(
        geometry.HorizontalVisible,
        geometry.VerticalVisible,
        Translate(geometry.HorizontalTrack, delta),
        Translate(geometry.HorizontalThumb, delta),
        Translate(geometry.VerticalTrack, delta),
        Translate(geometry.VerticalThumb, delta),
        Translate(geometry.Corner, delta));

    private static RectF Translate(RectF rect, PointF delta) =>
        rect.IsEmpty ? rect : new RectF(rect.X + delta.X, rect.Y + delta.Y, rect.Width, rect.Height);

    private void CancelScrollbarDrag() => _scrollbarDrag = null;

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

    private void InvalidateVisual(bool requestRender = true)
    {
        // Existing callers represent state that can affect selectors, layout, or both.
        // Scroll is deliberately not routed here: retained scroll composition has its own bit.
        _invalidation |= VisualInvalidation.Style;
        if (requestRender) RequestRender();
    }

    private NativeRenderPlan EvaluateRenderPlan()
    {
        var input = new NativeRenderPlanInput(
            (uint)_invalidation,
            _markupDirty ? 1 : 0,
            _hasPendingPointerMove ? 1 : 0,
            _displayList is not null ? 1 : 0,
            _displayListContainsPatchedChunks ? 1 : 0,
            _renderer is IIncrementalRenderBackend ? 1 : 0);
        return NativeRenderPlanInterop.Evaluate(input);
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

    private void InvalidateLayout()
    {
        _invalidation |= VisualInvalidation.Layout;
        RequestRender();
    }

    private void InvalidateDisplayList()
    {
        _invalidation |= VisualInvalidation.DisplayList;
        RequestRender();
    }

    private void InvalidateScroll()
    {
        _invalidation |= (VisualInvalidation)NativeRenderPlanInterop.VisualInvalidationForScroll(
            _displayListContainsPatchedChunks);
        RequestRender();
    }

    private void RequestRender() => RenderRequested?.Invoke(this, EventArgs.Empty);

    private void RequestPointerRender()
    {
        if (_pointerRenderPending)
        {
            return;
        }

        _pointerRenderPending = true;
        RequestRender();
    }

    private void OnImageResourceChanged(object? sender, ImageResourceChangedEventArgs e)
    {
        if (_hostSynchronizationContext is not null && SynchronizationContext.Current != _hostSynchronizationContext)
        {
            _hostSynchronizationContext.Post(static state => ((UiDocumentHost)state!).InvalidateDisplayList(), this);
            return;
        }
        InvalidateDisplayList();
    }

    private static int? ParsePositiveInt(string? value) =>
        int.TryParse(value, out var result) && result >= 0 ? result : null;

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
            _nativeDom?.Dispose();
            _renderer.Dispose();
        }

        GC.SuppressFinalize(this);
    }
}
