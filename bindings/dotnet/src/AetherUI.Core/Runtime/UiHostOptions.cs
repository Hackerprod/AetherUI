using AetherUI.Primitives;

namespace AetherUI.Runtime;

public sealed class UiHostOptions
{
    public string Title { get; init; } = "AetherUI";
    public int Width { get; init; } = 960;
    public int Height { get; init; } = 640;
    public int MinimumWidth { get; init; } = 420;
    public int MinimumHeight { get; init; } = 260;
    public ColorRgba ClearColor { get; init; } = ColorRgba.White;
    public IImageResourceResolver? ImageResolver { get; init; }
}
