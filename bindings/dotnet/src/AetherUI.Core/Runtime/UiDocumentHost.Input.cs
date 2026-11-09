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
/// Document host region: Input element value synchronization.
/// </summary>
public sealed partial class UiDocumentHost
{
    private Dictionary<string, string> _uncontrolledInputValues = new(StringComparer.Ordinal);

    private bool _runtimeValuesDirty;

    public void TextInput(char character)
    {
        ThrowIfDisposed();
        if (DeferInput(() => TextInput(character))) return;
        using var interactionEntry = EnterInteractionEntry();
        EnsureVisualTree();
        if (_focused is not { TagName: "input" } input || _focusClaim?.IsCurrent != true || input.IsDisabled || char.IsControl(character))
        {
            return;
        }

        var claim = _focusClaim;
        var continuity = InteractionContinuity.Capture(_document, input);
        var current = input.RuntimeValue ?? input.GetAttribute("value") ?? string.Empty;
        var maxLength = ParsePositiveInt(input.GetAttribute("maxlength"));
        if (maxLength is not null && current.Length >= maxLength.Value)
        {
            return;
        }

        SetInputValue(input, current + character);
        if (_disposed || !ReferenceEquals(claim, _focusClaim) || !claim!.IsCurrent || !continuity.Matches(this, input)) return;
        var uiEvent = Dispatch("input", input, default, character: character);
        if (uiEvent.IsDefaultPrevented && !_disposed && ReferenceEquals(_focusClaim, claim) &&
            claim!.IsCurrent && continuity.Matches(this, input))
        {
            SetInputValue(input, current);
        }

        if (!_disposed) InvalidateLayout();
    }

    public void KeyPressed(UiKey key)
    {
        ThrowIfDisposed();
        if (DeferInput(() => KeyPressed(key))) return;
        using var interactionEntry = EnterInteractionEntry();
        EnsureVisualTree();
        var target = _focused;
        var claim = _focusClaim;
        if (target is not null && claim?.IsCurrent == true)
        {
            var keyEvent = Dispatch("keydown", target, default, key);
            if (keyEvent.IsDefaultPrevented || _disposed) return;
            // A handler may hide, disable or replace the focused node before
            // the key's default editing/activation action runs.
            EnsureVisualTree();
        }

        if (key == UiKey.Tab)
        {
            FocusNext();
            return;
        }

        if (_disposed || target is null || !ReferenceEquals(target, _focused) || !ReferenceEquals(claim, _focusClaim) ||
            claim?.IsCurrent != true || !IsEligibleForFocus(target))
            return;

        if (target.TagName != "input" || target.IsDisabled)
        {
            if (key is UiKey.Enter or UiKey.Space && target.TagName == "button")
            {
                var click = Dispatch("click", target, default, key);
                if (_disposed) return;
                EnsureVisualTree();
                if (!click.IsDefaultPrevented && ReferenceEquals(claim, _focusClaim) && claim.IsCurrent && ReferenceEquals(target, _focused) && IsEligibleForFocus(target) &&
                    !string.Equals(target.GetAttribute("type"), "button", StringComparison.OrdinalIgnoreCase))
                {
                    SubmitClosestForm(target);
                }
            }

            return;
        }

        var input = target;
        var current = input.RuntimeValue ?? input.GetAttribute("value") ?? string.Empty;
        if (key is UiKey.Backspace or UiKey.Delete)
        {
            if (current.Length == 0)
            {
                return;
            }

            SetInputValue(input, current[..^1]);
            if (_disposed || !ReferenceEquals(claim, _focusClaim) || !claim.IsCurrent) return;
            Dispatch("input", input, default, key);
            if (!_disposed) InvalidateLayout();
        }
        else if (key == UiKey.Enter)
        {
            SubmitClosestForm(input);
        }
    }

    private void SetInputValue(ElementNode input, string value)
    {
        var synchronized = input.BindingCore is NativeDomBinding;
        if (!synchronized) NoteInputMutation();
        SetKnownRuntimeValue(input, value);
        _runtimeValuesDirty = true;
        if (!synchronized && input.Id is { Length: > 0 } id)
        {
            _uncontrolledInputValues[id] = value;
        }

        if (input.IsRequired && !string.IsNullOrWhiteSpace(value))
        {
            if (SetElementState(input, ElementState.Invalid, enabled: false))
                InvalidateVisual(requestRender: false);
        }
    }

    private static int? ParsePositiveInt(string? value) =>
        int.TryParse(value, out var result) && result >= 0 ? result : null;

    private void SyncNativeRuntimeValues()
    {
        if (_runtimeValuesDirty && _nativeDom is not null &&
            (_invalidation & (VisualInvalidation.Style | VisualInvalidation.Layout | VisualInvalidation.DisplayList)) != 0)
        {
            _nativeDom.SyncRuntimeValues();
            _runtimeValuesDirty = false;
        }
    }

}
