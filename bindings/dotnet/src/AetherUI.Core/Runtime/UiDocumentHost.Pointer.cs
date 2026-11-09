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
    private HashSet<ElementNode> _hoverChain = [];
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
        using var interactionEntry = EnterInteractionEntry();
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
            if (ReferenceEquals(target, _hovered) && HoverChainIsCurrent(target))
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
            _currentCursor = CursorKind.Default;
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
        if (ReferenceEquals(target, _hovered) && HoverChainIsCurrent(target)) return false;
        var request = checked(++_hoverRequestSerial);
        var pointerRequest = _pointerRequestSerial;
        var document = _document;
        var previous = _hovered;
        var previousEligible = HasCurrentHoverClaim(previous);
        var previousContinuity = previous is null ? default : InteractionContinuity.Capture(document, previous);
        var incomingContinuity = target is null ? default : InteractionContinuity.Capture(document, target);
        var sameTarget = previousEligible && ReferenceEquals(previous, target);
        var nextChain = target is null ? Array.Empty<ElementNode>() : target.AncestorsAndSelf().ToArray();
        var nextSet = new HashSet<ElementNode>(nextChain);
        _hoverChain.EnsureCapacity(_hoverChain.Count + nextChain.Length);
        var stateChanged = false;
        foreach (var element in _hoverChain.ToArray())
        {
            if (nextSet.Contains(element) && HasCurrentHoverClaim(element)) continue;
            stateChanged |= ClearHoverClaim(element);
        }
        foreach (var element in nextChain)
        {
            if (_hoverChain.Contains(element)) continue;
            var claim = AcquireInteractionClaim(element, ElementState.Hover);
            _interactionSession!.Hover.Add(element, claim);
            _hoverChain.Add(element);
            stateChanged = true;
        }
        if (stateChanged) InvalidateVisual(requestRender: false);
        // A same-document atomic move only changes the chain. Chromium's dirty hover
        // recomputation likewise does not invent a boundary event for the same target.
        if (sameTarget)
        {
            if (stateChanged && requestRender) RequestRender();
            return stateChanged;
        }
        _hovered = null;
        if (previousEligible && previous is not null && previousContinuity.Matches(this, previous))
        {
            Dispatch("mouseout", previous, point);
            if (_disposed || !ReferenceEquals(document, _document) || request != _hoverRequestSerial || pointerRequest != _pointerRequestSerial)
                return true;
            EnsureVisualTree();
            if (_disposed || !ReferenceEquals(document, _document) || request != _hoverRequestSerial || pointerRequest != _pointerRequestSerial)
                return true;
        }
        if (target is not null && (!incomingContinuity.Matches(this, target) || !IsCurrentlyVisible(target)))
        {
            _pendingPointerMove = point; _pendingPointerTarget = null;
            _hasPendingPointerTarget = false; _hasPendingPointerMove = true;
            _currentCursor = CursorFor(HitTest(point));
            return true;
        }
        _hovered = target;
        if (target is not null) Dispatch("mouseover", target, point);
        if (stateChanged && requestRender && !_disposed) RequestRender();
        return stateChanged;
    }

    private bool ClearHoverClaim(ElementNode element)
    {
        var changed = false;
        if (_interactionSession is { } session && session.Hover.TryGetValue(element, out var claim))
        {
            changed = ReleaseInteractionClaim(claim);
            session.Hover.Remove(element);
        }
        _hoverChain.Remove(element);
        return changed;
    }

    public void PointerLeft()
    {
        ThrowIfDisposed();
        ++_pointerRequestSerial;
        if (DeferInput(() => PointerLeft())) return;
        using var interactionEntry = EnterInteractionEntry();
        CountPointerBarrier();
        EnsureDomInputsCurrent();
        DrainInteractionLosses();
        if (_disposed) return;
        checked { ++_hoverRequestSerial; }
        _hasPointerPosition = false;
        _hasPendingPointerMove = false;
        _hasPendingPointerTarget = false;
        _pendingPointerTarget = null;
        _currentCursor = CursorKind.Default;
        var previous = _hovered;
        var previousEligible = HasCurrentHoverClaim(previous);
        var continuity = previous is null ? default : InteractionContinuity.Capture(_document, previous);
        _hovered = null;
        var stateChanged = false;
        foreach (var element in _hoverChain.ToArray()) stateChanged |= ClearHoverClaim(element);
        if (stateChanged) InvalidateVisual(requestRender: false);
        if (previousEligible && previous is not null && continuity.Matches(this, previous)) Dispatch("mouseout", previous, default);
        if (stateChanged && !_disposed) RequestRender();
    }

    public void PointerPressed(PointF point)
    {
        ThrowIfDisposed();
        if (DeferInput(() => PointerPressed(point))) return;
        using var interactionEntry = EnterInteractionEntry();
        var request = checked(++_pressRequestSerial);
        CountPointerBarrier();
        EnsureVisualTree();
        if (_disposed || request != _pressRequestSerial) return;
        if (TryBeginScrollbarInteraction(point)) return;
        var target = HitTest(point);
        if (target is null || target.IsDisabled) { Focus(null); return; }
        var document = _document;
        var continuity = InteractionContinuity.Capture(document, target);
        if (_activeClaim is { } old) ReleaseInteractionClaim(old);
        var claim = AcquireInteractionClaim(target, ElementState.Active);
        _activeClaim = claim; _interactionSession!.Active = claim; _active = target;
        InvalidateVisual(requestRender: false);
        EnsureVisualTree();
        if (_disposed || request != _pressRequestSerial || !ReferenceEquals(_activeClaim, claim) || !claim.IsCurrent) return;
        Focus(target.IsFocusable ? target : null);
        if (_disposed || request != _pressRequestSerial || !ReferenceEquals(_activeClaim, claim)) return;
        EnsureVisualTree();
        if (_disposed || request != _pressRequestSerial || !ReferenceEquals(_activeClaim, claim)) return;
        if (!claim.IsCurrent || !continuity.Matches(this, target) || !IsCurrentlyVisible(target) || target.IsDisabled)
        {
            ReleaseInteractionClaim(claim);
            _activeClaim = null; _interactionSession!.Active = null; _active = null;
            InvalidateVisual();
            return;
        }
        Dispatch("mousedown", target, point);
        if (!_disposed) InvalidateVisual();
    }

    public void PointerReleased(PointF point)
    {
        ThrowIfDisposed();
        if (DeferInput(() => PointerReleased(point))) return;
        using var interactionEntry = EnterInteractionEntry();
        var request = checked(++_pressRequestSerial);
        CountPointerBarrier();
        EnsureVisualTree();
        if (_disposed || request != _pressRequestSerial) return;
        if (_scrollbarPointerCaptured)
        {
            _scrollbarDrag = null;
            _scrollbarPointerCaptured = false;
            InvalidateScroll();
            return;
        }
        var target = HitTest(point);
        var pressed = _active;
        var claim = _activeClaim;
        var continuity = pressed is null ? default : InteractionContinuity.Capture(_document, pressed);
        var eligible = pressed is not null && claim is not null && claim.IsCurrent;
        if (claim is not null) ReleaseInteractionClaim(claim);
        // Retire before user code. A newer press from mouseup/click is never cleared later.
        if (ReferenceEquals(_activeClaim, claim))
        { _activeClaim = null; _interactionSession!.Active = null; _active = null; }
        if (eligible)
        {
            InvalidateVisual(requestRender: false);
            Dispatch("mouseup", pressed!, point);
            if (_disposed || request != _pressRequestSerial) return;
            EnsureVisualTree();
            if (_disposed || request != _pressRequestSerial) return;
            target = HitTest(point);
        }
        if (eligible && continuity.Matches(this, pressed!) && ReferenceEquals(pressed, target) && !pressed!.IsDisabled)
        {
            var click = Dispatch("click", pressed, point);
            if (_disposed || request != _pressRequestSerial) return;
            EnsureVisualTree();
            if (_disposed || request != _pressRequestSerial) return;
            if (!click.IsDefaultPrevented && continuity.Matches(this, pressed) && IsCurrentlyVisible(pressed) && !pressed.IsDisabled &&
                pressed.TagName == "button" && !string.Equals(pressed.GetAttribute("type"), "button", StringComparison.OrdinalIgnoreCase))
                SubmitClosestForm(pressed);
        }
        if (!_disposed) InvalidateVisual();
    }

    public CursorKind CursorAt(PointF point)
    {
        ThrowIfDisposed();
        using var interactionEntry = EnterInteractionEntry();
        if (_options.EnableInputWorkCounters) Interlocked.Increment(ref _cursorQueries);
        if (_styleOperationDepth != 0 || !HasCertifiedStyleSnapshot()) return CursorKind.Default;
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
        var registry = _componentRegistry;
        var dispatchDocument = _document;
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
            if (token is { Length: > 0 } && registry is not null && registry.TryGetEventHandler(token, out var handler))
            {
                handler(uiEvent);
            }

            if (_disposed || !ReferenceEquals(dispatchDocument, _document) || uiEvent.IsPropagationStopped)
            {
                break;
            }
        }

        return uiEvent;
    }

    private void Focus(ElementNode? target)
    {
        if (_disposed) return;
        using var interactionEntry = EnterInteractionEntry();
        var beforeDrain = _focusRequestSerial;
        EnsureDomInputsCurrent();
        DrainInteractionLosses();
        if (_disposed || beforeDrain != _focusRequestSerial) return;
        if (!HasCertifiedStyleSnapshot(requireDerived: false) ||
            (_invalidation & (VisualInvalidation.Style | VisualInvalidation.Layout)) != 0) EnsureVisualTree();
        if (_disposed || beforeDrain != _focusRequestSerial) return;
        var request = checked(++_focusRequestSerial);
        if (ReferenceEquals(target, _focused) && (target is null || _focusClaim?.IsCurrent == true)) return;
        var document = _document;
        var incomingContinuity = target is null ? default : InteractionContinuity.Capture(document, target);
        var previous = _focused;
        var previousClaim = _focusClaim;
        var deliverBlur = previousClaim?.IsCurrent == true;
        if (previousClaim is not null) ReleaseInteractionClaim(previousClaim);
        _focused = null; _focusClaim = null;
        if (_interactionSession is { } session) session.Focus = null;
        if (previous is not null)
        {
            InvalidateVisual(requestRender: false);
            if (deliverBlur) Dispatch("blur", previous, default);
            if (_disposed || !ReferenceEquals(document, _document) || request != _focusRequestSerial) return;
            EnsureVisualTree();
            if (_disposed || !ReferenceEquals(document, _document) || request != _focusRequestSerial) return;
        }
        if (target is not null && incomingContinuity.Matches(this, target) && IsEligibleForFocus(target))
        {
            var claim = AcquireInteractionClaim(target, ElementState.Focus);
            _focused = target; _focusClaim = claim; _interactionSession!.Focus = claim;
            InvalidateVisual(requestRender: false);
            Dispatch("focus", target, default);
        }
        if (!_disposed) InvalidateVisual();
    }

    private bool IsCurrentlyVisible(ElementNode element) =>
        ReferenceEquals(element.OwnerDocumentCore, _document) &&
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

        if (element.BindingCore is NativeDomBinding)
        {
            SetKnownElementState(element, next);
            return true;
        }

        _stateDirtyElements.EnsureCapacity(_stateDirtyElements.Count + 1);
        _stateInputsToSync.EnsureCapacity(_stateInputsToSync.Count + 1);
        SetKnownElementState(element, next);
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
        var request = checked(++_hoverRequestSerial);
        var previous = _hovered;
        var previousEligible = HasCurrentHoverClaim(previous);
        var continuity = previous is null ? default : InteractionContinuity.Capture(_document, previous);
        _hovered = null;
        foreach (var element in _hoverChain.ToArray()) ClearHoverClaim(element);
        InvalidateVisual(requestRender: false);
        if (previousEligible && previous is not null && continuity.Matches(this, previous)) Dispatch("mouseout", previous, _lastPointerPosition);
        if (_disposed || request != _hoverRequestSerial) return;
        EnsureVisualTree();
        if (_disposed || request != _hoverRequestSerial) return;
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
