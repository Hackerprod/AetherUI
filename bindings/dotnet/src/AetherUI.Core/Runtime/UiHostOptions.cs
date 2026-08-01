using AetherUI.Primitives;

namespace AetherUI.Runtime;

/// <summary>
/// Physical outer-window bounds supplied by a desktop host before its native window is created.
/// Width and height include the platform window frame rather than only the client area.
/// </summary>
public readonly record struct WindowBounds(int X, int Y, int Width, int Height)
{
    public bool IsUsable => Width > 0 && Height > 0;
}

public sealed class UiHostOptions
{
    public string Title { get; init; } = "AetherUI";
    public int Width { get; init; } = 960;
    public int Height { get; init; } = 640;
    public int MinimumWidth { get; init; } = 420;
    public int MinimumHeight { get; init; } = 260;
    /// <summary>
    /// Optional physical outer-window placement to use at creation time. Hosts that do not supply
    /// it retain the platform-default placement and client-size behavior.
    /// </summary>
    public WindowBounds? InitialWindowBounds { get; init; }
    public ColorRgba ClearColor { get; init; } = ColorRgba.White;
    public IImageResourceResolver? ImageResolver { get; init; }
    public Action<UiDocumentHostMetrics>? MetricsSink { get; init; }
}
