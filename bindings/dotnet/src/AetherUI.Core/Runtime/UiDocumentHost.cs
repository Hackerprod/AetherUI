using AetherUI.Css;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;

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
    /// Vertical scroll offset per scroll-container element. Most real markup scrolls a plain
    /// <c>&lt;div class="..."&gt;</c> with no id, so this is keyed by <see cref="ElementNode"/>
    /// instance (like <see cref="_computedStyles"/>) rather than by id — which means, like every
    /// other node-identity-dependent piece of state here, it resets on a markup rebuild instead of
    /// surviving it. Cleared alongside <see cref="_computedStyles"/> in RebuildDocument.
    /// </summary>
    private readonly Dictionary<ElementNode, float> _scrollOffsets = new();

    /// <summary>
    /// Cleared only when the tree itself is rebuilt; repopulated in full from the native style
    /// runtime's resolve result on every visual pass.
    /// </summary>
    private readonly Dictionary<ElementNode, ComputedStyle> _computedStyles = new();

    private DocumentNode? _document;
    private DomInterop? _nativeDom;
    private LayoutResult? _layout;
    private DisplayList? _displayList;
    private StyleMap? _styles;
    private ElementNode? _hovered;
    private ElementNode? _active;
    private ElementNode? _focused;
    private SizeF _viewport;
    private float _dpiScale = 1;
    private bool _markupDirty = true;
    private bool _visualDirty = true;
    private bool _disposed;

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
    public bool NeedsRender => _markupDirty || _visualDirty;
    public float DpiScale => _dpiScale;

    public void SetViewport(int pixelWidth, int pixelHeight, float dpiScale)
    {
        ThrowIfDisposed();
        if (pixelWidth <= 0 || pixelHeight <= 0 || !float.IsFinite(dpiScale) || dpiScale <= 0)
        {
            return;
        }

        _dpiScale = dpiScale;
        _viewport = new SizeF(pixelWidth / dpiScale, pixelHeight / dpiScale);
        _renderer.Resize(pixelWidth, pixelHeight, dpiScale);
        InvalidateVisual();
    }

    public void Render()
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        if (_imageResolver is not null)
            foreach (var image in _displayList!.Commands.OfType<DrawBackgroundImageCommand>())
                _imageResolver.Request(image.Source);
        _renderer.Render(_displayList!, _options.ClearColor);
        _visualDirty = false;
    }

    public void PointerMoved(PointF point)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        var target = _layout!.HitTest(point);
        if (ReferenceEquals(target, _hovered))
        {
            return;
        }

        var previous = _hovered;
        if (previous is not null)
        {
            previous.State &= ~ElementState.Hover;
            Dispatch("mouseout", previous, point);
        }

        _hovered = target;
        if (_hovered is not null)
        {
            _hovered.State |= ElementState.Hover;
            Dispatch("mouseover", _hovered, point);
        }

        InvalidateVisual();
    }

    public void PointerLeft()
    {
        ThrowIfDisposed();
        if (_hovered is null)
        {
            return;
        }

        var previous = _hovered;
        _hovered = null;
        previous.State &= ~ElementState.Hover;
        Dispatch("mouseout", previous, default);
        InvalidateVisual();
    }

    public void PointerPressed(PointF point)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        var target = _layout!.HitTest(point);
        if (target is null || target.IsDisabled)
        {
            Focus(null);
            return;
        }

        if (_active is not null)
        {
            _active.State &= ~ElementState.Active;
        }

        _active = target;
        _active.State |= ElementState.Active;
        Focus(target.IsFocusable ? target : null);
        Dispatch("mousedown", target, point);
        InvalidateVisual();
    }

    public void PointerReleased(PointF point)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        var target = _layout!.HitTest(point);
        var pressed = _active;
        if (pressed is not null)
        {
            pressed.State &= ~ElementState.Active;
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

        InvalidateVisual();
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
            InvalidateVisual();
        }
        else if (key == UiKey.Enter)
        {
            SubmitClosestForm(input);
        }
    }

    public CursorKind CursorAt(PointF point)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        var target = _layout!.HitTest(point);
        return target is not null && _styles!.TryGetValue(target, out var style)
            ? style.Cursor
            : CursorKind.Default;
    }

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
                input.State |= ElementState.Invalid;
                Dispatch("invalid", input, default);
                valid = false;
            }
            else
            {
                input.State &= ~ElementState.Invalid;
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
            previous.State &= ~ElementState.Focus;
            Dispatch("blur", previous, default);
        }

        _focused = target;
        if (_focused is not null)
        {
            _focused.State |= ElementState.Focus;
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
        if (input.Id is { Length: > 0 } id)
        {
            _uncontrolledInputValues[id] = value;
        }

        if (input.IsRequired && !string.IsNullOrWhiteSpace(value))
        {
            input.State &= ~ElementState.Invalid;
        }
    }

    private void EnsureVisualTree()
    {
        if (_markupDirty)
        {
            RebuildDocument();
        }

        if (!_visualDirty && _displayList is not null)
        {
            return;
        }

        _nativeDom!.SyncRuntimeValues();
        var elements = EnumerateElements(_document!).ToArray();
        var nativeStyles = _nativeStyleRuntime.Resolve(
            elements.Select(static element => (int)element.State).ToArray(),
            _viewport.Width, _viewport.Height, _dpiScale);
        for (var i = 0; i < elements.Length; i++)
        {
            _computedStyles[elements[i]] = StyleRuntimeBridge.ToComputedStyle(nativeStyles[i], elements[i]);
        }

        _styles = new StyleMap(_computedStyles);
        using var nativeStyleMap = _nativeStyleRuntime.CreateResolvedStyleMap(_nativeDom!.Native);
        using var nativeLayout = _layoutInterop.CreateNativeLayout(_nativeDom, nativeStyleMap, _viewport, ScrollOffsetFor);
        _layout = _layoutInterop.Project(nativeLayout, _nativeDom, _styles, _viewport);
        _displayList = AttachResolvedImages(_renderingInterop.Build(nativeLayout,
            _focused is null ? IntPtr.Zero : _nativeDom.GetNativeHandle(_focused)));
    }

    private float ScrollOffsetFor(ElementNode element) =>
        _scrollOffsets.TryGetValue(element, out var offset) ? offset : 0f;

    private DisplayList AttachResolvedImages(DisplayList displayList)
    {
        if (_imageResolver is null) return displayList;
        return new DisplayList(displayList.Commands.Select(command => command is DrawBackgroundImageCommand image &&
            _imageResolver.TryGetResource(image.Source, out var resource)
                ? image with { Resource = resource }
                : command).ToArray());
    }

    /// <summary>
    /// Routes a mouse-wheel notch to the nearest scrollable ancestor of whatever's under the
    /// cursor (walking up like <see cref="Dispatch"/> does for bubbling DOM events), matching how
    /// a real browser picks which scroll container a wheel event affects.
    /// </summary>
    public void MouseWheelScrolled(PointF point, float notches)
    {
        ThrowIfDisposed();
        EnsureVisualTree();
        const float pixelsPerNotch = 48f;
        var offsetDelta = -notches * pixelsPerNotch;

        for (var current = _layout!.HitTest(point); current is not null; current = current.Parent as ElementNode)
        {
            var box = _layout.FindPrimaryBox(current);
            if (box is null || box.ScrollableOverflowY <= 0)
            {
                continue;
            }

            var next = Math.Clamp(box.ScrollOffsetY + offsetDelta, 0f, box.ScrollableOverflowY);
            if (next != box.ScrollOffsetY)
            {
                _scrollOffsets[current] = next;
                InvalidateVisual();
            }

            return;
        }
    }

    private void RebuildDocument()
    {
        var focusedId = _focused?.Id;
        var hoveredId = _hovered?.Id;
        var activeId = _active?.Id;
        var markup = _component.RenderMarkup();
        var nativeDom = DomInterop.Parse(markup);
        var previousNativeDom = _nativeDom;
        _nativeDom = nativeDom;
        _document = nativeDom.Document;
        _styles = null;
        _layout = null;
        _displayList = null;

        // The previous tree's ElementNode instances are discarded on every rebuild — this engine
        // has no cross-render node identity yet. Clearing here drops the now-stale ComputedStyle
        // entries for nodes that no longer exist.
        _computedStyles.Clear();
        _scrollOffsets.Clear();

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
        _active = RestoreElement(activeId, ElementState.Active);
        _markupDirty = false;
        _visualDirty = true;
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
        _visualDirty = true;
        RenderRequested?.Invoke(this, EventArgs.Empty);
    }

    private void InvalidateVisual()
    {
        _visualDirty = true;
        RenderRequested?.Invoke(this, EventArgs.Empty);
    }

    private void OnImageResourceChanged(object? sender, ImageResourceChangedEventArgs e)
    {
        if (_hostSynchronizationContext is not null && SynchronizationContext.Current != _hostSynchronizationContext)
        {
            _hostSynchronizationContext.Post(static state => ((UiDocumentHost)state!).InvalidateVisual(), this);
            return;
        }
        InvalidateVisual();
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
            _nativeStyleRuntime.Dispose();
            _layoutInterop.Dispose();
            _renderingInterop.Dispose();
            _nativeDom?.Dispose();
            _renderer.Dispose();
        }

        GC.SuppressFinalize(this);
    }
}
