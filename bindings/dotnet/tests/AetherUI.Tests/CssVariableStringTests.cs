using AetherUI.Core.Native;
using System.Reflection;
using System.Runtime.InteropServices;

namespace AetherUI.Tests;

internal static class CssVariableStringTests
{
    internal static void Run()
    {
        var failures = RunCorpus(true, (group, name, pass, actual, expected) =>
            Console.WriteLine($"{group} {name}: {(pass ? "PASS" : "FAIL")} expected={expected} actual={actual}"));
        if (failures != 0) throw new InvalidOperationException($"CSS variable strings: {failures} failures");
    }

    // Also compiled unchanged into the isolated old/new comparison harness.
    internal static int RunCorpus(bool runExhaustion, Action<string, string, bool, string, string> record)
    {
        int failures = 0;
        void Test(string group, string name, Action test)
        {
            try { test(); record(group, name, true, "matched", "matched"); }
            catch (Exception error) { failures++; record(group, name, false, error.Message, "matched"); }
        }
        void Family(string group, string name, string declaration, string expected, bool varError = false, bool inline = false)
            => Test(group, name, () => Fixture(declaration, (style, text, codes) =>
            {
                Equal(style.FontFamily, expected);
                Equal(codes.Contains("AETHER-CSS-VAR"), varError);
            }, inline));

        Family("measured5", "literal", "font-family:'plain-family'", "plain-family");
        Family("measured5", "missing", "font-family:'var(--missing)'", "var(--missing)");
        Family("measured5", "defined", "font-family:'var(--family)'", "var(--family)");
        Family("measured5", "real", "font-family:var(--family)", "resolved-family");
        Family("measured5", "fallback", "font-family:var(--missing,fallback-family)", "fallback-family");
        Family("guards", "G3 quoted self", "--q:'var(--q)';font-family:var(--q)", "var(--q)");
        Family("guards", "G4 quoted fallback", "font-family:var(--missing,'var(--family)')", "var(--family)");
        Family("guards", "G4 real fallback", "font-family:var(--missing,var(--family))", "resolved-family");
        Family("guards", "G5 list", "font-family:'var(--missing)',var(--family)", "var(--missing)");
        Family("guards", "G5 string comment", "font-family:'/*var(--missing)*/'", "/*var(--missing)*/");
        Family("guards", "G5 comment", "font-family:/*var(--missing) ) , */var(--family)", "resolved-family");
        Family("guards", "G6 inline", "font-family:'var(--missing)'", "var(--missing)", inline: true);
        Test("guards", "G7 generated text", () => Fixture("--q:'var(--family)'", (style, text, codes) =>
        {
            Equal(text, "var(--missing)var(--family)");
            Equal(codes.Contains("AETHER-CSS-VAR"), false);
        }, extra: "#target::before{content:'var(--missing)'}#target::after{content:var(--q)}"));
        Test("guards", "G8 URL", () => Fixture("background:url(\"var(--missing)\")", (style, text, codes) =>
        {
            Equal(style.BackgroundLayers.Count, 1);
            Equal(style.BackgroundLayers[0].Kind, 1);
            Equal(style.BackgroundLayers[0].ImageSource, "var(--missing)");
        }));
        Test("guards", "G8 gradient", () => Fixture("--bg:linear-gradient(90deg,#000,#fff);background:var(--bg)", (style, text, codes) =>
        {
            Equal(style.BackgroundLayers.Count, 1);
            Equal(style.BackgroundLayers[0].Kind, 0);
            Equal(style.BackgroundLayers[0].Gradient?.Stops.Count, 2);
        }));
        Family("guards", "G9 self cycle", "--a:var(--a,inner-family);font-family:var(--a,outer-family)", "outer-family");
        Family("guards", "G9 indirect cycle", "--a:var(--b);--b:var(--a);font-family:var(--a,outer-family)", "outer-family");
        Family("guards", "G9 exterior recovery", "--bad:var(--bad);--ok:var(--bad,recovered-family);font-family:var(--ok,outer-family)", "recovered-family");
        Family("guards", "G10 empty fallback", "font-family:var(--missing,)fallback-family", "fallback-family");
        Family("guards", "G10 missing", "font-family:var(--missing)", "parent-family", true);
        Family("guards", "G12 empty N2", EmptyChain(2) + "font-family:var(--a2)small-family", "small-family");
        if (runExhaustion)
            Family("guards", "G12 exhaustion N20", EmptyChain(20) + "font-family:var(--a20,outer-family)", "parent-family", true);
        else record("skip", "G12 exhaustion N20", true, "disabled on old DLL for safety", "disabled on old DLL for safety");
        return failures;
    }
    private static string EmptyChain(int count)
    {
        var css = "--a0:var(--missing,);";
        for (int i = 1; i <= count; i++) css += $"--a{i}:var(--a{i - 1})var(--a{i - 1});";
        return css;
    }
    private static void Fixture(string declaration, Action<NativeComputedStyle, string?, List<string>> check, bool inline = false, string extra = "")
    {
        var markup = "<!doctype html><html><body><div id='target'" +
            (inline ? " style=\"" + declaration + "\"" : "") + "></div></body></html>";
        using var document = NativeDomDocument.Parse(markup);
        using var runtime = new StyleRuntimeInterop(800, 600, 1);
        using var diagnostics = new Diagnostics(runtime);
        runtime.Load(document, "", "body{font-family:'parent-family';--family:resolved-family}#target{" +
            (inline ? "" : declaration) + "}" + extra);
        var before = document.EnumerateElementHandles().ToArray();
        var styleIndex = Array.FindIndex(before, e => document.GetAttributes(e).Any(a => a.Key == "id" && a.Value == "target"));
        if (styleIndex < 0) throw new InvalidOperationException("target missing before resolve");
        var resolution = runtime.Resolve(new int[before.Length], 800, 600, 1);
        // Resolve may create pseudos: reacquire all handles before text/style access.
        var after = document.EnumerateElementHandles().ToArray();
        var target = Array.FindIndex(after, e => document.GetAttributes(e).Any(a => a.Key == "id" && a.Value == "target"));
        if (target < 0) throw new InvalidOperationException("target missing after resolve");
        // The style list uses original element order; handles/text are reacquired after pseudos.
        if (resolution.Styles.Count != before.Length) throw new InvalidOperationException("style/element count mismatch");
        Equal(document.GetTagName(after[target]), "div");
        string text = ReadDescendantText(document, after[target]);
        if (extra.Length != 0)
        {
            // Public DOM children hide generated pseudos. Refresh them and take their
            // handles from ordered real get_update/ReadStyle records, without layout.
            var changes = runtime.ResolveStateChanges([after[target]], [0]);
            var refreshed = document.EnumerateElementHandles().ToArray();
            var host = refreshed.First(e => document.GetAttributes(e).Any(a => a.Key == "id" && a.Value == "target"));
            text = string.Concat(changes.Updates.Where(u => u.Node != host && Parent(u.Node) == host)
                .Select(u => ReadDescendantText(document, u.Node)));
        }
        check(resolution.Styles[styleIndex], text, diagnostics.Codes);
        if (diagnostics.CallbackError != null) throw new InvalidOperationException(diagnostics.CallbackError);
    }
    [DllImport("aether_core", EntryPoint = "aether_node_get_parent", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr Parent(IntPtr node);
    private static string ReadDescendantText(NativeDomDocument document, IntPtr host)
    {
        var text = new System.Text.StringBuilder();
        var pending = new Stack<IntPtr>();
        pending.Push(host);
        while (pending.Count > 0)
        {
            var node = pending.Pop();
            text.Append(document.GetTextContent(node));
            for (int i = document.GetChildCount(node); i > 0; i--)
                pending.Push(document.GetChild(node, i - 1));
        }
        return text.ToString();
    }
    private static void Equal<T>(T actual, T expected)
    {
        if (!EqualityComparer<T>.Default.Equals(actual, expected))
            throw new InvalidOperationException($"expected {expected}; actual {actual}");
    }
    private sealed class Diagnostics : IDisposable
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void Callback(IntPtr code, IntPtr property, IntPtr value, IntPtr message, IntPtr user);
        [DllImport("aether_core", EntryPoint = "aether_style_runtime_set_diagnostics_callback", CallingConvention = CallingConvention.Cdecl)]
        private static extern void Set(IntPtr runtime, Callback? callback, IntPtr user);
        private readonly IntPtr handle;
        private readonly Callback callback;
        internal readonly List<string> Codes = [];
        internal string? CallbackError;
        internal Diagnostics(StyleRuntimeInterop runtime)
        {
            handle = (IntPtr)(typeof(StyleRuntimeInterop).GetField("_runtime", BindingFlags.Instance | BindingFlags.NonPublic)?.GetValue(runtime)
                ?? throw new InvalidOperationException("runtime handle missing"));
            callback = (code, property, value, message, user) =>
            {
                try { Codes.Add(Marshal.PtrToStringUTF8(code) ?? ""); }
                catch (Exception error) { CallbackError = error.Message; }
            };
            Set(handle, callback, IntPtr.Zero);
        }
        public void Dispose() { Set(handle, null, IntPtr.Zero); GC.KeepAlive(callback); }
    }
}
