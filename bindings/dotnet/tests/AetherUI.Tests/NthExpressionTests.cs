using AetherUI.Core.Native;
using System.Reflection;
using System.Runtime.InteropServices;

namespace AetherUI.Tests;

internal static class NthExpressionTests
{
    internal static void Run()
    {
        Fixture("corpus4 baseline", "", 0, "FFF");
        Fixture("corpus4 odd", ".item:nth-child(odd){opacity:.8;}", 1, "TFT");
        Fixture("corpus4 2n-1", ".item:nth-child(2n-1){opacity:.8;}", 1, "TFT");
        Fixture("corpus4 2n+1", ".item:nth-child(2n+1){opacity:.8;}", 1, "TFT");
        Fixture("guards even", ".item:nth-child(even){opacity:.8;}", 1, "FTF");
        Fixture("guards ODD", ".item:nth-child(ODD){opacity:.8;}", 1, "TFT");
        Fixture("guards n", ".item:nth-child(n){opacity:.8;}", 1, "TTT");
        Fixture("guards +n", ".item:nth-child(+n){opacity:.8;}", 1, "TTT");
        Fixture("guards +1n", ".item:nth-child(+1n){opacity:.8;}", 1, "TTT");
        Fixture("guards n+0", ".item:nth-child(n+0){opacity:.8;}", 1, "TTT");
        Fixture("guards -n", ".item:nth-child(-n){opacity:.8;}", 1, "FFF");
        Fixture("guards -n+3", ".item:nth-child(-n+3){opacity:.8;}", 1, "TTT");
        Fixture("guards -2n+3", ".item:nth-child(-2n+3){opacity:.8;}", 1, "TFT");
        Fixture("guards 2n+0", ".item:nth-child(2n+0){opacity:.8;}", 1, "FTF");
        Fixture("guards -2n-1", ".item:nth-child(-2n-1){opacity:.8;}", 1, "FFF");
        Fixture("guards 2", ".item:nth-child(2){opacity:.8;}", 1, "FTF");
        Fixture("guards +2", ".item:nth-child(+2){opacity:.8;}", 1, "FTF");
        Fixture("guards 0n+2", ".item:nth-child(0n+2){opacity:.8;}", 1, "FTF");
        Fixture("guards 0", ".item:nth-child(0){opacity:.8;}", 1, "FFF");
        Fixture("guards -2", ".item:nth-child(-2){opacity:.8;}", 1, "FFF");
        Fixture("guards 0n", ".item:nth-child(0n){opacity:.8;}", 1, "FFF");
        Fixture("guards +0n+0", ".item:nth-child(+0n+0){opacity:.8;}", 1, "FFF");
        Fixture("guards 3", ".item:nth-child(3){opacity:.8;}", 1, "FFT");
        Fixture("guards  \t2N\r\n+\f1 ", ".item:nth-child( \t2N\r\n+\f1 ){opacity:.8;}", 1, "TFT");
        Fixture("guards n- 1", ".item:nth-child(n- 1){opacity:.8;}", 1, "TTT");
        Fixture("guards n - 1", ".item:nth-child(n - 1){opacity:.8;}", 1, "TTT");
        Fixture("guards + 2", ".item:nth-child(+ 2){opacity:.8;}", 0, "FFF");
        Fixture("guards - 2", ".item:nth-child(- 2){opacity:.8;}", 0, "FFF");
        Fixture("guards + n", ".item:nth-child(+ n){opacity:.8;}", 0, "FFF");
        Fixture("guards - n", ".item:nth-child(- n){opacity:.8;}", 0, "FFF");
        Fixture("guards 2 n", ".item:nth-child(2 n){opacity:.8;}", 0, "FFF");
        Fixture("guards 2n 1", ".item:nth-child(2n 1){opacity:.8;}", 0, "FFF");
        Fixture("guards 2n1", ".item:nth-child(2n1){opacity:.8;}", 0, "FFF");
        Fixture("guards n--1", ".item:nth-child(n--1){opacity:.8;}", 0, "FFF");
        Fixture("guards n+-1", ".item:nth-child(n+-1){opacity:.8;}", 0, "FFF");
        Fixture("guards n-+1", ".item:nth-child(n-+1){opacity:.8;}", 0, "FFF");
        Fixture("guards n++1", ".item:nth-child(n++1){opacity:.8;}", 0, "FFF");
        Fixture("guards n + -1", ".item:nth-child(n + -1){opacity:.8;}", 0, "FFF");
        Fixture("guards n - +1", ".item:nth-child(n - +1){opacity:.8;}", 0, "FFF");
        Fixture("guards +", ".item:nth-child(+){opacity:.8;}", 0, "FFF");
        Fixture("guards -", ".item:nth-child(-){opacity:.8;}", 0, "FFF");
        Fixture("guards n+", ".item:nth-child(n+){opacity:.8;}", 0, "FFF");
        Fixture("guards nn", ".item:nth-child(nn){opacity:.8;}", 0, "FFF");
        Fixture("guards 2nx", ".item:nth-child(2nx){opacity:.8;}", 0, "FFF");
        Fixture("guards 2n-1x", ".item:nth-child(2n-1x){opacity:.8;}", 0, "FFF");
        Fixture("guards 2x", ".item:nth-child(2x){opacity:.8;}", 0, "FFF");
        Fixture("guards 2.0n", ".item:nth-child(2.0n){opacity:.8;}", 0, "FFF");
        Fixture("guards 2e0n", ".item:nth-child(2e0n){opacity:.8;}", 0, "FFF");
        Fixture("guards o dd", ".item:nth-child(o dd){opacity:.8;}", 0, "FFF");
        Fixture("guards ", ".item:nth-child(){opacity:.8;}", 0, "FFF");
        Fixture("guards  \t\r\n\f", ".item:nth-child( \t\r\n\f){opacity:.8;}", 0, "FFF");
        Fixture("guards 2n\v+1", ".item:nth-child(2n\v+1){opacity:.8;}", 0, "FFF");
        Fixture("guards \v2n+1\v", ".item:nth-child(\v2n+1\v){opacity:.8;}", 0, "FFF");
        Fixture("guards 2147483647", ".item:nth-child(2147483647){opacity:.8;}", 1, "FFF");
        Fixture("guards -2147483648", ".item:nth-child(-2147483648){opacity:.8;}", 1, "FFF");
        Fixture("guards 2147483647n", ".item:nth-child(2147483647n){opacity:.8;}", 1, "FFF");
        Fixture("guards -2147483648n", ".item:nth-child(-2147483648n){opacity:.8;}", 1, "FFF");
        Fixture("guards 2147483647n+1", ".item:nth-child(2147483647n+1){opacity:.8;}", 1, "TFF");
        Fixture("guards -2147483648n+1", ".item:nth-child(-2147483648n+1){opacity:.8;}", 1, "TFF");
        Fixture("guards n-2147483648", ".item:nth-child(n-2147483648){opacity:.8;}", 1, "TTT");
        Fixture("guards -n+2147483647", ".item:nth-child(-n+2147483647){opacity:.8;}", 1, "TTT");
        Fixture("guards -1n+2147483647", ".item:nth-child(-1n+2147483647){opacity:.8;}", 1, "TTT");
        Fixture("guards 0n-2147483648", ".item:nth-child(0n-2147483648){opacity:.8;}", 1, "FFF");
        Fixture("guards 2147483648", ".item:nth-child(2147483648){opacity:.8;}", 0, "FFF");
        Fixture("guards -2147483649", ".item:nth-child(-2147483649){opacity:.8;}", 0, "FFF");
        Fixture("guards 2147483648n", ".item:nth-child(2147483648n){opacity:.8;}", 0, "FFF");
        Fixture("guards -2147483649n", ".item:nth-child(-2147483649n){opacity:.8;}", 0, "FFF");
        Fixture("guards n+2147483648", ".item:nth-child(n+2147483648){opacity:.8;}", 0, "FFF");
        Fixture("guards n-2147483649", ".item:nth-child(n-2147483649){opacity:.8;}", 0, "FFF");
        Fixture("guards 9999999999999999999999999999999999999999", ".item:nth-child(9999999999999999999999999999999999999999){opacity:.8;}", 0, "FFF");
        Fixture("guards .item:is(:nth-child(2n+1))", ".item:is(:nth-child(2n+1)){opacity:.8;}", 1, "TFT");
        Fixture("guards .item:where(:nth-child(2n+1))", ".item:where(:nth-child(2n+1)){opacity:.8;}", 1, "TFT");
        Fixture("guards .item:not(:nth-child(2n+1))", ".item:not(:nth-child(2n+1)){opacity:.8;}", 1, "FTF");
        Fixture("guards .item:nth-child(2n+1 of .item)", ".item:nth-child(2n+1 of .item){opacity:.8;}", 0, "FFF");
        Fixture("guards .item:nth-last-child(2n+1)", ".item:nth-last-child(2n+1){opacity:.8;}", 0, "FFF");
        Fixture("guards .item:nth-of-type(2n+1)", ".item:nth-of-type(2n+1){opacity:.8;}", 0, "FFF");
        Fixture("guards .item:nth-last-of-type(2n+1)", ".item:nth-last-of-type(2n+1){opacity:.8;}", 0, "FFF");
        Fixture("guards .item:nth-child(n+-1),#b", ".item:nth-child(n+-1),#b{opacity:.8;}", 1, "FTF");
        Fixture("guards .item:is(:nth-child(n+-1),#b)", ".item:is(:nth-child(n+-1),#b){opacity:.8;}", 1, "FTF");
        Fixture("guards .item:is(:nth-child(n+-1))", ".item:is(:nth-child(n+-1)){opacity:.8;}", 1, "FFF");
        Fixture("has parent", "#parent:has(.item:nth-child(2n+1)){opacity:.8;}", 1, "FFF", parentExpected: .8f);
        Fixture("cascade specificity", ".item:nth-child(2n+1){opacity:.8;}.item{opacity:.4;}", 2, "TFT", .8f, .4f);
        Fixture("cascade order", ".item:nth-child(2n+1){opacity:.8;}.item{opacity:.4;}.item:nth-child(2n+1){opacity:.6;}", 3, "TFT", .6f, .4f);
        Fixture("where zero specificity", ".item:where(:nth-child(2n+1)){opacity:.8;}.item{opacity:.4;}", 2, "FFF", .8f, .4f);
    }
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_style_runtime_add_stylesheet(IntPtr runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string css, int layer, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out long sheet, out int rules);
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_style_runtime_remove_stylesheet(IntPtr runtime, long sheet);
    private static void Fixture(string name, string css, int expectedRules, string mask, float selected = .8f, float other = .2f, float parentExpected = 1f)
    {
        const string markup = "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item'></div><div id='b' class='item'></div><div id='c' class='item'></div></div></body></html>";
        using var document = NativeDomDocument.Parse(markup);
        using var runtime = new StyleRuntimeInterop(800, 600, 1);
        var handle = (IntPtr)(typeof(StyleRuntimeInterop).GetField("_runtime", BindingFlags.NonPublic | BindingFlags.Instance)?.GetValue(runtime) ?? throw new InvalidOperationException("runtime missing"));
        if (aether_style_runtime_add_stylesheet(handle, css, 2, "nth-count", out var sheet, out var rules) != 0 || aether_style_runtime_remove_stylesheet(handle, sheet) == 0)
            throw new InvalidOperationException(name + ": rule count fixture failed");
        if (rules != expectedRules) throw new InvalidOperationException($"{name}: rules {rules}, expected {expectedRules}");
        runtime.Load(document, "", ".item{opacity:.2;}" + css);
        var elements = document.EnumerateElementHandles().ToArray();
        var resolution = runtime.Resolve(new int[elements.Length], 800, 600, 1);
        var fresh = document.EnumerateElementHandles().ToArray();
        if (fresh.Length != elements.Length || resolution.Styles.Count != elements.Length) throw new InvalidOperationException(name + ": style count mismatch");
        for (int j = 0; j < 4; j++)
        {
            var id = j == 3 ? "parent" : new[] { "a", "b", "c" }[j];
            var index = Array.FindIndex(fresh, node => document.GetAttributes(node).Any(a => a.Key == "id" && a.Value == id));
            if (index < 0) throw new InvalidOperationException(name + ": missing " + id);
            var expected = j == 3 ? parentExpected : mask[j] == 'T' ? selected : other;
            var actual = resolution.Styles[index].Opacity;
            if (Math.Abs(actual - expected) >= .0001) throw new InvalidOperationException($"{name}: {id} opacity {actual}, expected {expected}");
        }
    }
}
