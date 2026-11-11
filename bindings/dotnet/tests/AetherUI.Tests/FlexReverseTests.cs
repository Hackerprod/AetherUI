using AetherUI.Core.Native;
using AetherUI.Css;

namespace AetherUI.Tests;

internal static class FlexReverseTests
{
    internal static void Run()
    {
        Equal((int)FlexDirection.Row, 0, "Row ABI");
        Equal((int)FlexDirection.Column, 1, "Column ABI");
        Equal((int)FlexDirection.RowReverse, 2, "RowReverse ABI");
        Equal((int)FlexDirection.ColumnReverse, 3, "ColumnReverse ABI");
        Equal((int)JustifyContent.Start, 0, "historical Start ABI");
        Equal((int)JustifyContent.Center, 1, "Center ABI");
        Equal((int)JustifyContent.End, 2, "historical End ABI");
        Equal((int)JustifyContent.SpaceBetween, 3, "SpaceBetween ABI");
        Equal((int)JustifyContent.FlexStart, 0, "FlexStart alias");
        Equal((int)JustifyContent.FlexEnd, 2, "FlexEnd alias");
        Equal((int)JustifyContent.LogicalStart, 4, "LogicalStart ABI");
        Equal((int)JustifyContent.LogicalEnd, 5, "LogicalEnd ABI");
        foreach (var (direction, value, column) in new[] {
            ("row", 0, false), ("column", 1, true),
            ("row-reverse", 2, false), ("column-reverse", 3, true) })
        foreach (var (justify, alignment) in new[] {
            ("flex-start", 0), ("center", 1), ("flex-end", 2),
            ("space-between", 3), ("start", 4), ("end", 5) })
            Check(direction, value, column, justify, alignment);
    }

    private static void Check(string direction, int directionValue, bool column,
        string justify, int alignment)
    {
        using var doc = NativeDomDocument.Parse("<html><body><div id='host'><div id='item'></div></div></body></html>");
        using var runtime = new StyleRuntimeInterop(400, 400, 1);
        runtime.Load(doc, "", "html,body{margin:0;padding:0;}#host{display:flex;width:300px;height:300px;" +
            $"flex-direction:{direction};justify-content:{justify};align-items:flex-start;" +
            "}#item{width:40px;height:40px;flex:none;}");
        var resolved = runtime.Resolve(new int[doc.EnumerateElementHandles().Count()], 400, 400, 1);
        var handles = doc.EnumerateElementHandles().ToArray();
        IntPtr Node(string id) => handles.Single(h => doc.GetAttributes(h).Any(a => a.Key == "id" && a.Value == id));
        var exported = resolved.Styles[Array.IndexOf(handles, Node("host"))];
        Equal(exported.FlexDirection, directionValue, "native direction export");
        Equal(exported.JustifyContent, alignment, "native justify export");
        Equal((int)(FlexDirection)exported.FlexDirection, directionValue, "managed direction cast");
        Equal((int)(JustifyContent)exported.JustifyContent, alignment, "managed justify cast");
        using var map = runtime.CreateResolvedStyleMap(doc);
        using var engine = new NativeLayoutInterop((_, _, _) => default);
        using var layout = engine.Layout(doc, map, 400, 400, _ => default);
        IEnumerable<NativeLayoutBox> Walk(NativeLayoutBox box)
        {
            yield return box;
            for (var i = 0; i < box.ChildCount; i++)
                foreach (var child in Walk(layout.GetChild(box, i))) yield return child;
        }
        var host = Walk(layout.Root).First(b => b.Element == Node("host"));
        var item = Walk(host).First(b => b.Element == Node("item"));
        var reverse = directionValue >= 2;
        var expected = alignment switch {
            1 => 130f,
            2 => reverse ? 0f : 260f,
            4 => 0f,
            5 => 260f,
            _ => reverse ? 260f : 0f,
        };
        Equal(column ? item.BorderBox.Y - host.ContentBox.Y : item.BorderBox.X - host.ContentBox.X,
            expected, "native geometry through managed ABI");
        Equal(item.BorderBox.Width, 40, "item width");
        Equal(item.BorderBox.Height, 40, "item height");
    }

    private static void Equal(float actual, float expected, string field)
    {
        if (!float.IsFinite(actual) || MathF.Abs(actual - expected) > .01f)
            throw new InvalidOperationException($"{field}: actual={actual}, expected={expected}");
    }
}
