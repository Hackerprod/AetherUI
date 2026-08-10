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
/// Document host region: Pointer routing, hover chain, focus and DOM event dispatch.
/// </summary>
public sealed partial class UiDocumentHost
{
    private ElementNode? _hovered;
    private readonly HashSet<ElementNode> _hoverChain = [];
    private ElementNode? _active;
    private ElementNode? _focused;

    private PointF _pendingPointerMove;
    private ElementNode? _pendingPointerTarget;

    private bool _hasPendingPointerMove;
    private bool _hasPendingPointerTarget;
    private bool _pointerRenderPending;

    private CursorKind _currentCursor = CursorKind.Default;

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

    private ElementNode? HitTest(PointF point)
    {
        if (_nativeLayout is not null && _nativeDom is not null)
        {
            var nativeHit = _layoutInterop.HitTest(_nativeLayout, _nativeVisualStyleMap, _nativeDom, point, ScrollOffsetFor);
            if (nativeHit is not null) return nativeHit;
        }

        return _layout?.HitTest(point, ScrollOffsetFor);
    }

    private void RequestPointerRender()
    {
        if (_pointerRenderPending)
        {
            return;
        }

        _pointerRenderPending = true;
        RequestRender();
    }

}
