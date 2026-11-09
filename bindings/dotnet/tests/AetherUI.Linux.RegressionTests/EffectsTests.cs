using AetherUI.Css;
using AetherUI.Linux;
using AetherUI.Linux.Graphics;
using AetherUI.Primitives;
using AetherUI.Rendering;

internal static class EffectsTests
{
    internal static void Run()
    {
        var memory = new MemoryFramePresenter();
        using var renderer = new CairoRenderBackend(new LinuxSoftwareRenderTarget(memory), new RenderViewport(80, 80, 1));
        void Render(params PaintCommand[] commands) => renderer.Render(new DisplayList(commands), ColorRgba.Transparent);
        byte Pixel(int x, int y, int channel = 3) => memory.CurrentFrame!.Pixels.Span[(y * 80 + x) * 4 + channel];
        static void Check(bool ok, string why) { if (!ok) throw new InvalidOperationException("Effects: " + why); }
        var rect = new RectF(20, 20, 40, 40);
        var shadow = new BoxShadow(0, 0, 8, 0, ColorRgba.Black, false);
        Render(new DrawBoxShadowCommand(rect, CornerRadii.Uniform(5), shadow));
        Check(Pixel(18, 40) > 0, "outset blur must have a halo outside the border box");
        Check(Pixel(40, 40) == 0, "outset shadow must not fill transparent element content");
        Check(Pixel(0, 40) == 0, "finite three-sigma halo must not reach distant pixels");
        Render(new DrawBoxShadowCommand(rect, CornerRadii.Zero, shadow with { Inset = true }));
        Check(Pixel(21, 40) > 0, "inset blur must darken the inside edge");
        Check(Pixel(18, 40) == 0, "inset shadow must stay clipped to the box");
        Check(Pixel(40, 40) == 0, "inset shadow must preserve the clear interior");
        Render(new PushClipCommand(new RectF(0, 0, 30, 80), CornerRadii.Zero),
            new DrawBoxShadowCommand(rect, CornerRadii.Zero, shadow), new PopClipCommand());
        Check(Pixel(18, 40) > 0 && Pixel(61, 40) == 0, "shadow composite must preserve caller clip");
        Render(new DrawTextShadowCommand("Ág", new RectF(10, 10, 50, 30), "DejaVu Sans", 20,
            FontWeight.Normal, TextAlign.Left, new BoxShadow(2, 2, 4, 0, ColorRgba.Black, false)));
        Check(memory.CurrentFrame!.Pixels.Span.ToArray().Where((_, i) => i % 4 == 3).Any(v => v != 0), "Pango text shadow must produce pixels");
        renderer.Render(new DisplayList(new PaintCommand[] {
            new ApplyBackdropFilterCommand(new RectF(10,10,60,60), CornerRadii.Zero, new[] {
                new BackdropFilter(BackdropFilterKind.Brightness, CssLength.Zero, 2),
                new BackdropFilter(BackdropFilterKind.Contrast, CssLength.Zero, 2) }) }), new ColorRgba(.2f,.2f,.2f));
        Check(Math.Abs(Pixel(40,40,2) - 77) <= 1, "brightness then contrast must execute in declared order");
        Check(Pixel(5,5,2) == 51, "backdrop filter must not alter outside clip");
        renderer.Render(new DisplayList(new PaintCommand[] {
            new ApplyBackdropFilterCommand(new RectF(0,0,80,80), CornerRadii.Zero, new[] {
                new BackdropFilter(BackdropFilterKind.Saturate, CssLength.Zero, 0) }) }), new ColorRgba(1,0,0,.5f));
        Check(Pixel(40,40,0) == Pixel(40,40,1) && Pixel(40,40,1) == Pixel(40,40,2), "zero saturation must yield grayscale");
        Check(Pixel(40,40) == 128, "backdrop replacement must preserve alpha, not source-over twice");
        renderer.Render(new DisplayList(new PaintCommand[] {
            new PushTransformCommand(new TransformMatrix(0,1,-1,0,80,0)),
            new ApplyBackdropFilterCommand(new RectF(10,20,30,40), CornerRadii.Zero, new[] {
                new BackdropFilter(BackdropFilterKind.Brightness,CssLength.Zero,.5f) }), new PopTransformCommand() }), ColorRgba.White);
        Check(Math.Abs(Pixel(40,20,2)-128)<=1 && Pixel(10,20,2)==255 && Pixel(40,50,2)==255,
            "backdrop must clip under rotation and sample in device coordinates");
        Render(new FillRoundedRectangleCommand(new RectF(30,30,20,20),CornerRadii.Zero,ColorRgba.White),
            new ApplyBackdropFilterCommand(new RectF(0,0,80,80),CornerRadii.Zero,new[] {
                new BackdropFilter(BackdropFilterKind.Blur,new CssLength(6,CssUnit.Pixels),0) }));
        Check(Pixel(28,40)>0 && Pixel(40,40)>200, "backdrop blur must spread already-painted pixels");
        var before = memory.CurrentFrame;
        try
        {
            Render(new DrawBoxShadowCommand(rect,CornerRadii.Zero,shadow with {BlurRadius=200}));
            throw new InvalidOperationException("Effects: excessive kernel unexpectedly accepted");
        }
        catch (ArgumentOutOfRangeException) { }
        Check(ReferenceEquals(before,memory.CurrentFrame), "effect failure must not present a partial frame");
        Console.WriteLine("PASS effects: shadows, clipping, text mask, filter order/alpha, affine backdrop, bounded failure");
    }
}
