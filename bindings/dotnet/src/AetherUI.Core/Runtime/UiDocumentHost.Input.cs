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
    private readonly Dictionary<string, string> _uncontrolledInputValues = new(StringComparer.Ordinal);

    private bool _runtimeValuesDirty;

    public void TextInput(char character)
    {
        ThrowIfDisposed();
        if (DeferInput(() => TextInput(character))) return;
        EnsureVisualTree();
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
        if (DeferInput(() => KeyPressed(key))) return;
        EnsureVisualTree();
        var target = _focused;
        if (target is not null)
        {
            var keyEvent = Dispatch("keydown", target, default, key);
            if (keyEvent.IsDefaultPrevented) return;
            // A handler may hide, disable or replace the focused node before
            // the key's default editing/activation action runs.
            EnsureVisualTree();
        }

        if (key == UiKey.Tab)
        {
            FocusNext();
            return;
        }

        if (target is null || !ReferenceEquals(target, _focused) || !IsEligibleForFocus(target))
            return;

        if (target.TagName != "input" || target.IsDisabled)
        {
            if (key is UiKey.Enter or UiKey.Space && target.TagName == "button")
            {
                var click = Dispatch("click", target, default, key);
                EnsureVisualTree();
                if (!click.IsDefaultPrevented && ReferenceEquals(target, _focused) && IsEligibleForFocus(target) &&
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
            Dispatch("input", input, default, key);
            InvalidateLayout();
        }
        else if (key == UiKey.Enter)
        {
            SubmitClosestForm(input);
        }
    }

    private void SetInputValue(ElementNode input, string value)
    {
        NoteInputMutation();
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
