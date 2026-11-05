using System.Reflection;
using AetherUI.Core.Native;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

namespace AetherUI.Tests;

internal static class RelativePercentageTests
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;

    internal static void Run()
    {
        foreach (var display in new[] { "block", "inline-block" })
        foreach (var definite in new[] { true, false })
        {
            RectF[][]? zero = null;
            foreach (var percent in definite ? new[] { 0, 30, 50 } : new[] { 0, 30 })
            {
                using var renderer = new Recorder();
                var component = new Component(display, definite, percent);
                using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
                host.SetViewport(360, 340, 1);
                host.Render();
                var dx = 2.4f * percent;
                var dy = definite ? 2f * percent : 0f;
                var ids = new[] { "outer", "item", "leaf", "after" };
                var rects = ids.Select(id => ManagedRects(host, id)).ToArray();
                zero ??= rects;
                for (var n = 0; n < ids.Length; n++)
                {
                    var native = NativeRects(host, ids[n]);
                    for (var r = 0; r < 4; r++)
                    {
                        Equal(rects[n][r], native[r], "native and managed four rects");
                        var expected = zero[n][r];
                        if (ids[n] is "item" or "leaf") expected = expected.Offset(dx, dy);
                        Equal(rects[n][r], expected, "rigid visual translation and stable flow");
                    }
                }
                Equal(rects[0][1], new(20, 20, 240, definite ? 200 : 52), "parent geometry");
                Equal(rects[1][1], new(20 + dx, 20 + dy, 60, 40), "item border geometry");
                Equal(rects[1][2], new(22 + dx, 22 + dy, 56, 36), "item padding geometry");
                Equal(rects[1][3], new(25 + dx, 25 + dy, 50, 30), "item content geometry");
                Equal(rects[2][1], new(31 + dx, 29 + dy, 10, 10), "leaf geometry");
                Equal(rects[3][1], new(20, 60, 20, 12), "following sibling flow");
                foreach (var n in new[] { 1, 2, 3 })
                    Require(renderer.Last!.Commands.OfType<FillRoundedRectangleCommand>().Any(fill => fill.Rectangle == rects[n][1]), "display-list coordinates");
                foreach (var (id, point) in new[] { ("item", new PointF(60 + dx, 40 + dy)), ("leaf", new PointF(33 + dx, 31 + dy)) })
                {
                    host.Render();
                    // Host input can resolve styles. Never retain native map handles across it.
                    var result = (NativeLayoutResult)typeof(UiDocumentHost).GetField("_nativeLayout", Private)!.GetValue(host)!;
                    var styles = (NativeStyleMap)typeof(UiDocumentHost).GetField("_nativeVisualStyleMap", Private)!.GetValue(host)!;
                    var bridge = typeof(UiDocumentHost).GetField("_layoutInterop", Private)!.GetValue(host)!;
                    var interop = (NativeLayoutInterop)bridge.GetType().GetField("_native", Private)!.GetValue(bridge)!;
                    var owner = FindNative(host, result, id);
                    Require(interop.HitTest(result, null, point.X, point.Y, _ => default) == owner.Element, "snapshot hit");
                    Require(interop.HitTest(result, styles, point.X, point.Y, _ => default) == owner.Element, "current-style hit");
                    Require(interop.HitTestInput(result, styles, point.X, point.Y, _ => default).Owner == owner.Element, "native input owner");
                    Require(host.Layout!.HitTest(point)?.Id == id, "managed hit");
                    component.Downs.Clear(); component.Clicks.Clear();
                    host.PointerPressed(point); host.PointerReleased(point);
                    Require(component.Downs.Contains(id) && component.Clicks.Contains(id), "host callbacks");
                }
            }
        }
        foreach (var display in new[] { "block", "inline-block" })
        {
            using var renderer = new Recorder();
            var component = new Component(display, true, 30);
            using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer, new UiHostOptions());
            host.SetViewport(360, 340, 1); host.Render();
            Equal(ManagedRects(host, "item")[1], new(92, 80, 60, 40), "before height change");
            component.UseAuto(); host.Render();
            Equal(ManagedRects(host, "item")[1], new(92, 20, 60, 40), "no stale definite base after height change");
        }
    }

    private static RectF[] ManagedRects(UiDocumentHost host, string id)
    {
        var box = host.Layout!.FindPrimaryBox(host.Document!.GetElementById(id)!)!;
        return [box.MarginBox, box.BorderBox, box.PaddingBox, box.ContentBox];
    }

    private static RectF[] NativeRects(UiDocumentHost host, string id)
    {
        var result = (NativeLayoutResult)typeof(UiDocumentHost).GetField("_nativeLayout", Private)!.GetValue(host)!;
        var box = FindNative(host, result, id);
        static RectF Convert(NativeRect r) => new(r.X, r.Y, r.Width, r.Height);
        return [Convert(box.MarginBox), Convert(box.BorderBox), Convert(box.PaddingBox), Convert(box.ContentBox)];
    }

    private static NativeLayoutBox FindNative(UiDocumentHost host, NativeLayoutResult result, string id)
    {
        var dom = typeof(UiDocumentHost).GetField("_nativeDom", Private)!.GetValue(host)!;
        var getHandle = dom.GetType().GetMethod("GetNativeHandle")!;
        var handle = (IntPtr)getHandle.Invoke(dom, [host.Document!.GetElementById(id)!])!;
        for (var n = 0; n < result.PaintOrderCount; n++)
        {
            var box = result.GetPaintOrderBox(n);
            if (box.Element == handle) return box;
        }
        throw new InvalidOperationException("native fixture box missing: " + id);
    }

    private static void Require(bool value, string message)
    {
        if (!value) throw new InvalidOperationException("relative percentage: " + message);
    }
    private static void Equal(RectF a, RectF b, string message) => Require(
        MathF.Abs(a.X - b.X) < .02f && MathF.Abs(a.Y - b.Y) < .02f && MathF.Abs(a.Width - b.Width) < .02f && MathF.Abs(a.Height - b.Height) < .02f,
        $"{message}: actual {a}, expected {b}");

    private sealed class Recorder : IRenderBackend
    {
        internal DisplayList? Last;
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList list, ColorRgba clear) => Last = list;
        public void Dispose() { }
    }

    private sealed class Component(string display, bool definite, int percent) : UiComponent
    {
        internal readonly List<string> Downs = [], Clicks = [];
        private bool _auto;
        internal void UseAuto() { _auto = true; StateHasChanged(); }
        protected override void BuildRenderTree(MarkupWriter w)
        {
            w.WriteLiteral("<style>html,body{margin:0;background:#ffffff}body{width:360px;height:340px;box-sizing:border-box;padding-top:20px}#outer{margin-left:20px;width:240px;height:" + (definite && !_auto ? "200px" : "auto") + ";font-size:0;line-height:0}#item{display:" + display + ";vertical-align:top;position:relative;left:" + percent + "%;top:" + percent + "%;width:50px;height:30px;padding:3px;border:2px solid #000000;background:#ff0000;cursor:pointer}#leaf{margin-left:6px;margin-top:4px;width:10px;height:10px;background:#00ff00;cursor:help}#after{width:20px;height:12px;background:#0000ff}</style><body><div id='outer'><div id='item'");
            w.WriteEventAttribute("onmousedown", () => Downs.Add("item")); w.WriteEventAttribute("onclick", () => Clicks.Add("item"));
            w.WriteLiteral("><div id='leaf'");
            w.WriteEventAttribute("onmousedown", () => Downs.Add("leaf")); w.WriteEventAttribute("onclick", () => Clicks.Add("leaf"));
            w.WriteLiteral("></div></div><div id='after'></div></div></body>");
        }
    }
}
