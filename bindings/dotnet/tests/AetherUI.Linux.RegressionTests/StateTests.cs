using AetherUI.Linux;
using AetherUI.Linux.Graphics;
using AetherUI.Primitives;
using AetherUI.Rendering;
internal static class StateTests
{
    internal static void Run()
    {
        var memory = new MemoryFramePresenter();
        using var renderer = new CairoRenderBackend(new(memory), new(40, 40, 1));
        void Check(bool ok, string why)
        {
            if (!ok)
                throw new InvalidOperationException("State: " + why);
        }
        byte P(int x, int y) => memory.CurrentFrame!.Pixels.Span[(y * 40 + x) * 4 + 3];
        var scene = new DisplayList(new PaintCommand[] { new PushScrollOffsetCommand(9), new FillRoundedRectangleCommand(new(20, 20, 5, 5), default, ColorRgba.White), new PopScrollOffsetCommand() });
        renderer.SetScrollOffsetProvider(_ => new(10, 12));
        renderer.Render(scene, default);
        Check(P(11, 9) == 255 && P(21, 21) == 0, "Two-axis offset applies negative translation");
        renderer.SetScrollOffsetProvider(null);
        renderer.Render(scene, default);
        Check(P(21, 21) == 255 && P(11, 9) == 0, "Provider reset clears retained offset");
        renderer.SetScrollVisualStateProvider(_ => new(new(5, 5), default));
        renderer.Render(scene, default);
        Check(P(16, 16) == 255, "Visual state offset");
        renderer.SetScrollOffsetProvider(_ => new(10, 12));
        renderer.Render(scene, default);
        Check(P(11, 9) == 255 && P(16, 16) == 0, "Offset provider replaces visual state provider");
        renderer.SetScrollVisualStateProvider(_ => new(default, new(true, true, new(0, 35, 35, 5), new(10, 35, 10, 5), new(35, 0, 5, 35), new(35, 10, 5, 10), new(35, 35, 5, 5))));
        renderer.Render(new(new PaintCommand[] { new DrawScrollbarChromeCommand(9) }), default);
        Check(P(2, 37) > 0 && P(15, 37) > 0 && P(37, 2) > 0 && P(37, 15) > 0 && P(37, 37) > 0, "Chrome tracks/thumbs/corner");
        // A clip is captured under the matrix at push and survives a temporary nested transform.
        renderer.Render(new(new PaintCommand[] { new PushClipCommand(new(0, 0, 10, 10), default), new PushTransformCommand(new(1, 0, 0, 1, 8, 0)), new FillRoundedRectangleCommand(new(0, 0, 10, 10), default, ColorRgba.White), new PopTransformCommand(), new PopClipCommand() }), default);
        Check(P(9, 5) > 0 && P(12, 5) == 0, "Clip remains fixed in device space across transform");
        var old = memory.CurrentFrame;
        try
        {
            renderer.Resize(8192, 8192, 1);
            throw new InvalidOperationException("Budget accepted");
        }
        catch (ArgumentOutOfRangeException) { }
        renderer.Render(new(Array.Empty<PaintCommand>()), ColorRgba.White);
        Check(memory.CurrentFrame!.PixelWidth == 40 && memory.CurrentFrame.PixelHeight == 40, "Failed resize retains dimensions");
        old = memory.CurrentFrame;
        try
        {
            memory.Present(2, 2, 8, new byte[1]);
            throw new InvalidOperationException("Invalid frame accepted");
        }
        catch (ArgumentException) { }
        Check(ReferenceEquals(old, memory.CurrentFrame), "Memory presenter fails before atomic commit");
        Console.WriteLine("PASS state: scroll providers/reset, chrome, clip capture, resize rollback, atomic memory presenter");
    }
}
