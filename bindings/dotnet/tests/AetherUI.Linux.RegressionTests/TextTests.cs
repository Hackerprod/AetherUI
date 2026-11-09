using AetherUI.Css;
using AetherUI.Linux;
using AetherUI.Linux.Graphics;
using AetherUI.Primitives;
using AetherUI.Rendering;

internal static class TextTests
{
    private const int Width = 320, Height = 180;
    private const string Family = "DejaVu Sans";
    private static void Assert(bool value, string message)
    {
        if (!value)
            throw new InvalidOperationException("Text regression: " + message);
    }

    public static void Run()
    {
        var presenter = new MemoryFramePresenter();
        using var backend = new CairoRenderBackend(new(presenter), new(Width, Height, 1));
        var style = new ComputedStyle { FontFamily = Family, FontSize = 24 };
        var empty = backend.MeasureText("", style);
        Assert(empty.Width == 0 && empty.Height == style.FontSize * style.LineHeight, "empty semantics");
        foreach (var text in new[] { "Español", "e\u0301", "العربية", "漢字", "😀", "office ffi" })
        {
            var metrics = backend.MeasureText(text, style);
            Assert(float.IsFinite(metrics.Width) && metrics.Width > 0 && metrics.Baseline > 0 && metrics.Baseline <= metrics.Height, "multilingual metrics " + text);
            var pixels = Render(new DrawTextCommand(text, new(10, 10, 300, 90), Family, 24, FontWeight.Normal, TextAlign.Start, ColorRgba.White));
            Assert(Bounds(pixels).Right >= Bounds(pixels).Left, "multilingual raster " + text);
        }
        var initial = backend.MeasureText("Baseline", style);
        Assert(backend.MeasureText("Hola   ", style).Width > backend.MeasureText("Hola", style).Width, "trailing whitespace");
        Assert(backend.MeasureText("Baseline", style, 1).Width == initial.Width, "intrinsic measurement stays unwrapped");
        for (var i = 0; i < 400; i++)
            backend.MeasureText("Cache entry " + i, style);
        Assert(backend.MeasureText("Baseline", style) == initial, "cache eviction changes metrics");
        backend.Resize(Width, Height, 2);
        Assert(backend.MeasureText("Baseline", style) == initial, "DPI changed DIP metrics");
        backend.Resize(Width, Height, 1);

        var rect = new RectF(10, 10, 260, 60);
        DrawTextCommand Label(TextAlign align, string text = "Test") => new(text, rect, Family, 24, FontWeight.Normal, align, ColorRgba.White);
        var left = Bounds(Render(Label(TextAlign.Start)));
        var center = Bounds(Render(Label(TextAlign.Center)));
        var right = Bounds(Render(Label(TextAlign.End)));
        Assert(left.Left < center.Left && center.Left < right.Left, "alignment must move glyphs");
        Assert(Math.Abs((center.Left - left.Left) - (right.Left - center.Left)) <= 2, "center alignment midpoint");
        var wrapped = Bounds(Render(new DrawTextCommand("Words that wrap onto multiple lines", new(10, 10, 85, 160), Family, 20, FontWeight.Normal, TextAlign.Start, ColorRgba.White, true)));
        var unwrapped = Bounds(Render(new DrawTextCommand("Words that wrap onto multiple lines", new(10, 10, 85, 160), Family, 20, FontWeight.Normal, TextAlign.Start, ColorRgba.White, false)));
        Assert(wrapped.Bottom > unwrapped.Bottom + 20, "wrap must add lines");

        var gradient = new LinearGradient(1, 0, new[] { new GradientStop(new(1, 0, 0), 0), new GradientStop(new(0, 0, 1), 1) });
        var gradientPixels = Render(new DrawTextGradientCommand("MMMMMM", new(10, 10, 180, 60), Family, 30, FontWeight.Bold, TextAlign.Start, gradient, .5f));
        var maxAlpha = 0;
        var red = 0;
        var blue = 0;
        for (var i = 0; i < gradientPixels.Length; i += 4)
        {
            maxAlpha = Math.Max(maxAlpha, gradientPixels[i + 3]);
            if (gradientPixels[i + 3] > 40)
            {
                if (gradientPixels[i + 2] > gradientPixels[i])
                    red++;
                if (gradientPixels[i] > gradientPixels[i + 2])
                    blue++;
            }
            Assert(gradientPixels[i] <= gradientPixels[i + 3] && gradientPixels[i + 2] <= gradientPixels[i + 3], "premultiplied gradient");
        }
        Assert(maxAlpha > 80 && maxAlpha <= 129 && red > 0 && blue > 0, "gradient color and opacity");

        // Windows clips glyph ink to the command rectangle before blur. A one-pixel-high
        // rectangle above the glyph ink must not produce a full-height shadow.
        var tiny = new RectF(20, 20, 70, 1);
        var clipped = Bounds(Render(new DrawTextCommand("Mg", tiny, Family, 32, FontWeight.Normal, TextAlign.Start, ColorRgba.White)));
        var shadow = Bounds(Render(new DrawTextShadowCommand("Mg", tiny, Family, 32, FontWeight.Normal, TextAlign.Start, new(0, 0, 2, 0, ColorRgba.White, false))));
        Assert(clipped.Bottom < 21 && shadow.Bottom < 25, "shadow must clip glyph ink before blur, matching Windows");

        var redGradient = new LinearGradient(1, 0, new[] { new GradientStop(new(1, 0, 0), 0), new GradientStop(new(1, 0, 0), 1) });
        var emoji = Render(new DrawTextGradientCommand("😀", new(10, 10, 110, 90), "Noto Color Emoji", 40, FontWeight.Normal, TextAlign.Start, redGradient, .5f));
        var emojiInk = 0;
        for (var i = 0; i < emoji.Length; i += 4)
        {
            Assert(emoji[i] == 0 && emoji[i + 1] == 0, "color emoji leaked native color into gradient");
            Assert(emoji[i + 2] <= emoji[i + 3] && emoji[i + 3] <= 128, "color emoji gradient premultiplication/opacity");
            if (emoji[i + 3] > 0)
                emojiInk++;
        }
        Assert(emojiInk > 0, "color emoji gradient coverage is empty");

        // Color glyphs retain their intrinsic palette, but command alpha applies to the
        // whole glyph image rather than only to monochrome Cairo source paint.
        var plainEmoji = Render(new DrawTextCommand("😀", new(10, 10, 110, 90), "Noto Color Emoji", 40,
            FontWeight.Normal, TextAlign.Start, new(1, 0, 0, .25f)));
        var plainEmojiInk = 0;
        var intrinsicColorPixels = 0;
        for (var i = 0; i < plainEmoji.Length; i += 4)
        {
            var alpha = plainEmoji[i + 3];
            Assert(alpha <= 64, "normal color emoji ignores command opacity");
            Assert(plainEmoji[i] <= alpha && plainEmoji[i + 1] <= alpha && plainEmoji[i + 2] <= alpha,
                "normal color emoji is not premultiplied");
            if (alpha > 0)
                plainEmojiInk++;
            if (plainEmoji[i + 1] > 4 || plainEmoji[i] > 4)
                intrinsicColorPixels++;
        }
        Assert(plainEmojiInk > 0 && intrinsicColorPixels > 0, "normal emoji lost its intrinsic palette");
        var smallStyle = new ComputedStyle { FontFamily = Family, FontSize = 14 };
        var largeStyle = new ComputedStyle { FontFamily = Family, FontSize = 32 };
        var smallMetrics = backend.MeasureText("H", smallStyle); var largeMetrics = backend.MeasureText("H", largeStyle);
        var mixed = Render(new DrawTextCommand("H", new(10, 70 - smallMetrics.Baseline, 35, 50), Family, 14, FontWeight.Normal, TextAlign.Start, ColorRgba.White),
            new DrawTextCommand("H", new(80, 70 - largeMetrics.Baseline, 60, 60), Family, 32, FontWeight.Normal, TextAlign.Start, ColorRgba.White));
        int LastInk(int from, int to) { int bottom = -1; for (int y = 0; y < Height; y++) for (int x = from; x < to; x++) if (mixed[(y * Width + x) * 4 + 3] > 32) bottom = y; return bottom; }
        var smallBottom = LastInk(10, 45); var largeBottom = LastInk(80, 140);
        Assert(smallBottom >= 67 && smallBottom <= 70 && largeBottom >= 67 && largeBottom <= 70 && Math.Abs(smallBottom - largeBottom) <= 1, "mixed-size raster baselines align using measured baseline");
        Console.WriteLine("PASS text multilingual metrics/raster, baseline, cache, DPI, wrap/alignment, gradient alpha/color emoji and Windows-compatible shadow clipping.");

        byte[] Render(params PaintCommand[] commands)
        {
            backend.Render(new(commands), ColorRgba.Transparent);
            return presenter.CurrentFrame!.Pixels.ToArray();
        }
    }

    private static (int Left, int Top, int Right, int Bottom) Bounds(byte[] pixels)
    {
        int left = Width, top = Height, right = -1, bottom = -1;
        for (var y = 0; y < Height; y++)
            for (var x = 0; x < Width; x++)
                if (pixels[(y * Width + x) * 4 + 3] > 8)
                {
                    left = Math.Min(left, x);
                    top = Math.Min(top, y);
                    right = Math.Max(right, x);
                    bottom = Math.Max(bottom, y);
                }
        return (left, top, right, bottom);
    }
}
