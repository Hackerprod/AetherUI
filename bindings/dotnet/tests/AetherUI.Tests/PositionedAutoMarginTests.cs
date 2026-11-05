using AetherUI.Core.Native;

namespace AetherUI.Tests;

internal static class PositionedAutoMarginTests
{
    private const string Markup = "<!doctype html><html><body><div id='host'><div id='item'></div></div></body></html>";
    private const string BaseCss = "html,body,div{display:block;margin:0;padding:0;border:0}" +
        "#host{position:relative;width:300px;height:200px}" +
        "#item{position:absolute;left:20px;right:40px;top:10px;bottom:30px;width:100px;height:60px;margin:auto}";

    internal static void Run()
    {
        var count = 0;
        using (var f = new Fixture())
        {
            f.ResolveAndLayout("");
            f.Expect(new(90,60,100,60), new(20,10,240,160));
            f.Auto(true,true,true,true);
            count++;
        }
        using (var f = new Fixture())
        {
            f.ResolveAndLayout("#item{margin:0;margin-right:auto;margin-bottom:auto}");
            f.Expect(new(20,10,100,60), new(20,10,240,160));
            f.Auto(false,false,true,true);
            count++;
        }
        using (var f = new Fixture())
        {
            f.ResolveAndLayout("#item{margin-right:10%;margin-bottom:10%}");
            f.Expect(new(130,80,100,60), new(20,10,240,160));
            Equal(f.Item.MarginBox.X+f.Item.MarginBox.Width-f.Item.BorderBox.X-f.Item.BorderBox.Width,30,"right 10%");
            Equal(f.Item.MarginBox.Y+f.Item.MarginBox.Height-f.Item.BorderBox.Y-f.Item.BorderBox.Height,30,"bottom 10% uses width");
            f.Auto(true,true,false,false);
            count++;
        }
        foreach (var borderBox in new[] { false, true })
        {
            using var f = new Fixture();
            f.ResolveAndLayout("#item{padding:5px;border:2px solid;box-sizing:"+(borderBox?"border-box":"content-box")+"}");
            Rect(f.Item.MarginBox,new(20,10,240,160),"G11 M");
            Rect(f.Item.BorderBox,borderBox?new(90,60,100,60):new(83,53,114,74),"G11 B");
            Rect(f.Item.PaddingBox,borderBox?new(92,62,96,56):new(85,55,110,70),"G11 P");
            Rect(f.Item.ContentBox,borderBox?new(97,67,86,46):new(90,60,100,60),"G11 C");
            f.Auto(true,true,true,true);
            count++;
        }
        foreach (var prepared in new[] { false, true })
        {
            using var f = new Fixture();
            f.ResolveAndLayout("#host{left:37px;top:23px;width:150px;height:90px}#item{position:fixed}",prepared);
            Rect(f.Box("host").BorderBox,new(37,23,150,90),"G13 host");
            f.Expect(new(90,60,100,60),new(20,10,240,160));
            f.Auto(true,true,true,true);
            count++;
        }
        using (var f = new Fixture())
        {
            // One caller-owned DOM and engine. Every Resolve creates fresh handles/map, and each
            // prepared layout consumes the previous result before its map is released.
            string[] changes = ["", "#item{margin:0}", "#item{margin:0;margin-right:auto;margin-bottom:auto}", "#item{margin:auto;width:120px}"];
            NativeRect[] border = [new(90,60,100,60),new(20,10,100,60),new(20,10,100,60),new(80,60,120,60)];
            NativeRect[] margin = [new(20,10,240,160),new(20,10,100,60),new(20,10,240,160),new(20,10,240,160)];
            for (var i=0;i<changes.Length;i++)
            {
                f.ResolveAndLayout(changes[i],true);
                f.Expect(border[i],margin[i]);
                f.Auto(i is 0 or 3,i is 0 or 3,i!=1,i!=1);
                count++;
            }
        }
        Equal(count,11,"enumerated snapshots");
        Console.WriteLine("G01/G02/G04/G11/G13/G15: 11 managed snapshots passed");
    }

    private sealed class Fixture : IDisposable
    {
        private readonly NativeDomDocument _document = NativeDomDocument.Parse(Markup);
        private readonly StyleRuntimeInterop _runtime = new(300,200,1);
        private readonly NativeLayoutInterop _engine = new((_,_,_)=>default);
        private NativeLayoutResult? _layout;
        private NativeStyleMap? _map;
        private IntPtr[] _handles = [];
        private NativeComputedStyle[] _styles = [];
        public NativeLayoutBox Item => Box("item");

        public void ResolveAndLayout(string css,bool prepared=false)
        {
            _runtime.Load(_document,"",BaseCss+css);
            var resolution=_runtime.Resolve(new int[_document.EnumerateElementHandles().Count()],300,200,1);
            _handles=_document.EnumerateElementHandles().ToArray();
            _styles=resolution.Styles.ToArray();
            var nextMap=_runtime.CreateResolvedStyleMap(_document);
            NativeLayoutResult next;
            try
            {
                if(prepared) _engine.PrepareTransitionContext(null,nextMap,_layout);
                next=_engine.Layout(_document,nextMap,300,200,_=>default);
            }
            catch { nextMap.Dispose(); throw; }
            _layout?.Dispose();
            _map?.Dispose();
            _layout=next;_map=nextMap;
        }
        private IntPtr Node(string id)=>_handles.Single(h=>_document.GetAttributes(h).Any(a=>a.Key=="id"&&a.Value==id));
        public NativeLayoutBox Box(string id)
        {
            var layout=_layout??throw new InvalidOperationException("layout missing");
            var target=Node(id);
            NativeLayoutBox? Find(NativeLayoutBox b)
            {
                if(b.Element==target)return b;
                for(var i=0;i<b.ChildCount;i++) { var found=Find(layout.GetChild(b,i));if(found.HasValue)return found; }
                return null;
            }
            return Find(layout.Root)??throw new InvalidOperationException("box missing: "+id);
        }
        public void Expect(NativeRect border,NativeRect margin)
        {
            Rect(Item.MarginBox,margin,"M");Rect(Item.BorderBox,border,"B");
            Rect(Item.PaddingBox,border,"P");Rect(Item.ContentBox,border,"C");
        }
        public void Auto(bool left,bool top,bool right,bool bottom)
        {
            var m=_styles[Array.IndexOf(_handles,Node("item"))].Margin;
            if((m.Left.Unit==0)!=left||(m.Top.Unit==0)!=top||(m.Right.Unit==0)!=right||(m.Bottom.Unit==0)!=bottom)
                throw new InvalidOperationException("CSS AUTO units changed during layout");
        }
        public void Dispose()
        {
            _layout?.Dispose();_map?.Dispose();_engine.Dispose();_runtime.Dispose();_document.Dispose();
        }
    }
    private static void Equal(float actual,float expected,string field)
    {
        if(!float.IsFinite(actual)||MathF.Abs(actual-expected)>=.01f)
            throw new InvalidOperationException($"{field}: actual={actual}, expected={expected}");
    }
    private static void Rect(NativeRect actual,NativeRect expected,string field)
    {
        Equal(actual.X,expected.X,field+" X");Equal(actual.Y,expected.Y,field+" Y");
        Equal(actual.Width,expected.Width,field+" width");Equal(actual.Height,expected.Height,field+" height");
    }
}
