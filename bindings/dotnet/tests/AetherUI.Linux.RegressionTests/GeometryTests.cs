using AetherUI.Css;
using AetherUI.Linux;
using AetherUI.Linux.Graphics;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;
internal static class GeometryTests
{
    internal static void Run()
    {
        var memory = new MemoryFramePresenter();
        using var renderer = new CairoRenderBackend(new(memory), new(80, 80, 1));
        void Render(params PaintCommand[] commands) => renderer.Render(new(commands), default);
        byte P(int x, int y, int channel = 3) => memory.CurrentFrame!.Pixels.Span[(y * 80 + x) * 4 + channel];
        void Check(bool ok, string why)
        {
            if (!ok)
                throw new InvalidOperationException("Geometry: " + why);
        }
        var rect = new RectF(10, 10, 60, 60);
        var inner = new RectF(19, 19, 42, 42);
        for (int side = 0; side < 4; side++)
        {
            (int x, int y) Point(int distance) => side switch
            {
                0 => (40, 10 + distance),
                1 => (69 - distance, 40),
                2 => (40, 69 - distance),
                _ => (10 + distance, 40)
            };
            foreach (var style in new[] { BorderStyle.Solid, BorderStyle.Double, BorderStyle.Groove, BorderStyle.Ridge, BorderStyle.Inset, BorderStyle.Outset, BorderStyle.Dotted, BorderStyle.Dashed })
            {
                Render(new FillBorderSideCommand(rect, default, inner, default, side, (int)style, new(.5f, .5f, .5f)));
                var a = Point(1);
                var b = Point(4);
                var c = Point(7);
                if (style == BorderStyle.Double)
                    Check(P(a.x, a.y) > 240 && P(b.x, b.y) == 0 && P(c.x, c.y) > 240, $"Double border bands side {side}");
                else if (style is BorderStyle.Groove or BorderStyle.Ridge)
                    Check(P(a.x, a.y) > 240 && P(c.x, c.y) > 240 && P(a.x, a.y, 0) != P(c.x, c.y, 0), $"Shaded border bands side {side}");
                else
                    Check(memory.CurrentFrame!.Pixels.Span.ToArray().Any(x => x != 0), $"Border fixture {side} {style}");
                Check(P(40, 40) == 0, "Border must not fill content");
            }
        }
        Render(new FillRoundedRectangleCommand(rect, new(20, 6, 3, 18, 15, 9, 8, 20), ColorRgba.White));
        Check(P(10, 10) == 0 && P(30, 10) > 100 && P(40, 40) == 255, "Elliptical corners");
        Render(new PushTransformCommand(new(0, 1, -1, 0, 80, 0)), new PushClipCommand(new(10, 20, 20, 30), default), new FillRoundedRectangleCommand(new(0, 0, 80, 80), default, ColorRgba.White), new PopClipCommand(), new PopTransformCommand());
        Check(P(40, 20) == 255 && P(10, 20) == 0, "Clip uses pushed transform");
        Render(new PushTransformCommand(new(0, 0, 0, 1, 0, 0)), new FillRoundedRectangleCommand(rect, default, ColorRgba.White), new PopTransformCommand(), new FillRoundedRectangleCommand(new(0, 0, 2, 2), default, ColorRgba.White));
        Check(P(40, 40) == 0 && P(0, 0) == 255, "Singular transforms suppress only subtree");
        var g = new LinearGradient(1, 0, new[] { new GradientStop(new(1, 0, 0), 0), new GradientStop(new(0, 0, 1), 1) });
        Render(new FillRoundedRectangleGradientCommand(new(10, 10, 20, 20), default, new(10, 10, 60, 20), g, 1), new FillRoundedRectangleGradientCommand(new(30, 10, 40, 20), default, new(10, 10, 60, 20), g, 1));
        Check(Math.Abs(P(29, 20, 2) - P(30, 20, 2)) < 10 && P(15, 20, 2) > P(65, 20, 2), "Gradient phase continuous across clipped tiles");
        var resource = new ImageResourceResult(new(2, 1), new byte[] { 0, 0, 255, 255, 255, 0, 0, 255 });
        DrawBackgroundImageCommand Image(RectF source, RectF dest, BackgroundRepeat repeat) => new("memory", new(0, 0, 80, 80), default, default, BackgroundImageSizeKind.Auto, default, default, repeat, repeat, 1, source, dest, true, resource);
        // The core resolves repeat/space/round to tiles; playback maps every explicit source/destination unchanged.
        foreach (var repeat in new[] { BackgroundRepeat.Repeat, BackgroundRepeat.Space, BackgroundRepeat.Round })
        {
            Render(Image(new(0, 0, 2, 1), new(10, 10, 20, 10), repeat), Image(new(1, 0, 1, 1), new(40, 10, 10, 10), repeat));
            Check(P(15, 15, 2) > 150 && P(45, 15, 0) > 150 && P(35, 15) == 0, $"Resolved tile {repeat}");
        }
        var old = memory.CurrentFrame;
        try
        {
            Render(Image(new(0, 0, 2, 1), rect, BackgroundRepeat.NoRepeat) with
            {
                Resource = new(new(2, 1), new byte[1])
            });
            throw new InvalidOperationException("Bad image accepted");
        }
        catch (ArgumentException) { }
        Check(ReferenceEquals(old, memory.CurrentFrame), "Malformed image must retain frame");
        Render(new StrokeRoundedRectangleCommand(new(20, 20, 40, 40), CornerRadii.Uniform(8), 4, ColorRgba.White));
        Check(P(40, 18) > 240 && P(40, 21) > 240 && P(40, 17) == 0 && P(40, 22) == 0 && P(40, 40) == 0 && P(19, 19) == 0, $"Isolated rounded stroke width and hollow interior: {P(40, 18)},{P(40, 21)},{P(40, 17)},{P(40, 22)},{P(40, 40)},{P(19, 19)}");
        renderer.SetScrollOffsetProvider(_ => new(3, 2));
        Render(new PushTransformCommand(new(1, 0, 0, 1, 5, 5)),
            new PushClipCommand(new(0, 0, 50, 50), CornerRadii.Uniform(4)), new PushScrollOffsetCommand(1),
            new DrawBoxShadowCommand(new(10, 10, 20, 20), default, new(3, 0, 2, 0, ColorRgba.White, false)),
            Image(new(0, 0, 1, 1), new(10, 10, 20, 20), BackgroundRepeat.NoRepeat) with { Resource = new(new(1, 1), new byte[] { 255, 255, 255, 255 }) },
            new ApplyBackdropFilterCommand(new(10, 10, 20, 20), default, new[] { new BackdropFilter(BackdropFilterKind.Brightness, CssLength.Zero, .5f) }),
            new FillRoundedRectangleCommand(new(60, 0, 10, 10), default, ColorRgba.White),
            new PopScrollOffsetCommand(), new PopClipCommand(), new PopTransformCommand());
        Check(P(20, 20) == 255 && Math.Abs(P(20, 20, 0) - 128) <= 1, $"Combined image and backdrop under transform/scroll: A={P(20, 20)} B={P(20, 20, 0)}");
        Check(P(34, 23) > 0 && P(5, 5) == 0 && P(65, 8) == 0, "Combined shadow halo and captured clip");
        if (Environment.GetEnvironmentVariable("AETHER_CAPTURE_PATH") is { Length: > 0 } path)
        {
            using var output = File.Create(path); output.Write(System.Text.Encoding.ASCII.GetBytes("P6\n80 80\n255\n")); var pixels = memory.CurrentFrame!.Pixels.Span;
            for (int k = 0; k < pixels.Length; k += 4) { output.WriteByte(pixels[k + 2]); output.WriteByte(pixels[k + 1]); output.WriteByte(pixels[k]); }
            Console.WriteLine("Combined fixture capture (80x80): " + path);
        }
        renderer.SetScrollOffsetProvider(null);
        Console.WriteLine("PASS geometry: elliptical corners, transforms/clips, all border sides/styles, gradient phase, resolved image tiles");
    }
}
