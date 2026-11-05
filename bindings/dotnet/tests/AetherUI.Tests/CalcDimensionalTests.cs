using AetherUI.Core.Native;

namespace AetherUI.Tests;

internal static class CalcDimensionalTests
{
    internal static void Run()
    {
        (string Css, float Width)[] cases =
        [
            ("10px", 10), ("0", 0), ("calc(10px + 0px)", 10), ("calc(0px + 10px)", 10),
            ("calc(10px * 0)", 0), ("calc(0 * 10px)", 0), ("calc(10px / 2)", 5),
            ("calc((1 - 1) * 10px)", 0), ("calc(10px + 0)", 73), ("calc(0 + 10px)", 73),
            ("calc(10px - 0)", 73), ("calc(0 - 10px)", 73), ("calc(0)", 73),
            ("calc(1 - 1)", 73), ("calc(10px + (2 - 2))", 73), ("calc(10px + 1)", 73)
        ];
        foreach (var c in cases) Check("width:" + c.Css, c.Width, 7);
        foreach (var css in new[] { "calc(0)", "calc(1 - 1)", "calc(0px)" }) Check("line-height:" + css, 73, 0);

        // Focused guards derived from the existing contextual policy.
        Check("width:calc(10px - 10px)", 0, 7);
        Check("line-height:calc(10px + 0)", 73, 7);
        Check("line-height:calc(2)", 73, 7);
        Check("--lh:calc(1 - 1);line-height:var(--lh)", 73, 0);
    }

    private static void Check(string declaration, float width, float lineHeight)
    {
        using var document = NativeDomDocument.Parse("<!doctype html><html><body><div id='target'></div></body></html>");
        using var runtime = new StyleRuntimeInterop(800, 600, 1);
        runtime.Load(document, "", "html,body{display:block;margin:0;padding:0;border:0;}" +
            "#target{display:block;width:73px;height:10px;font-size:16px;line-height:7px;}#target{" + declaration + ";}");
        var elements = document.EnumerateElementHandles().ToArray();
        var target = Array.FindIndex(elements, e => document.GetAttributes(e).Any(a => a.Key == "id" && a.Value == "target"));
        if (target < 0) throw new InvalidOperationException("calc target missing");
        var resolved = runtime.Resolve(new int[elements.Length], 800, 600, 1);
        // Resolve reads get_update through the real interop reader.
        Equal(resolved.Styles[target].LineHeight, lineHeight, declaration + " computed line-height");
        Equal(resolved.Styles[target].FontSize, 16, declaration + " computed font-size");
        using var styles = runtime.CreateResolvedStyleMap(document);
        using var engine = new NativeLayoutInterop((_, _, _) => new NativeTextMetrics(0, 1, .8f));
        using var layout = engine.Layout(document, styles, 800, 600, _ => default);
        var found = false;
        for (var i = 0; i < layout.PaintOrderCount; i++)
        {
            var box = layout.GetPaintOrderBox(i);
            if (box.Element != elements[target]) continue;
            Equal(box.BorderBox.Width, width, declaration + " BorderBox width");
            found = true;
            break;
        }
        if (!found) throw new InvalidOperationException("calc target box missing");
    }

    private static void Equal(float actual, float expected, string context)
    {
        if (MathF.Abs(actual - expected) >= .001f || !float.IsFinite(actual))
            throw new InvalidOperationException($"{context}: expected {expected}, actual {actual}");
    }
}
