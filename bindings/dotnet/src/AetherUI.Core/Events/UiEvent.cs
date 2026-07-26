using AetherUI.Dom;
using AetherUI.Primitives;

namespace AetherUI.Events;

public enum UiKey : ushort
{
    None,
    Backspace,
    Tab,
    Enter,
    Escape,
    Space,
    Left,
    Up,
    Right,
    Down,
    Home,
    End,
    Delete,
}

public sealed class UiEvent
{
    internal UiEvent(string type, ElementNode target)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(type);
        ArgumentNullException.ThrowIfNull(target);
        Type = type;
        Target = target;
        CurrentTarget = target;
    }

    public string Type { get; }
    public ElementNode Target { get; }
    public ElementNode CurrentTarget { get; internal set; }
    public PointF PointerPosition { get; internal set; }
    public UiKey Key { get; internal set; }
    public char Character { get; internal set; }
    public string? Value { get; internal set; }
    public bool IsDefaultPrevented { get; private set; }
    public bool IsPropagationStopped { get; private set; }

    public void PreventDefault() => IsDefaultPrevented = true;
    public void StopPropagation() => IsPropagationStopped = true;
}

public delegate void UiEventHandler(UiEvent e);
