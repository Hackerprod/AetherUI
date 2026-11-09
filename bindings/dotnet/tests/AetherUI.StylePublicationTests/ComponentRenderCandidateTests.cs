using System.Reflection;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Runtime;

// Pure managed tests. Register ComponentRenderCandidateTests.Run() in the existing runner.
internal static class ComponentRenderCandidateTests
{
    internal static void Run()
    {
        FailedBuildPreservesLegacyAndPinnedHandlers();
        FormatterFailuresSealWriterAndPreserveHandlers();
        FailedDownstreamStagesDoNotPublishHandlers();
        DirectRenderAndCandidateDisposalDoNotChangePins();
        PreparedCandidateSupersessionAndPublication();
        NestedRenderCannotPublishOuterRegistry();
        InvalidationAndDisposeDuringUserCodeInvalidateCandidate();
        DisposeOverrideCannotKeepCandidateAlive();
        ExhaustionCannotCertifyOldCandidate();
        WrongThreadCannotChangeBoundAuthority();
        ValidationAndReferencePublicationDoNotAllocate();
    }

    private static void FailedBuildPreservesLegacyAndPinnedHandlers()
    {
        using var component = new Component();
        var calls = new List<string>();
        component.Build = writer => Event(writer, () => calls.Add("A"));
        component.RenderMarkup();
        using var first = component.PrepareRenderCandidate();
        var pin = first.Registry;
        MarkupWriter? retained = null;
        component.Build = writer =>
        {
            retained = writer;
            Event(writer, () => calls.Add("B"));
            Dispatch(component, "aether:0"); // Must still resolve the complete legacy A.
            Dispatch(pin, "aether:0");
            throw new OutOfMemoryException("Build failure after token reuse");
        };
        Throws<OutOfMemoryException>(() => component.RenderMarkup());
        Dispatch(component, "aether:0");
        Dispatch(pin, "aether:0");
        Check(calls.SequenceEqual(new[] { "A", "A", "A", "A" }), "Build exposed or destroyed a registry.");
        Check(!component.IsRenderCandidateCurrent(first), "A candidate survived a newer render attempt.");
        AssertSealed(retained!);
    }

    private static void FormatterFailuresSealWriterAndPreserveHandlers()
    {
        foreach (var attribute in new[] { false, true })
        foreach (var formattable in new[] { false, true })
        {
            using var component = new Component();
            var calls = 0;
            component.Build = writer => Event(writer, () => ++calls);
            component.RenderMarkup();
            MarkupWriter? retained = null;
            var sideEffects = 0;
            component.Build = writer =>
            {
                retained = writer;
                Event(writer, () => throw new Exception("B must not be dispatched"));
                Func<string> fail = () => { ++sideEffects; throw new FormatException("user formatting"); };
                object value = formattable ? new Formattable(fail) : new Text(fail);
                if (attribute) writer.WriteAttribute("title", value);
                else writer.Write(value);
            };
            Throws<FormatException>(() => component.RenderMarkup());
            Dispatch(component, "aether:0");
            Check(calls == 1 && sideEffects == 1, "Formatting failure changed registry or rolled back user effects.");
            AssertSealed(retained!);
        }
    }

    private static void FailedDownstreamStagesDoNotPublishHandlers()
    {
        using var component = new Component();
        var calls = new List<string>();
        component.Build = writer => Event(writer, () => calls.Add("A"));
        component.RenderMarkup();
        using var first = component.PrepareRenderCandidate();
        var activeRegistry = first.Registry;
        var activeMarkup = first.Markup;
        // These are deliberately simulated failures OUTSIDE component preparation, not claims
        // that native parse, CSS or mapping were tested by this pure managed test.
        foreach (var stage in new[] { "parse", "compile", "mapping" })
        {
            component.Build = writer => Event(writer, () => calls.Add("B"));
            Throws<OutOfMemoryException>(() =>
            {
                using var next = component.PrepareRenderCandidate();
                Check(next.Markup.Contains("aether:0", StringComparison.Ordinal), "Token reuse not exercised.");
                Check(!ReferenceEquals(activeRegistry, next.Registry), "Registry staging reused A storage.");
                throw new OutOfMemoryException(stage);
            });
            Check(activeMarkup == first.Markup, "Downstream failure changed the published markup.");
            Dispatch(activeRegistry, "aether:0");
            Dispatch(component, "aether:0");
        }
        Check(calls.Count == 6 && calls.All(call => call == "A"), "Downstream failure rerouted handlers.");
    }

    private static void DirectRenderAndCandidateDisposalDoNotChangePins()
    {
        using var component = new Component();
        var calls = new List<string>();
        MarkupWriter? writerA = null;
        component.Build = writer => { writerA = writer; Event(writer, () => calls.Add("A")); };
        var first = component.PrepareRenderCandidate();
        var pinned = first.Registry;
        var retainedLossToken = "aether:0";
        first.Dispose();
        first.Dispose();
        Check(!component.IsRenderCandidateCurrent(first), "Disposed candidate remained current.");
        Throws<ObjectDisposedException>(() => { _ = first.Registry; });
        Throws<ObjectDisposedException>(() => { _ = first.Markup; });
        AssertSealed(writerA!);

        component.Build = writer =>
        {
            Event(writer, () => calls.Add("B"));
            writer.WriteEventAttribute("onblur", () => calls.Add("B-new-attribute"));
        };
        var markup = component.RenderMarkup();
        Check(markup.Contains("aether:0", StringComparison.Ordinal), "Direct render did not reuse token.");
        Dispatch(component, "aether:0");
        Dispatch(pinned, retainedLossToken); // Uses the frozen token, not a new DOM attribute.
        component.Dispose();
        Check(!component.TryGetEventHandler("aether:0", out _), "Disposed legacy registry remained active.");
        // Historical lookup is data only. A disposed HOST must not invoke this handler.
        Check(pinned.TryGetEventHandler(retainedLossToken, out _), "Component disposal destroyed a historical registry.");
        Check(calls.SequenceEqual(new[] { "B", "A" }), "Pinned registry followed a direct external render.");
    }

    private static void PreparedCandidateSupersessionAndPublication()
    {
        using var component = new Component();
        component.BindRenderOwnerThread();
        var calls = new List<string>();
        component.Build = writer => Event(writer, () => calls.Add("A"));
        using var first = component.PrepareRenderCandidate();
        var oldPin = first.Registry;
        var published = first.Registry;
        component.Build = writer => Event(writer, () => calls.Add("B"));
        using var superseded = component.PrepareRenderCandidate();
        component.Invalidate(); // User code between prepare and final host validation.
        Check(!component.IsRenderCandidateCurrent(superseded), "Post-prepare invalidation was ignored.");
        Dispatch(published, "aether:0");
        using var next = component.PrepareRenderCandidate();
        var registry = next.Registry;
        Check(component.IsRenderCandidateCurrent(next), "Fresh B candidate was rejected.");
        published = registry; // Simulated host reference swap, without user callbacks.
        next.Dispose();
        Dispatch(published, "aether:0");
        Dispatch(oldPin, "aether:0");
        Check(calls.SequenceEqual(new[] { "A", "B", "A" }), "Publication lost old or new handler identity.");
    }

    private static void NestedRenderCannotPublishOuterRegistry()
    {
        foreach (var inFormatter in new[] { false, true })
        {
            using var component = new Component();
            var calls = new List<string>();
            component.Build = writer => Event(writer, () => calls.Add("A"));
            component.RenderMarkup();
            using var first = component.PrepareRenderCandidate();
            var pin = first.Registry;
            MarkupWriter? retained = null;
            component.Build = writer =>
            {
                retained = writer;
                Event(writer, () => calls.Add("outer-B"));
                string Nested()
                {
                    Dispatch(component, "aether:0");
                    component.Build = nested => Event(nested, () => calls.Add("inner-C"));
                    component.RenderMarkup();
                    return "nested";
                }
                if (inFormatter) writer.Write(new Text(Nested));
                else Nested();
            };
            Throws<InvalidOperationException>(() => component.RenderMarkup());
            Dispatch(component, "aether:0");
            Dispatch(pin, "aether:0");
            Check(calls.SequenceEqual(new[] { "A", "inner-C", "A" }), "Outer staging overwrote reentrant publication.");
            AssertSealed(retained!);
        }
    }

    private static void InvalidationAndDisposeDuringUserCodeInvalidateCandidate()
    {
        foreach (var dispose in new[] { false, true })
        foreach (var inFormatter in new[] { false, true })
        {
            using var component = new Component();
            var calls = 0;
            var sideEffects = 0;
            component.Build = writer => Event(writer, () => ++calls);
            component.RenderMarkup();
            using var first = component.PrepareRenderCandidate();
            var pin = first.Registry;
            component.Build = writer =>
            {
                Event(writer, () => throw new Exception("Rejected candidate handler"));
                string ChangeAuthority()
                {
                    ++sideEffects;
                    if (dispose) component.Dispose();
                    else component.Invalidate();
                    return "changed";
                }
                if (inFormatter) writer.WriteAttribute("title", new Text(ChangeAuthority));
                else ChangeAuthority();
            };
            Throws<InvalidOperationException>(() => component.RenderMarkup());
            Check(!component.IsRenderCandidateCurrent(first), "Invalidation/disposal left candidate current.");
            Check(sideEffects == 1, "User effects were rolled back or replayed.");
            Dispatch(pin, "aether:0");
            if (!dispose) Dispatch(component, "aether:0");
            Check(calls == (dispose ? 1 : 2), "Invalidation/disposal damaged old pins.");
        }

        // The revision advances before a listener that throws, and its effects are retained.
        using var notifying = new Component();
        using var candidate = notifying.PrepareRenderCandidate();
        notifying.Invalidated += (_, _) => throw new FormatException("notification failure");
        Throws<FormatException>(notifying.Invalidate);
        Check(!notifying.IsRenderCandidateCurrent(candidate), "Throwing listener hid an invalidation.");
    }

    private static void DisposeOverrideCannotKeepCandidateAlive()
    {
        foreach (var throws in new[] { false, true })
        {
            var component = new Component();
            using var candidate = component.PrepareRenderCandidate();
            var overrideCalls = 0;
            component.OnDispose = () =>
            {
                ++overrideCalls;
                Check(!component.IsRenderCandidateCurrent(candidate), "Dispose override saw a live candidate.");
                Throws<ObjectDisposedException>(() => component.RenderMarkup());
                Throws<ObjectDisposedException>(component.Invalidate);
                component.Dispose(); // Must not recurse into the override.
                if (throws) throw new FormatException("dispose override");
            };
            if (throws) Throws<FormatException>(component.Dispose);
            else component.Dispose();
            Check(!component.IsRenderCandidateCurrent(candidate), "Override without base preserved authority.");
            component.Dispose();
            Check(overrideCalls == 1, "Reentrant/repeated disposal executed override again.");
        }
    }

    private static void ExhaustionCannotCertifyOldCandidate()
    {
        foreach (var generation in new[] { false, true })
        {
            using var component = new Component();
            SetField(component, generation ? "_renderGeneration" : "_invalidationRevision",
                generation ? long.MaxValue - 1 : long.MaxValue);
            using var candidate = component.PrepareRenderCandidate();
            if (generation) Throws<OverflowException>(() => component.PrepareRenderCandidate());
            else Throws<OverflowException>(component.Invalidate);
            Check(!component.IsRenderCandidateCurrent(candidate), "Exhaustion left an old certificate valid.");
            Throws<InvalidOperationException>(() => component.RenderMarkup());
        }

        using var eventComponent = new Component();
        var calls = 0;
        eventComponent.Build = writer => Event(writer, () => ++calls);
        eventComponent.RenderMarkup();
        MarkupWriter? retained = null;
        eventComponent.Build = writer =>
        {
            retained = writer;
            SetField(writer, "_nextEventId", long.MaxValue);
            Event(writer, () => throw new Exception("Overflowed handler"));
        };
        Throws<OverflowException>(() => eventComponent.RenderMarkup());
        Dispatch(eventComponent, "aether:0");
        Check(calls == 1, "Event-id exhaustion changed legacy registry.");
        AssertSealed(retained!);
    }

    private static void WrongThreadCannotChangeBoundAuthority()
    {
        using var component = new Component();
        component.BindRenderOwnerThread();
        component.BindRenderOwnerThread();
        var builds = 0;
        var notifications = 0;
        var disposalCallbacks = 0;
        component.Build = _ => ++builds;
        component.Invalidated += (_, _) => ++notifications;
        component.OnDispose = () => ++disposalCallbacks;
        using var candidate = component.PrepareRenderCandidate();
        Exception? failure = null;
        var thread = new Thread(() =>
        {
            try
            {
                Throws<InvalidOperationException>(component.BindRenderOwnerThread);
                Throws<InvalidOperationException>(() => component.PrepareRenderCandidate());
                Throws<InvalidOperationException>(() => component.RenderMarkup());
                Throws<InvalidOperationException>(component.Invalidate);
                Throws<InvalidOperationException>(component.Dispose);
                Check(!component.IsRenderCandidateCurrent(candidate), "Wrong-thread certificate accepted.");
            }
            catch (Exception error) { failure = error; }
        });
        thread.Start();
        thread.Join();
        if (failure is not null) throw new Exception("Wrong-thread checks failed.", failure);
        Check(builds == 1 && notifications == 0 && disposalCallbacks == 0, "Wrong-thread call executed user code.");
        Check(component.IsRenderCandidateCurrent(candidate), "Wrong-thread call changed authority.");
        using var other = new Component();
        Check(!other.IsRenderCandidateCurrent(candidate), "Candidate identity crossed components.");
    }

    private static void ValidationAndReferencePublicationDoNotAllocate()
    {
        using var component = new Component();
        component.BindRenderOwnerThread();
        using var candidate = component.PrepareRenderCandidate();
        var registry = candidate.Registry;
        var markup = candidate.Markup;
        UiComponent.EventRegistry? published = null;
        string? publishedMarkup = null;
        for (var i = 0; i < 100; ++i) Check(component.IsRenderCandidateCurrent(candidate), "Warmup candidate invalid.");
        var before = GC.GetAllocatedBytesForCurrentThread();
        for (var i = 0; i < 1000; ++i)
        {
            if (!component.IsRenderCandidateCurrent(candidate)) throw new Exception("Candidate invalid.");
            published = registry;
            publishedMarkup = markup;
        }
        var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        Check(allocated == 0, "Candidate validation/reference publication allocated.");
        Check(ReferenceEquals(published, registry) && ReferenceEquals(publishedMarkup, markup), "Reference publication failed.");
    }

    private static void AssertSealed(MarkupWriter writer)
    {
        Throws<InvalidOperationException>(() => writer.WriteLiteral("late"));
        Throws<InvalidOperationException>(() => writer.Write("late"));
        Throws<InvalidOperationException>(() => writer.WriteAttribute("id", "late"));
        Throws<InvalidOperationException>(() => writer.WriteEventAttribute("onclick", () => { }));
        Throws<InvalidOperationException>(() => writer.WriteEventAttribute("onclick", _ => { }));
    }

    private static void Event(MarkupWriter writer, Action handler)
    {
        writer.WriteLiteral("<button");
        writer.WriteEventAttribute("onclick", handler);
        writer.WriteLiteral(">button</button>");
    }

    private static void Dispatch(UiComponent component, string token)
    {
        Check(component.TryGetEventHandler(token, out var handler), "Legacy handler missing.");
        handler(new UiEvent("click", new ElementNode("button")));
    }

    private static void Dispatch(UiComponent.EventRegistry registry, string token)
    {
        Check(registry.TryGetEventHandler(token, out var handler), "Pinned handler missing.");
        handler(new UiEvent("click", new ElementNode("button")));
    }

    private static void SetField(object target, string field, long value)
    {
        var type = target is UiComponent ? typeof(UiComponent) : target.GetType();
        (type.GetField(field, BindingFlags.Instance | BindingFlags.NonPublic) ??
            throw new Exception($"Missing test field {field}")).SetValue(target, value);
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }

    private sealed class Component : UiComponent
    {
        internal Action<MarkupWriter> Build { get; set; } = _ => { };
        internal Action? OnDispose { get; set; }
        internal void Invalidate() => StateHasChanged();
        protected override void BuildRenderTree(MarkupWriter writer) => Build(writer);
        protected override void Dispose(bool disposing)
        {
            // Intentionally omit base: public Dispose must invalidate before this callback.
            if (disposing) OnDispose?.Invoke();
        }
    }

    private sealed class Text(Func<string> format)
    {
        public override string ToString() => format();
    }

    private sealed class Formattable(Func<string> format) : IFormattable
    {
        public string ToString(string? specification, IFormatProvider? provider) => format();
    }
}
