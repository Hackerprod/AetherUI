using AetherUI.Core.Native;
using System.Reflection;
using System.Runtime.InteropServices;
namespace AetherUI.Tests;
internal static class NotStrictListTests
{
    private static int _assertions, _fixtures, _resolutions;
    private static void Check(bool ok, string message) { _assertions++; if (!ok) throw new InvalidOperationException(message); }
    internal static void Run()
    {
        _assertions = _fixtures = _resolutions = 0;
        Fixture("valid-single", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip){opacity:.8}", 1, [0.8f, 0.2f, 0.8f]);
        Fixture("valid-list", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,.omit){opacity:.8}", 1, [0.8f, 0.2f, 0.2f]);
        Fixture("unknown-last", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,:aether-unknown){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("unknown-first", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(:aether-unknown,.skip){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("bad-nth", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,:nth-child(bogus)){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("empty-last", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("empty-first", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(,.skip){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("empty-middle", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,,.omit){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("dangling-combinator", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,.omit >){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("is-forgiving", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:is(.keep,:aether-unknown){opacity:.8}", 1, [0.8f, 0.2f, 0.2f]);
        Fixture("where-forgiving", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:where(.keep,:aether-unknown){opacity:.8}", 1, [0.8f, 0.2f, 0.2f]);
        Fixture("valid-not-containing-forgiving-is", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(:is(.skip,:aether-unknown)){opacity:.8}", 1, [0.8f, 0.2f, 0.8f]);
        Fixture("invalid-not-inside-forgiving-is", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:is(:not(.skip,:aether-unknown),.keep){opacity:.8}", 1, [0.8f, 0.2f, 0.2f]);
        Fixture("D01", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:where(:not(.skip,:unknown),.keep){opacity:.8}", 1, [0.8f, 0.2f, 0.2f]);
        Fixture("D02", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:is(.keep,:not(:unknown,.skip)){opacity:.8}", 1, [0.8f, 0.2f, 0.2f]);
        Fixture("D03", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(:not(.skip,:unknown)){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("D04", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(:where(.skip,:unknown)){opacity:.8}", 1, [0.8f, 0.2f, 0.8f]);
        Fixture("D05", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,:is(.omit,:unknown)){opacity:.8}", 1, [0.8f, 0.2f, 0.2f]);
        Fixture("D06", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,   ){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("D07", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(){opacity:.8}", 0, [0.2f, 0.2f, 0.2f]);
        Fixture("D08", "<!doctype html><html><head></head><body><div id='parent'><div id='a' data-k='a,b' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not([data-k=\"a,b\"],.skip){opacity:.8}", 1, [0.2f, 0.2f, 0.8f]);
        Fixture("D09", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip),.item:not(.omit){opacity:.8}", 2, [0.8f, 0.8f, 0.8f]);
        Fixture("D10", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,:unknown),#b{opacity:.8}", 1, [0.2f, 0.8f, 0.2f]);
        Fixture("D11", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:is(#never:hover:not(.skip,:unknown),.keep){opacity:.8}.item.keep{opacity:.4}", 2, [0.4f, 0.2f, 0.2f]);
        Fixture("D12", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(#never,.skip){opacity:.8}.item.keep{opacity:.4}", 2, [0.8f, 0.2f, 0.8f]);
        Fixture("D13", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:where(:not(#never,.skip)){opacity:.8}.item.keep{opacity:.4}", 2, [0.4f, 0.2f, 0.8f]);
        Fixture("D14", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(:hover){opacity:.8}", 1, [0.8f, 0.8f, 0.8f]);
        Fixture("D15", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:is(:not(.skip,:unknown)){opacity:.8}", 1, [0.2f, 0.2f, 0.2f]);
        Fixture("D16", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(:is(:not(.skip,:unknown))){opacity:.8}", 1, [0.8f, 0.8f, 0.8f]);
        Fixture("continuation", "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>", ".item:not(.skip,:unknown){opacity:.8}#b{opacity:.6}", 1, [0.2f, 0.6f, 0.2f]);
        Console.WriteLine($"NOT CASES=30 FIXTURES={_fixtures} RESOLUTIONS={_resolutions} ASSERTIONS={_assertions}");
    }
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_style_runtime_add_stylesheet(IntPtr runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string css, int layer, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out long sheet, out int rules);
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_style_runtime_remove_stylesheet(IntPtr runtime, long sheet);
    private sealed class Session : IDisposable
    {
        internal readonly NativeDomDocument Document;
        internal readonly StyleRuntimeInterop Runtime;
        // Resolve.Styles always uses this original Load order, including after reparenting.
        private readonly IntPtr[] _original;
        internal Session(string markup, string css, int expectedRules)
        {
            _fixtures++;
            Document = NativeDomDocument.Parse(markup);
            Runtime = new StyleRuntimeInterop(800, 600, 1);
            try
            {
                var handle = (IntPtr)(typeof(StyleRuntimeInterop).GetField("_runtime", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(Runtime)!);
                Check(aether_style_runtime_add_stylesheet(handle, css, 2, "guard-count", out var sheet, out var rules) == 0, "test stylesheet status");
                Check(rules == expectedRules, "test sheet rule count (base/UA excluded)");
                Check(aether_style_runtime_remove_stylesheet(handle, sheet) != 0, "remove count-only sheet");
                Runtime.Load(Document, "", ".item{opacity:.2}" + css);
                _original = Document.EnumerateElementHandles().ToArray();
            }
            catch { Runtime.Dispose(); Document.Dispose(); throw; }
        }
        internal IntPtr Find(string name) => Document.EnumerateElementHandles().Single(node =>
            Document.GetAttributes(node).Any(a => a.Key == "id" && a.Value == name || a.Key == "class" && a.Value.Split(' ').Contains(name)));
        internal NativeStyleResolution Resolve()
        {
            _resolutions++;
            var result = Runtime.Resolve(new int[_original.Length], 800, 600, 1);
            Check(result.Styles.Count == _original.Length, "resolved style count");
            Check(Document.EnumerateElementHandles().ToHashSet().SetEquals(_original), "fresh DOM retains node identities");
            return result;
        }
        internal void Expect(NativeStyleResolution result, string name, float expected) =>
            Check(Math.Abs(result.Styles[Array.IndexOf(_original, Find(name))].Opacity - expected) < .0001, name + ": computed opacity");
        public void Dispose() { Runtime.Dispose(); Document.Dispose(); }
    }

    private static void Fixture(string name, string markup, string css, int rules, float[] expected)
    {
        using var f = new Session(markup, css, rules);
        var result = f.Resolve();
        for (int i = 0; i < 3; i++) f.Expect(result, new[] { "a", "b", "c" }[i], expected[i]);
        if (name == "D14")
        {
            foreach (int state in new[] { 1, 0 })
            {
                _resolutions++;
                var change = f.Runtime.ResolveStateChanges([f.Find("b")], [state]);
                var b = f.Find("b");
                Check(change.Updates.Count > 0, "state change produces updates");
                Check(Math.Abs(change.Updates.Single(u => u.Node == b).Style.Opacity - (state == 1 ? .2f : .8f)) < .0001, "D14 state opacity");
                foreach (string id in new[] { "a", "c" })
                {
                    var node = f.Find(id);
                    foreach (var update in change.Updates.Where(u => u.Node == node)) Check(Math.Abs(update.Style.Opacity - .8f) < .0001, "D14 unaffected opacity");
                }
            }
        }
        Console.WriteLine($"NOT PASS {name} rules={rules} opacity=[{string.Join(",", expected)}]");
    }
}
