using AetherUI.Core.Native;
using System.Reflection;
using System.Runtime.InteropServices;

namespace AetherUI.Tests;

internal static class HasRelativeAnchorTests
{
    private const string BaseMarkup = "<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>";
    private const string PlusMarkup = "<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid' id='mid' data-k='x'><i class='leaf'></i></div></div></section></body></html>";
    private static int _assertions, _fixtures, _resolutions;
    private static void Check(bool ok, string message)
    {
        _assertions++;
        if (!ok) throw new InvalidOperationException(message);
    }
    internal static void Run()
    {
        _assertions = _fixtures = _resolutions = 0;
        Fixture("G01","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid' id='mid' data-k='x'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid#mid[data-k=x] > .leaf){opacity:.75}",1,0.75f);
        Fixture("G02","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.subject > .mid){opacity:.75}",1,0.25f);
        Fixture("G03","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid .leaf){opacity:.75}",1,0.75f);
        Fixture("G04","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid .missing){opacity:.75}",1,0.25f);
        Fixture("G05","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><span class='pre'></span> <i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.pre + .leaf){opacity:.75}",1,0.75f);
        Fixture("G06","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><span class='pre'></span> <i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.pre ~ .leaf){opacity:.75}",1,0.75f);
        Fixture("G07","<!DOCTYPE html><html><head></head><body><span class='pre'></span><section class='subject' id='subject'><i class='leaf' id='leaf'></i></section></body></html>","subject",".subject:has(.pre + .subject .leaf){opacity:.75}",1,0.25f);
        Fixture("G08a","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(body .leaf, .mid > .leaf){opacity:.75}",1,0.75f);
        Fixture("G08b","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid > .leaf, body .leaf){opacity:.75}",1,0.75f);
        Fixture("G09","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(body .leaf, .subject .leaf){opacity:.75}",1,0.25f);
        Fixture("G10","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid :is(body .leaf)){opacity:.75}",1,0.75f);
        Fixture("G11","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.leaf:not(body .leaf)){opacity:.75}",1,0.25f);
        Fixture("G13","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid:has(.leaf)){opacity:.75}",1,0.75f);
        Fixture("G14","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid:has(.mid .leaf)){opacity:.75}",1,0.25f);
        Fixture("G12","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:not(:has(body .leaf)){opacity:.75}",1,0.75f);
        Fixture("G15","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid:has(.leaf)):has(.leaf){opacity:.75}",1,0.75f);
        Fixture("G16a","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","leaf","body .leaf{opacity:.75}",1,0.75f);
        Fixture("G16b","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","leaf",".subject .leaf{opacity:.75}",1,0.75f);
        Fixture("G17a","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(> .leaf){opacity:.75}",0,0.25f);
        Fixture("G17b","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(+ .leaf){opacity:.75}",0,0.25f);
        Fixture("G17c","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(~ .leaf){opacity:.75}",0,0.25f);
        Fixture("G17d","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(){opacity:.75}",0,0.25f);
        Fixture("G17e","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(:unknown){opacity:.75}",0,0.25f);
        Fixture("G17f","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(> .leaf, .leaf){opacity:.75}",1,0.75f);
        Fixture("G17g","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(:unknown, .leaf){opacity:.75}",1,0.75f);
        Fixture("G17h","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(:unknown, body .leaf){opacity:.75}",1,0.25f);
        Fixture("G17i","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",":-internal-relative-anchor{opacity:.75}",0,0.25f);
        Fixture("G18a","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject pick' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.leaf){opacity:.75}.subject.pick{opacity:.4}",2,0.4f);
        Fixture("G18b","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject pick' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject.pick{opacity:.4}.subject:has(.leaf){opacity:.75}",2,0.75f);
        Fixture("G18c","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject pick' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(:where(.leaf)){opacity:.75}.subject{opacity:.4}",2,0.4f);
        Fixture("G18d","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject pick' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.leaf,#never){opacity:.75}.subject.pick{opacity:.4}",2,0.75f);
        Attributes(false);
        Attributes(true);
        Reparent();
        State();
        Console.WriteLine($"HAS FAMILIES=21 STATIC=31 DYNAMIC=4 FIXTURES={_fixtures} RESOLUTIONS={_resolutions} ASSERTIONS={_assertions}");
    }
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_style_runtime_add_stylesheet(IntPtr runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string css, int layer, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out long sheet, out int rules);
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_style_runtime_remove_stylesheet(IntPtr runtime, long sheet);
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern int aether_node_append_child(IntPtr parent, IntPtr child);
    [DllImport("aether_core", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr aether_node_get_parent(IntPtr node);

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
                Runtime.Load(Document, "", ".subject,.leaf{opacity:.25;width:10px;height:11px}" + css);
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
            ExpectStyle(result.Styles[Array.IndexOf(_original, Find(name))], expected, name);
        public void Dispose() { Runtime.Dispose(); Document.Dispose(); }
    }
    private static void ExpectStyle(NativeComputedStyle style, float expected, string name)
    {
        Check(Math.Abs(style.Opacity - expected) < .0001, $"{name}: opacity {style.Opacity}, expected {expected}");
        Check(style.Width.Value == 10 && style.Height.Value == 11, name + ": dimensions 10x11");
    }
    private static void Fixture(string name, string markup, string target, string css, int rules, float expected)
    {
        using var f = new Session(markup, css, rules);
        f.Expect(f.Resolve(), target, expected);
        Console.WriteLine("HAS PASS " + name);
    }
    private static void Attributes(bool functional)
    {
        using var f = new Session(PlusMarkup, functional ? ".subject:has(:is([data-k=yes] .leaf)){opacity:.75}" : ".subject:has([data-k=yes] .leaf){opacity:.75}", 1);
        var node = functional ? f.Document.Body : f.Find("mid");
        f.Document.SetAttribute(node, "data-k", "x");
        f.Expect(f.Resolve(), "subject", .25f);
        f.Document.SetAttribute(node, "data-k", "yes");
        f.Expect(f.Resolve(), "subject", .75f);
        f.Document.SetAttribute(node, "data-k", "x");
        f.Expect(f.Resolve(), "subject", .25f);
        Console.WriteLine("HAS PASS " + (functional ? "G19b" : "G19a"));
    }
    private static void Reparent()
    {
        using var f = new Session("<!DOCTYPE html><html><head></head><body><div class='subject' id='A'><i class='leaf'></i></div><div class='subject' id='B'></div></body></html>", ".subject:has(.leaf){opacity:.75}", 1);
        var result = f.Resolve(); f.Expect(result, "A", .75f); f.Expect(result, "B", .25f);
        var leaf = f.Find("leaf"); var a = f.Find("A"); var b = f.Find("B");
        Check(aether_node_append_child(b, leaf) == 0, "move leaf to B");
        result = f.Resolve();
        Check(f.Find("leaf") == leaf && aether_node_get_parent(leaf) == b, "same leaf in B");
        f.Expect(result, "A", .25f); f.Expect(result, "B", .75f);
        Check(aether_node_append_child(a, leaf) == 0, "return leaf to A");
        result = f.Resolve();
        Check(f.Find("leaf") == leaf && aether_node_get_parent(leaf) == a, "same leaf in A");
        f.Expect(result, "A", .75f); f.Expect(result, "B", .25f);
        Console.WriteLine("HAS PASS G20");
    }
    private static void State()
    {
        using var f = new Session(BaseMarkup, ".subject:has(.leaf:hover){opacity:.75}", 1);
        f.Expect(f.Resolve(), "subject", .25f);
        foreach (var state in new[] { 1, 0 })
        {
            var leaf = f.Find("leaf");
            _resolutions++;
            var change = f.Runtime.ResolveStateChanges([leaf], [state]);
            var subject = f.Find("subject");
            var update = change.Updates.Single(u => u.Node == subject);
            ExpectStyle(update.Style, state == 1 ? .75f : .25f, "G21 subject");
            Check((uint)update.Impact == 0x0C, "subject COMPOSITE|HITTEST impact");
            Check((change.Impact & (NativeStyleImpact.Layout | NativeStyleImpact.TextLayout)) == 0, "no layout/text impact");
            Check(!change.StackingOrderChanged, "opacity leaves stacking metadata unchanged");
        }
        _resolutions++;
        var noop = f.Runtime.ResolveStateChanges([f.Find("leaf")], [0]);
        Check(noop.Updates.Count == 0 && noop.Impact == NativeStyleImpact.None && !noop.StackingOrderChanged, "same off with nonempty leaf input is no-op");
        Console.WriteLine("HAS PASS G21");
    }
}
