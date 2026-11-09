using AetherUI.Css;
using AetherUI.Linux;
using AetherUI.Linux.Graphics;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

static class Program
{
    private static void Assert(bool condition,string message){if(!condition)throw new InvalidOperationException(message);}
    private static void Throws<T>(Action action) where T:Exception {try{action();}catch(T){return;}throw new InvalidOperationException($"Expected {typeof(T).Name}");}
    private sealed record UnknownCommand:PaintCommand;
    private sealed class CallbackPresenter:ILinuxFramePresenter {public Action? Callback;public int Calls; public void Present(int w,int h,int stride,ReadOnlySpan<byte> pixels){Calls++;Callback?.Invoke();}}
    static void Main()
    {
        var presenter=new MemoryFramePresenter();var viewport=new RenderViewport(96,96,1);
        using var backend=new CairoRenderBackend(new(presenter),viewport);
        Assert((object)backend is not IIncrementalRenderBackend,"Linux must declare full repaint only");
        var r=new RectF(8,8,40,30);var radii=new CornerRadii(4,8,8,4,6,9,9,6);
        var gradient=new LinearGradient(1,0,new[]{new GradientStop(new(1,0,0),0),new GradientStop(new(0,0,1),1)});
        var fixtures=new PaintCommand[]{
            new FillRoundedRectangleCommand(r,radii,new(1,0,0)),
            new StrokeRoundedRectangleCommand(r,radii,2,new(0,1,0)),
            new FillRoundedRectangleGradientCommand(r,radii,r,gradient,1),
            new FillBorderSideCommand(r,radii,new(12,12,32,22),default,0,(int)BorderStyle.Solid,new(1,1,0)),
            new DrawTextCommand("Español عربى 漢字 😀",new(0,40,96,30),"sans-serif",12,FontWeight.Normal,TextAlign.Start,ColorRgba.White),
            new DrawTextGradientCommand("Gradient",new(0,40,96,30),"sans-serif",12,FontWeight.Normal,TextAlign.Start,gradient,1),
            new DrawTextShadowCommand("Shadow",new(0,40,96,30),"sans-serif",12,FontWeight.Normal,TextAlign.Start,new(1,2,2,0,ColorRgba.White,false)),
            new DrawBoxShadowCommand(r,radii,new(2,2,3,1,ColorRgba.White,false)),
            new ApplyBackdropFilterCommand(r,radii,new[]{new BackdropFilter(BackdropFilterKind.Brightness,default,.5f)}),
            new DrawBackgroundImageCommand("memory",r,default,default,BackgroundImageSizeKind.Auto,default,default,BackgroundRepeat.NoRepeat,BackgroundRepeat.NoRepeat,1,new(0,0,1,1),r,true,new(new(1,1),new byte[]{0,255,0,255})),
            new PushClipCommand(new(0,0,90,90),default),new PopClipCommand(),
            new PushTransformCommand(new(1,0,0,1,2,2)),new PopTransformCommand(),
            new PushScrollOffsetCommand(7),new PopScrollOffsetCommand(),new DrawScrollbarChromeCommand(7)
        };
        var types=typeof(PaintCommand).Assembly.GetTypes().Where(t=>t.IsSubclassOf(typeof(PaintCommand))).OrderBy(t=>t.Name).ToArray();
        Assert(types.SequenceEqual(fixtures.Select(x=>x.GetType()).OrderBy(t=>t.Name)),"Every PaintCommand requires a fixture");
        int providerCalls=0;
        backend.SetScrollVisualStateProvider(id=>{providerCalls++;return new(new(1,2),new(true,true,new(0,90,90,6),new(5,90,20,6),new(90,0,6,90),new(90,5,6,20),new(90,90,6,6)));});
        backend.Render(new(fixtures),ColorRgba.Transparent);Assert(providerCalls==1,"Scroll provider sampled once per frame");
        Assert(presenter.CurrentFrame!.Pixels.Span.ToArray().Any(x=>x!=0),"Raster must contain pixels");
        var old=presenter.CurrentFrame;
        Throws<NotSupportedException>(()=>backend.Render(new(new PaintCommand[]{new UnknownCommand()}),default));Assert(ReferenceEquals(old,presenter.CurrentFrame),"Unknown command presented partial frame");
        Throws<ArgumentException>(()=>backend.Render(new(new PaintCommand[]{new PopClipCommand()}),default));
        Throws<ArgumentException>(()=>backend.Render(new(new PaintCommand[]{new PushClipCommand(r,default)}),default));
        Throws<ArgumentException>(()=>backend.Render(new(new PaintCommand[]{new PushClipCommand(r,default),new PopTransformCommand()}),default));
        Throws<ArgumentException>(()=>backend.Render(new(new PaintCommand[]{new FillRoundedRectangleCommand(new(float.NaN,0,1,1),default,default)}),default));
        foreach(var dpi in new[]{0f,float.NaN,float.PositiveInfinity,-1})Throws<ArgumentOutOfRangeException>(()=>backend.Resize(96,96,dpi));
        Throws<ArgumentOutOfRangeException>(()=>backend.Resize(int.MaxValue,10,1));
        backend.Resize(0,20,1);backend.Render(new(Array.Empty<PaintCommand>()),ColorRgba.White);Assert(ReferenceEquals(old,presenter.CurrentFrame),"Suspended viewport presented");
        for(var n=1;n<=3;n++){backend.Resize(96,96,n);backend.Render(new(new PaintCommand[]{new FillRoundedRectangleCommand(new(0,0,4,4),default,ColorRgba.White)}),default);Assert(presenter.CurrentFrame!.Pixels.Span[(n*4-1)*4+3]>0,"DPI geometry");}
        backend.SetScrollOffsetProvider(_=>new(float.NaN,0));Throws<ArgumentException>(()=>backend.Render(new(new PaintCommand[]{new PushScrollOffsetCommand(1),new PopScrollOffsetCommand()}),default));backend.SetScrollOffsetProvider(null);
        var style=new ComputedStyle{FontFamily="sans-serif",FontSize=18};
        foreach(var text in new[]{"","Español","e\u0301","العربية","漢字","😀","office"}){var m=backend.MeasureText(text,style);Assert(float.IsFinite(m.Width)&&m.Height>0,"Text metrics");}
        Assert(backend.MeasureText("Hola   ",style).Width>backend.MeasureText("Hola",style).Width,"Trailing whitespace");
        Exception? threadError=null;var thread=new Thread(()=>{try{backend.Resize(1,1,1);}catch(Exception ex){threadError=ex;}});thread.Start();thread.Join();Assert(threadError is InvalidOperationException,"Cross-thread rejection");
        var callback=new CallbackPresenter();using var other=new CairoRenderBackend(new(callback),new(8,8,1));
        other.Render(new(Array.Empty<PaintCommand>()),ColorRgba.White);var confirmed=other.LastConfirmedFrame.ToArray();
        callback.Callback=()=>throw new InvalidOperationException("copied then failed");Throws<FramePresentationException>(()=>other.Render(new(Array.Empty<PaintCommand>()),ColorRgba.Black));Assert(other.PresentationUncertain&&callback.Calls==2&&confirmed.SequenceEqual(other.LastConfirmedFrame.ToArray()),"Uncertain presentation state");
        callback.Callback=()=>Throws<InvalidOperationException>(()=>other.Resize(2,2,1));other.Render(new(Array.Empty<PaintCommand>()),ColorRgba.Black);Assert(!other.PresentationUncertain,"Explicit recovery request");
        EffectsTests.Run();backend.Dispose();backend.Dispose();Throws<ObjectDisposedException>(()=>backend.Resize(1,1,1));
        Console.WriteLine("PASS Linux offscreen contracts, all 17 command fixtures, lifecycle, text and effects. No window/event integration exercised.");
    }
}
