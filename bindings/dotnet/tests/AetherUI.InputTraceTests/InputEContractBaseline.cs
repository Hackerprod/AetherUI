using AetherUI.Css;
using AetherUI.Layout;
using AetherUI.Runtime;

// Explicit historical observations, not the proposed phase-E behavior. Keep this
// mode separate from future optimized-input gates to make contract changes visible.
internal static class InputEContractBaseline
{
    public static void Run()
    {
        foreach (var counters in new[] { false, true })
        {
            var component = new ContractComponent();
            using var renderer = new NullRenderer();
            using var host = new UiDocumentHost(component, new ApproximateTextMeasurer(), renderer,
                new UiHostOptions { Width = 200, Height = 200, EnableInputWorkCounters = counters });
            host.Render();
            Eq(CursorKind.Text, host.CursorAt(new(20, 100)));
            Eq(CursorKind.Pointer, host.PointerMoved(new(20, 20)));
            Eq(CursorKind.Pointer, host.CursorAt(new(20, 100)));
            Console.WriteLine($"BASELINE counters={counters}: pending A cursor query at B returns Pointer; B independently returns Text");
            host.PointerPressed(new(20, 20));
            host.PointerMoved(new(20, 100));
            host.PointerReleased(new(20, 20));
            Eq("a:mousedown,a:mouseup,a:click", string.Join(",", component.Trace));
            host.Render();
            Eq("a:mousedown,a:mouseup,a:click,b:mouseover", string.Join(",", component.Trace));
            Eq(CursorKind.Text, host.CursorAt(new(20, 100)));
            Console.WriteLine($"BASELINE counters={counters}: {string.Join(",", component.Trace)}");
        }
        Console.WriteLine("PASS 2 historical E contract observations x counters OFF/ON; no contract correction enabled");
    }

    private static void Eq<T>(T expected, T actual)
    {
        if (!Equals(expected, actual))
            throw new InvalidOperationException($"Expected [{expected}] got [{actual}]");
    }

    private sealed class ContractComponent : UiComponent
    {
        public List<string> Trace { get; } = [];
        protected override void BuildRenderTree(MarkupWriter w)
        {
            w.WriteLiteral("<style>body{margin:0}#a,#b{width:80px;height:80px}#a{cursor:pointer}#b{cursor:text}</style><body>");
            foreach (var id in new[] { "a", "b" })
            {
                w.WriteLiteral($"<div id='{id}'");
                foreach (var type in new[] { "mouseover", "mouseout", "mousedown", "mouseup", "click" })
                    w.WriteEventAttribute("on" + type, e => Trace.Add(id + ":" + type));
                w.WriteLiteral("></div>");
            }
            w.WriteLiteral("</body>");
        }
    }
}
