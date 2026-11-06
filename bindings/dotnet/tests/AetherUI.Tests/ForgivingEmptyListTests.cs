using AetherUI.Core.Native;
using System.Reflection;
using System.Runtime.InteropServices;

namespace AetherUI.Tests;

// Public ABI mirror: rule counts and computed styles, without visual/browser checks.
internal static class ForgivingEmptyListTests
{
    internal static void Run()
    {
        foreach (var function in new[] { "is", "where" })
            foreach (var body in new[] { "", " ", "/**/", ",", ":unknown", ":unknown,:nth-child(bogus)", "#ghost:hover:unknown", ":has(.ready,:unknown)" })
                Fixture($"{function}({body})", $".item:{function}({body}){{opacity:.8}}", 1, "FFF");
        Fixture("negated empty", ".item:not(:is()){opacity:.8}", 1, "TTT");
        Fixture("negated all invalid", ".item:not(:where(:unknown)){opacity:.8}", 1, "TTT");
        Fixture("double negation", ".item:not(:not(:is())){opacity:.8}", 1, "FFF");
        Fixture("valid impossible branch", ".item:is(:where()){opacity:.8}", 1, "FFF");
        Fixture("has empty branch", "#parent:has(:is()){opacity:.8}", 1, "FFF");
        Fixture("has negated empty branch", "#parent:has(:not(:where())){opacity:.8}", 1, "FFF", parentExpected: .8f);
        Fixture("not has empty branch", ".item:not(:has(:is())){opacity:.8}", 1, "TTT");
        Fixture("retained top-level empty", ".item:is(),#b{opacity:.8}", 2, "FTF");
        Fixture("continuation", ".item:where(:unknown){opacity:.8}#b{opacity:.8}", 2, "FTF");
        Fixture("discarded metadata tie", ".item:not(:is(#ghost:hover:unknown)){opacity:.8}.item{opacity:.4}", 2, "FFF", other: .4f);
        Fixture("discarded metadata reverse tie", ".item{opacity:.4}.item:not(:is(#ghost:hover:unknown)){opacity:.8}", 2, "TTT");
        Fixture("impossible ID counts", ".item:is(.item,#ghost:is()){opacity:.8}.item.item{opacity:.4}", 2, "TTT");
        Fixture("where remains zero", ".item:where(.item,#ghost:is()){opacity:.8}.item.item{opacity:.4}", 2, "FFF", other: .4f);
        foreach (var selector in new[] { ":not()", ":has()", ":not(.safe,)", ":has(.ready,:unknown)", ":not(:is(),)", ":has(:where(),:unknown)" })
            Fixture("strict " + selector, ".item" + selector + "{opacity:.8}", 0, "FFF");
    }

    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_style_runtime_add_stylesheet(IntPtr runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string css, int layer, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out long sheet, out int rules);
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_style_runtime_remove_stylesheet(IntPtr runtime, long sheet);

    private static void Fixture(string name, string css, int expectedRules, string mask, float other = .2f, float parentExpected = 1f)
    {
        const string markup = "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item'></div><div id='b' class='item'></div><div id='c' class='item'></div></div></body></html>";
        using var document = NativeDomDocument.Parse(markup);
        using var runtime = new StyleRuntimeInterop(800, 600, 1);
        var handle = (IntPtr)(typeof(StyleRuntimeInterop).GetField("_runtime", BindingFlags.NonPublic | BindingFlags.Instance)?.GetValue(runtime) ?? throw new InvalidOperationException("runtime missing"));
        if (aether_style_runtime_add_stylesheet(handle, css, 2, "forgiving-empty-count", out var sheet, out var rules) != 0)
            throw new InvalidOperationException(name + ": add stylesheet failed");
        if (aether_style_runtime_remove_stylesheet(handle, sheet) == 0)
            throw new InvalidOperationException(name + ": remove stylesheet failed");
        if (rules != expectedRules) throw new InvalidOperationException($"{name}: rules {rules}, expected {expectedRules}");
        runtime.Load(document, "", ".item{opacity:.2;}" + css);
        var elements = document.EnumerateElementHandles().ToArray();
        // Use original Load order, as required by Resolve.Styles.
        for (var pass = 0; pass < 2; pass++)
        {
            var resolution = runtime.Resolve(new int[elements.Length], 800, 600, 1);
            if (resolution.Styles.Count != elements.Length) throw new InvalidOperationException(name + ": style count mismatch");
            for (int j = 0; j < 4; j++)
            {
                var id = j == 3 ? "parent" : new[] { "a", "b", "c" }[j];
                var index = Array.FindIndex(elements, node => document.GetAttributes(node).Any(a => a.Key == "id" && a.Value == id));
                if (index < 0) throw new InvalidOperationException(name + ": missing " + id);
                var expected = j == 3 ? parentExpected : mask[j] == 'T' ? .8f : other;
                var actual = resolution.Styles[index].Opacity;
                if (Math.Abs(actual - expected) >= .0001) throw new InvalidOperationException($"{name}: {id} opacity {actual}, expected {expected}");
            }
        }
    }
}
