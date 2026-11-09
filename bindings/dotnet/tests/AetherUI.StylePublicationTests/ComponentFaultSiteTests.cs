#if AETHER_DOM_FAULT_TESTING
using System.Runtime.CompilerServices;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Runtime;

internal static class ComponentFaultSiteTests
{
    internal static void Run()
    {
        var rows = new List<string> { "family\tsite\toccurrence\tboundary\treached" };
        var recorded = Case(null, 0);
        Collect();
        Check(!recorded.RetiredHandler.IsAlive, "Recording fixture retained its handler target.");
        var occurrences = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var site in recorded.Trace)
        {
            occurrences.TryGetValue(site, out var count);
            occurrences[site] = ++count;
            var result = Case(site, count);
            Collect();
            Check(!result.RetiredHandler.IsAlive, $"Aborted handler target retained at {site}/{count}.");
            rows.Add($"component\t{site}\t{count}\t13.3\ttrue");
        }
        ReentrantPublicationSurvivesOuterFailure();
        // Retained writer, Build/formatter throws, StateHasChanged and Dispose reentrancy,
        // supersession, overflow and cross-thread cases remain separate semantic tests.
        ComponentRenderCandidateTests.Run();
        if (Environment.GetEnvironmentVariable("AETHER_COMPONENT_FAULT_TRACE") is { Length: > 0 } path)
            File.WriteAllLines(path, rows);
        Console.WriteLine($"PASS component faults: {recorded.Trace.Length} reached occurrences / {occurrences.Count} source operations; fresh fixture each; not CLR/BCL allocator replacement");
    }

    private sealed record Result(string[] Trace, WeakReference RetiredHandler);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static Result Case(string? site, int occurrence)
    {
        using var component = new Component();
        component.RenderMarkup();
        using var old = component.PrepareRenderCandidate();
        var pin = old.Registry;
        Check(component.TryGetEventHandler("aether:0", out var a), "Missing A legacy handler.");
        var oldMarkup = old.Markup;
        component.Version = "B";
        component.Writer = null;
        var retired = new WeakReference(component.Target);
        var session = site is null ? ManagedDomFault.Arm() : ManagedDomFault.ArmSite(site, occurrence);
        Exception? error = null;
        string? markup = null;
        try { markup = component.RenderMarkup(); }
        catch (Exception thrown) { error = thrown; }
        finally { session.Dispose(); }
        var trace = session.Sites.Take(session.Count).ToArray();
        if (site is null)
        {
            if (error is not null) throw new Exception("Recording failed", error);
            Check(markup!.Contains("aether:0", StringComparison.Ordinal), "B did not reuse token zero.");
        }
        else
        {
            Check(session.Fired && ReferenceEquals(error, session.Failure), $"Fault {site}/{occurrence} was not its requested sentinel: {error}");
            Check(markup is null && old.Markup == oldMarkup, "Failure returned or changed markup A.");
            Check(component.TryGetEventHandler("aether:0", out var current) && ReferenceEquals(a, current), "Failure replaced legacy A.");
            Check(pin.TryGetEventHandler("aether:0", out var pinned) && ReferenceEquals(a, pinned), "Historical registry A changed.");
            Deliver(current!); Deliver(pinned!);
            Check(component.Trace.SequenceEqual(new[] { "A", "A" }), "Failed B rerouted reused token.");
            Check(!component.IsRenderCandidateCurrent(old), "New attempt did not supersede old candidate authority.");
            if (component.Writer is { } writer)
            {
                Throws<InvalidOperationException>(() => writer.WriteLiteral("late"));
                Throws<InvalidOperationException>(() => writer.WriteEventAttribute("onclick", () => { }));
            }
            // Explicit new attempt after disarming; never an automatic retry from failure.
            var successful = component.RenderMarkup();
            Check(successful.Contains("aether:0", StringComparison.Ordinal), "Explicit retry failed.");
        }
        Check(component.TryGetEventHandler("aether:0", out var b), "B registry missing after success/retry.");
        Deliver(b);
        Check(component.Trace.Last() == "B", "B did not publish the new handler.");
        Check(pin.TryGetEventHandler("aether:0", out var retainedA) && ReferenceEquals(a, retainedA), "Publishing B invalidated a retained A registry.");
        return new(trace, retired);
    }

    private static void ReentrantPublicationSurvivesOuterFailure()
    {
        using var component = new ReentrantComponent();
        component.RenderMarkup();
        using var old = component.PrepareRenderCandidate();
        var pin = old.Registry;
        component.Outer = true;
        using var session = ManagedDomFault.ArmSite("component.candidate", 2);
        Exception? error = null;
        try { component.RenderMarkup(); }
        catch (Exception thrown) { error = thrown; }
        session.Dispose();
        Check(session.Fired && ReferenceEquals(error, session.Failure), "Outer allocation failure was not reached after nested C publication.");
        Check(component.TryGetEventHandler("aether:0", out var newest), "Reentrant C disappeared.");
        Deliver(newest);
        Check(pin.TryGetEventHandler("aether:0", out var oldest), "Historical A disappeared.");
        Deliver(oldest);
        Check(component.Trace.SequenceEqual(new[] { "C", "A" }), "Failure restored A over a newer reentrant publication.");
        Check(component.Effects == 1, "Outer user effect rolled back or replayed.");
    }

    private static void Deliver(UiEventHandler handler) => handler(new UiEvent("click", new ElementNode("button")));
    private static void Collect() { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); }
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static void Throws<T>(Action action) where T : Exception
    { try { action(); } catch (T) { return; } throw new Exception($"Expected {typeof(T).Name}"); }

    private sealed class HandlerTarget(List<string> trace)
    {
        internal void A(UiEvent _) => trace.Add("A");
        internal void B(UiEvent _) => trace.Add("B");
        internal void Action() => trace.Add("action");
    }
    private sealed class Component : UiComponent
    {
        internal readonly List<string> Trace = [];
        internal readonly HandlerTarget Target;
        internal string Version = "A";
        internal MarkupWriter? Writer;
        internal Component() => Target = new(Trace);
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            Writer = writer;
            writer.WriteLiteral("<button");
            // Cache the A delegate so historical candidate and legacy registry are identical.
            writer.WriteEventAttribute("onclick", Version == "A" ? AHandler : Target.B);
            if (Version == "B")
            {
                for (var i = 0; i < 12; i++) writer.WriteEventAttribute("onextra" + i, Target.B);
                writer.WriteEventAttribute("onaction", Target.Action);
                writer.WriteAttribute("title", new Formatted());
                writer.WriteAttribute("disabled", true);
                writer.WriteLiteral(">");
                writer.Write(new Formatted());
                writer.Write(new Plain());
                writer.WriteLiteral(new string('x', 4096));
            }
            writer.WriteLiteral("</button>");
        }
        private UiEventHandler? _a;
        private UiEventHandler AHandler => _a ??= Target.A;
    }
    private sealed class Formatted : IFormattable
    { public string ToString(string? format, IFormatProvider? provider) => "<encoded & value>"; }
    private sealed class Plain
    { public override string ToString() => "<plain & value>"; }
    private sealed class ReentrantComponent : UiComponent
    {
        internal bool Outer;
        internal int Effects;
        internal readonly List<string> Trace = [];
        private string _label = "A";
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            var label = _label;
            writer.WriteEventAttribute("onclick", _ => Trace.Add(label));
            if (!Outer) return;
            Outer = false; Effects++; _label = "C";
            RenderMarkup();
        }
    }
}
#endif
