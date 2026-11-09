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
    private PointF _lastPointerPosition;
    private bool _hasPointerPosition;
    private ElementNode? _pendingPointerTarget;

    private bool _hasPendingPointerMove;
    private bool _hasPendingPointerTarget;
    private bool _pointerRenderPending;
    private long _pointerRequestSerial;
    private long _pointerAcknowledgedSerial;

    private CursorKind _currentCursor = CursorKind.Default;

    public CursorKind PointerMoved(PointF point)
    {
        ThrowIfDisposed();
        ++_pointerRequestSerial;
        if (DeferInput(() => PointerMoved(point))) return CursorKind.Default;
        if (_options.EnableInputWorkCounters) Interlocked.Increment(ref _pointerMovesReceived);
        _lastPointerPosition = point;
        _hasPointerPosition = true;
        if (_scrollbarPointerCaptured)
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
                if (_options.EnableInputWorkCounters)
                {
                    Interlocked.Increment(ref _sameTargetMoves);
                    if (_hasPendingPointerMove) Interlocked.Increment(ref _pendingMovesCoalesced);
                }
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

        if (_options.EnableInputWorkCounters && _hasPendingPointerMove)
            Interlocked.Increment(ref _pendingMovesCoalesced);
        _pendingPointerMove = point;
        _hasPendingPointerMove = true;
        RequestPointerRender();
        return _currentCursor;
    }

    private bool CanHitTestCurrentVisualTree() =>
        HasCertifiedStyleSnapshot() &&
        !_markupDirty &&
        (_invalidation & (VisualInvalidation.Style | VisualInvalidation.Layout)) == 0 &&
        _layout is not null &&
        _styles is not null;

    private bool ApplyPendingPointerMove()
    {
        if (!_hasPendingPointerMove)
        {
            return false;
        }

        if (_options.EnableInputWorkCounters) Interlocked.Increment(ref _pendingMovesProcessed);
        var pointerSerial = _pointerRequestSerial;
        var point = _pendingPointerMove;
        var target = _hasPendingPointerTarget ? _pendingPointerTarget : HitTest(point);
        _hasPendingPointerMove = false;
        _hasPendingPointerTarget = false;
        _pendingPointerTarget = null;
        _pointerAcknowledgedSerial = pointerSerial;
        _currentCursor = CursorFor(target);
        return UpdateHoverTarget(target, point, requestRender: false);
    }

    private bool UpdateHoverTarget(ElementNode? target, PointF point, bool requestRender)
    {
        if (ReferenceEquals(target, _hovered) && (target is not null || _hoverChain.Count == 0))
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

        if (stateChanged) InvalidateVisual(requestRender: false);
        if (previous is not null)
        {
            // The outgoing handler can hide/replace the incoming target. Resolve
            // its effects before delivering mouseover to that candidate.
            _hovered = null;
            Dispatch("mouseout", previous, point);
            EnsureVisualTree();
            // Only reject a stale/hidden candidate here. Do not broaden the
            // visibility fix into resettling pointer-events hover semantics.
            if (target is not null && !IsCurrentlyVisible(target))
            {
                _pendingPointerMove = point;
                _pendingPointerTarget = null;
                _hasPendingPointerTarget = false;
                _hasPendingPointerMove = true;
                _currentCursor = CursorFor(HitTest(point));
                return true;
            }
        }

        _hovered = target;
        if (_hovered is not null)
        {
            Dispatch("mouseover", _hovered, point);
        }

        // State changes were invalidated before callbacks; do not enqueue a
        // redundant style pass after the outgoing handler's revalidation.
        if (stateChanged && requestRender) RequestRender();
        return stateChanged;
    }

    public void PointerLeft()
    {
        ThrowIfDisposed();
        ++_pointerRequestSerial;
        if (DeferInput(() => PointerLeft())) return;
        CountPointerBarrier();
        _hasPointerPosition = false;
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
        if (DeferInput(() => PointerPressed(point))) return;
        CountPointerBarrier();
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
        // :active can hide this candidate even on the first press, before any
        // previous focus exists to trigger blur-time style revalidation.
        InvalidateVisual(requestRender: false);
        EnsureVisualTree();
        Focus(target.IsFocusable ? target : null);
        EnsureVisualTree();
        if (!IsCurrentlyVisible(target) || target.IsDisabled)
        {
            if (ReferenceEquals(_active, target))
            {
                SetElementState(target, ElementState.Active, enabled: false);
                _active = null;
                InvalidateVisual();
            }
            return;
        }
        Dispatch("mousedown", target, point);
        InvalidateVisual();
    }

    public void PointerReleased(PointF point)
    {
        ThrowIfDisposed();
        if (DeferInput(() => PointerReleased(point))) return;
        CountPointerBarrier();
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
            InvalidateVisual(requestRender: false);
            Dispatch("mouseup", pressed, point);
            EnsureVisualTree();
            target = HitTest(point);
        }

        _active = null;
        if (pressed is not null && ReferenceEquals(pressed, target) && !pressed.IsDisabled)
        {
            var click = Dispatch("click", pressed, point);
            EnsureVisualTree();
            if (!click.IsDefaultPrevented && IsCurrentlyVisible(pressed) && !pressed.IsDisabled && pressed.TagName == "button" &&
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
        if (_options.EnableInputWorkCounters) Interlocked.Increment(ref _cursorQueries);
        if (_styleOperationDepth != 0) return CursorKind.Default;
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

    private CursorKind CursorFor(ElementNode? target)
    {
        if (_options.EnableInputWorkCounters) Interlocked.Increment(ref _cursorResolutions);
        return target is not null && _styles is not null && _styles.TryGetValue(target, out var style)
            ? style.Cursor
            : CursorKind.Default;
    }

    // Observation only: the baseline does not flush pending hover at these boundaries.
    private void CountPointerBarrier()
    {
        if (!_options.EnableInputWorkCounters) return;
        Interlocked.Increment(ref _pointerBarriers);
        if (_hasPendingPointerMove) Interlocked.Increment(ref _pointerBarriersWithPendingMove);
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
        // Clear the old identity before callbacks can reenter. Do not install the
        // new target until blur has resolved its possible DOM/style changes.
        _focused = null;
        if (previous is not null)
        {
            SetElementState(previous, ElementState.Focus, enabled: false);
            InvalidateVisual(requestRender: false);
            Dispatch("blur", previous, default);
            EnsureVisualTree();
            if (_focused is not null) return; // A reentrant handler chose focus.
        }

        if (target is not null && IsEligibleForFocus(target))
        {
            _focused = target;
            SetElementState(target, ElementState.Focus, enabled: true);
            InvalidateVisual(requestRender: false);
            Dispatch("focus", target, default);
        }

        InvalidateVisual();
    }

    private bool IsCurrentlyVisible(ElementNode element) =>
        _styles is not null && _styles.TryGetValue(element, out var style) &&
        style.Visible && style.Display != DisplayKind.None;

    private bool IsEligibleForFocus(ElementNode element) =>
        element.IsFocusable && !element.IsDisabled && IsCurrentlyVisible(element) &&
        _layout?.FindPrimaryBox(element) is { } box && !box.BorderBox.IsEmpty;

    private void FocusNext()
    {
        EnsureVisualTree();
        var focusable = _document!.DescendantElements()
            .Where(IsEligibleForFocus)
            .ToArray();
        if (focusable.Length == 0)
        {
            Focus(null);
            return;
        }

        var index = Array.IndexOf(focusable, _focused);
        Focus(focusable[(index + 1 + focusable.Length) % focusable.Length]);
        EnsureVisualTree();
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
        NoteInputMutation();
        _stateDirtyElements.Add(element);
        _stateInputsToSync.Add(element);
        return true;
    }

    private ElementNode? HitTest(PointF point)
    {
        if (_styleOperationDepth != 0 || !HasCertifiedStyleSnapshot()) return null;
        if (_options.EnableInputWorkCounters) Interlocked.Increment(ref _hitQueries);
        if (_nativeLayout is not null && _nativeDom is not null)
        {
            // A native miss is authoritative, including a rejection by an ancestor clip.
            return _layoutInterop.HitTest(_nativeLayout, _nativeVisualStyleMap, _nativeDom, point, ScrollOffsetFor);
        }

        return _layout?.HitTest(point, ScrollOffsetFor);
    }

    private void ResetUnstableHover()
    {
        var pointerSerial = _pointerRequestSerial;
        var previous = _hovered;
        _hovered = null;
        foreach (var element in _hoverChain)
            SetElementState(element, ElementState.Hover, enabled: false);
        _hoverChain.Clear();
        InvalidateVisual(requestRender: false);
        if (previous is not null) Dispatch("mouseout", previous, _lastPointerPosition);
        EnsureVisualTree();
        // The final resolve may have queued a visibility-driven move. Deliberately
        // discard it only if no new input arrived through the callbacks.
        if (_pointerRequestSerial == pointerSerial)
        {
            _hasPendingPointerMove = false;
            _hasPendingPointerTarget = false;
            _pendingPointerTarget = null;
            _pointerAcknowledgedSerial = pointerSerial;
        }
        _currentCursor = CursorFor(HitTest(_lastPointerPosition));
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
