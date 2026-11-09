#if AETHER_DOM_FAULT_TESTING
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

internal static class HostFailureBoundaryTests
{
    internal static void Run()
    {
        var rows = new List<string> { "family\tsite\toccurrence\tboundary\treached" };
        foreach (var family in new[] { "component", "style", "style-reorder" })
        {
            Collect(); var resources = ManagedDomFault.Resources;
            var trace = SweepCase(family, null, 0);
            Collect(); Equal(resources, ManagedDomFault.Resources, "recording resource teardown");
            var counts = new Dictionary<string, int>(StringComparer.Ordinal);
            foreach (var site in trace)
            {
                counts.TryGetValue(site, out var occurrence); counts[site] = ++occurrence;
                SweepCase(family, site, occurrence);
                Collect(); Equal(resources, ManagedDomFault.Resources, site + " resource teardown");
                rows.Add($"{family}\t{site}\t{occurrence}\t{(family != "component" ? "13.4" : "13.3")}\ttrue");
            }
            Check(trace.Length > 0, "No source hooks reached: " + family);
            Console.WriteLine($"PASS host {family}: {trace.Length} reached occurrences / {counts.Count} source operations");
        }
        var recoverySites = RecoverySiteCase(null, 0);
        Collect(); var recoveryResources = ManagedDomFault.Resources;
        foreach (var site in recoverySites)
        {
            Console.WriteLine($"REACHED recovery {site.Site}/{site.Occurrence}");
            RecoverySiteCase(site.Site, site.Occurrence);
            Collect(); Equal(recoveryResources, ManagedDomFault.Resources, "recovery resource teardown");
            rows.Add($"recovery\t{site.Site}\t{site.Occurrence}\t13.5\ttrue");
        }
        Check(recoverySites.Length > 0, "No recovery source sites reached.");
        Console.WriteLine($"PASS recovery: {recoverySites.Length} reached source occurrences after native R2");
        ReorderRetainsBnext();
        TransformSizeOverflow();
        RecoveryFailurePreservesR2();
        AfterJointPublishFailure("diagnostic");
        AfterJointPublishFailure("layout");
        AfterJointPublishFailure("display");
        if (Environment.GetEnvironmentVariable("AETHER_HOST_FAULT_TRACE") is { Length: > 0 } path) File.WriteAllLines(path, rows);
        Console.WriteLine("PASS host failure boundaries 13.4–6; source-site failures and external callback failures are distinct from native/CLR allocator injection");
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static string[] SweepCase(string family, string? selected, int occurrence)
    {
        using var fixture = new Fixture();
        if (family == "component")
        {
            fixture.Host.PointerMoved(new(10, 10)); fixture.Host.Render();
            fixture.Host.PointerPressed(new(10, 10)); fixture.Host.Render();
            fixture.Component.Version = "B"; fixture.Component.Invalidate();
        }
        else
        {
            fixture.ConfirmNativeR2();
            if (family == "style-reorder")
            {
                fixture.A.SetAttribute("style", fixture.A.GetAttribute("style") + ";position:relative;z-index:4");
                fixture.A.State |= ElementState.Invalid;
            }
        }
        var host = fixture.Host;
        var document = host.Document!;
        var registry = Field<UiComponent.EventRegistry>(host, "_componentRegistry");
        var native = NativeImage(fixture.Dom);
        var slot = fixture.Runtime.ReadDomSourceSlot();
        var publication = host.HostPublicationSerial;
        var styleToken = Field<NativeStylePublicationToken>(host, "_publishedStyleToken");
        var transitionToken = fixture.Transitions.ReadToken();
        var frames = fixture.Renderer.Frames;
        var buffers = ManagedDomFault.Resources.Buffers;
        var session = selected is null ? ManagedDomFault.Arm() : ManagedDomFault.ArmSite(selected, occurrence);
        var componentEnd = -1;
        if (family == "component") host.ComponentPublicationProbe = phase => { if (phase == "ready") componentEnd = session.Count; };
        Exception? error = null;
        try { host.Render(); } catch (Exception caught) { error = caught; }
        finally { host.ComponentPublicationProbe = null; session.Dispose(); }
        // Component composition includes every existing source hook reached before the
        // final ready boundary: writer, parse, UTF8, mapping, claims and binding staging.
        // It deliberately excludes style resolution/retirement after B is already published.
        if (selected is null && family == "component") Check(componentEnd >= 0, "Component precommit boundary was not reached.");
        var trace = session.Sites.Take(family == "component" && componentEnd >= 0 ? componentEnd : session.Count)
            .Where(site => family == "component" || site.StartsWith("host.style.", StringComparison.Ordinal) ||
                site.StartsWith("style.preview.", StringComparison.Ordinal) || site.StartsWith("transition.prepare.", StringComparison.Ordinal)).ToArray();
        if (selected is null)
        {
            if (error is not null) throw new Exception("Recording failed: " + family, error);
            Check(fixture.Renderer.Frames > frames, "Recording produced no frame.");
            return trace;
        }
        Check(session.Fired && ReferenceEquals(error, session.Failure), $"Fault {selected}/{occurrence} did not throw the requested sentinel: {error}");
        Equal(native, NativeImage(fixture.Dom), "Confirmed native inputs were rolled back or changed");
        Equal(slot, fixture.Runtime.ReadDomSourceSlot(), "Source slot changed after rejected staging");
        Check(ReferenceEquals(document, host.Document) && ReferenceEquals(registry, Field<object>(host, "_componentRegistry")), "Rejected preparation changed DOM/registry identity.");
        Equal(publication, host.HostPublicationSerial, "Failed preparation published a managed revision");
        Equal(styleToken, Field<NativeStylePublicationToken>(host, "_publishedStyleToken"), "Failed preparation replaced published style token");
        Equal(transitionToken, fixture.Transitions.ReadToken(), "Failed preparation changed transition storage/status");
        Equal(buffers, ManagedDomFault.Resources.Buffers, "Temporary transition buffers leaked");
        Check(ManagedDomFault.Resources.Valid, "Invalid resource accounting.");
        AssertClosed(host, fixture.Renderer, frames);
        var produced = Field<long[]>(host, "_invalidationProduced");
        var acknowledged = Field<long[]>(host, "_invalidationAcknowledged");
        Check(produced[0] > acknowledged[0], "Pending style input was acknowledged by failure.");
        Check(registry.TryGetEventHandler("aether:0", out var handler), "Missing pinned A handler.");
        handler(new("click", document.GetElementById("a")!));
        Check(fixture.Component.Trace.Last() == "A", "B reused token zero and rerouted failed publication.");
        if (family != "component") fixture.AssertR2Mirror();
        if (family == "style-reorder")
            Check(HasPendingElement(host, fixture.A), "Rejected reorder consumed pending same-node batch.");
        host.Render(); // An explicit attempt, after disarming, recovers from current inputs.
        if (family != "component") fixture.AssertFullOracle();
        else Check(!ReferenceEquals(document, host.Document), "Explicit component retry failed.");
        return trace;
    }

    private readonly record struct ReachedSite(string Site, int Occurrence);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static ReachedSite[] RecoverySiteCase(string? selected, int occurrence)
    {
        using var f = new Fixture();
        f.A.SetAttribute("style", "width:133px;height:60px;opacity:.6;transform:var(--recovery-source-missing)");
        var publication = f.Host.HostPublicationSerial;
        var frames = f.Renderer.Frames;
        var transition = f.Transitions.ReadToken();
        var buffers = ManagedDomFault.Resources.Buffers;
        var callbacks = 0;
        var recoveryStart = -1;
        var recoveryEnd = -1;
        string? r2Image = null;
        NativeStylePublicationToken r2 = default;
        var session = selected is null ? ManagedDomFault.Arm() : ManagedDomFault.ArmSite(selected, occurrence);
        StyleDiagnosticCallback callback = (_, _, _, _, _) =>
        {
            if (++callbacks != 1) return;
            f.Dom.Native.SetAttribute(f.Dom.GetNativeHandle(f.A), "style", "width:177px;height:60px;opacity:.4;transform:translateX(6px)");
            using var current = f.Runtime.PreparePublication(null, 240, 240, 1);
            Check(current.Commit(), "R2 commit in source-sweep failed.");
            using var map = current.Publish();
            r2 = current.Token;
            r2Image = NativeImage(f.Dom);
            recoveryStart = session.Count;
        };
        NativeMethods.StyleRuntimeSetDiagnosticsCallback(Field<IntPtr>(f.Runtime, "_runtime"), callback, IntPtr.Zero);
        f.Host.StylePublicationProbe = phase => { if (recoveryStart >= 0 && phase == "managed-staged") recoveryEnd = session.Count; };
        Exception? error = null;
        try { f.Host.Render(); } catch (Exception caught) { error = caught; }
        finally
        {
            NativeMethods.StyleRuntimeSetDiagnosticsCallback(Field<IntPtr>(f.Runtime, "_runtime"), null, IntPtr.Zero);
            f.Host.StylePublicationProbe = null;
            session.Dispose(); GC.KeepAlive(callback);
        }
        Check(recoveryStart >= 0 && callbacks >= 1, "Native R2 callback was not reached.");
        var sites = new List<ReachedSite>();
        var counts = new Dictionary<string, int>(StringComparer.Ordinal);
        for (var i = 0; i < session.Count; i++)
        {
            var site = session.Sites[i]; counts.TryGetValue(site, out var count); counts[site] = ++count;
            if (i >= recoveryStart && i < recoveryEnd)
                sites.Add(new(site, count));
        }
        if (selected is null)
        {
            if (error is not null) throw new Exception("Recovery source recording failed.", error);
            Check(recoveryEnd >= recoveryStart, "Recovery precommit boundary was not reached.");
            Equal(publication + 1, f.Host.HostPublicationSerial, "Recovery published more than one accepted revision");
            return sites.ToArray();
        }
        Check(session.Fired && ReferenceEquals(error, session.Failure), $"Recovery {selected}/{occurrence} missed its sentinel: {error}");
        Equal(publication, f.Host.HostPublicationSerial, "Recovery failure published stale R1 or partial recovery");
        Check(f.Runtime.IsCurrent(r2), "Recovery failure replaced native R2.");
        Equal(r2Image, NativeImage(f.Dom), "Recovery failure changed native current inputs");
        Equal(transition, f.Transitions.ReadToken(), "Rejected recovery changed transition storage");
        Equal(buffers, ManagedDomFault.Resources.Buffers, "Recovery leaked transition buffers");
        AssertClosed(f.Host, f.Renderer, frames);
        // Reading managed attributes is itself an explicit import retry. Check the
        // failure gates first, then permit that retry and verify current R2 inputs.
        Check(f.A.GetAttribute("style")!.Contains("177px", StringComparison.Ordinal), "Recovery restored stale managed inputs.");
        Check(Field<long[]>(f.Host, "_invalidationProduced")[0] > Field<long[]>(f.Host, "_invalidationAcknowledged")[0], "Recovery consumed pending input.");
        f.Host.Render();
        f.AssertFullOracle();
        return sites.ToArray();
    }

    private static void ReorderRetainsBnext()
    {
        using var f = new Fixture();
        f.ConfirmNativeR2();
        f.A.SetAttribute("style", f.A.GetAttribute("style") + ";position:relative;z-index:4");
        var publication = f.Host.HostPublicationSerial;
        var frames = f.Renderer.Frames;
        string? newest = null;
        f.Host.StylePublicationProbe = phase =>
        {
            if (phase != "native-preview") return;
            f.A.State |= ElementState.Invalid;
            newest = NativeImage(f.Dom);
        };
        var session = ManagedDomFault.ArmSite("host.style.paint-reset", 1);
        Exception? error = null;
        try { f.Host.Render(); } catch (Exception caught) { error = caught; }
        finally { f.Host.StylePublicationProbe = null; session.Dispose(); }
        Check(session.Fired && ReferenceEquals(error, session.Failure), "Reorder Bnext site missed sentinel.");
        Check(newest is not null, "Reorder Bnext callback not reached.");
        Equal(newest, NativeImage(f.Dom), "Reorder rejection rolled back newer Bnext inputs");
        Equal(publication, f.Host.HostPublicationSerial, "Reorder failure published a projection");
        Check(Field<HashSet<ElementNode>>(f.Host, "_stateDirtyElements").Contains(f.A), "Reorder consumed callback Bnext.");
        AssertClosed(f.Host, f.Renderer, frames);
        f.Host.Render(); f.AssertFullOracle();
        Console.WriteLine("PASS paint-reset rejection retains callback Bnext without rollback");
    }

    private sealed class OversizedTransforms : IReadOnlyList<CssTransformOperation>
    {
        public int Count => int.MaxValue;
        public CssTransformOperation this[int index] => throw new InvalidOperationException("Overflow must precede indexing.");
        public IEnumerator<CssTransformOperation> GetEnumerator() => throw new InvalidOperationException("Overflow must precede enumeration.");
        System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => GetEnumerator();
    }

    private static void TransformSizeOverflow()
    {
        var resources = ManagedDomFault.Resources;
        var threw = false;
        try
        {
            using var observation = TransitionStyleMarshaler.Prepare(IntPtr.Zero,
                new ComputedStyle { Transforms = new OversizedTransforms() }, new ComputedStyle());
        }
        catch (OverflowException) { threw = true; }
        Check(threw, "Transform byte count overflow was not rejected before allocation.");
        Equal(resources, ManagedDomFault.Resources, "Transform overflow allocated a native buffer");
        Console.WriteLine("PASS checked transform byte count without oversized allocation");
    }

    private static void RecoveryFailurePreservesR2()
    {
        using var f = new Fixture();
        var host = f.Host;
        f.A.SetAttribute("style", "width:133px;height:60px;opacity:.6;transform:var(--boundary-missing)");
        var publication = host.HostPublicationSerial;
        var frames = f.Renderer.Frames;
        var calls = 0; var recoveryReads = 0;
        var failure = new OutOfMemoryException("recovery source read");
        NativeStylePublicationToken r2 = default;
        StyleDiagnosticCallback callback = (_, _, _, _, _) =>
        {
            if (++calls != 1) return;
            f.Dom.Native.SetAttribute(f.Dom.GetNativeHandle(f.A), "style", "width:177px;height:60px;opacity:.4;transform:translateX(6px)");
            using var current = f.Runtime.PreparePublication(null, 240, 240, 1);
            Check(current.Commit(), "Native R2 commit failed.");
            using var map = current.Publish();
            r2 = current.Token;
            // The outer preview already exists. Only the ensuing recovery reads styles.
            StyleRuntimeInterop.StyleReadProbe = () => { recoveryReads++; throw failure; };
        };
        NativeMethods.StyleRuntimeSetDiagnosticsCallback(Field<IntPtr>(f.Runtime, "_runtime"), callback, IntPtr.Zero);
        Exception? observed = null;
        try { host.Render(); } catch (Exception error) { observed = error; }
        finally
        {
            StyleRuntimeInterop.StyleReadProbe = null;
            NativeMethods.StyleRuntimeSetDiagnosticsCallback(Field<IntPtr>(f.Runtime, "_runtime"), null, IntPtr.Zero);
            GC.KeepAlive(callback);
        }
        Check(calls >= 1 && recoveryReads == 1 && ReferenceEquals(observed, failure), "Bounded recovery did not reach its first style read failure.");
        Equal(publication, host.HostPublicationSerial, "Superseded R1 or failed recovery was published");
        Check(f.Runtime.IsCurrent(r2), "Failure replaced native R2.");
        Check(f.A.GetAttribute("style")!.Contains("177px", StringComparison.Ordinal), "Recovery replayed old managed inputs over R2.");
        AssertClosed(host, f.Renderer, frames);
        host.Render();
        Equal(177f, Field<IReadOnlyDictionary<ElementNode, ComputedStyle>>(host, "_computedStyles")[f.A].Width.Value, "Explicit recovery did not use R2");
        Console.WriteLine("PASS 13.5 native callback R2 retained through recovery source-read failure");
    }

    private static void AfterJointPublishFailure(string kind)
    {
        using var f = new Fixture(images: kind == "display");
        f.ConfirmNativeR2();
        var host = f.Host;
        var publication = host.HostPublicationSerial;
        var frames = f.Renderer.Frames;
        var reached = false;
        NativeStylePublicationToken published = default;
        NativeTransitionToken newest = default;
        var failure = new FormatException(kind + " after joint publication");
        void ChangeAndFail()
        {
            if (reached) return;
            reached = true;
            Check(host.HostPublicationSerial == publication + 1, "External callback did not occur after joint publication.");
            published = Field<NativeStylePublicationToken>(host, "_publishedStyleToken");
            Check(f.Transitions.TryTick(long.MaxValue, out _) == 0, "Newer tick failed.");
            newest = f.Transitions.ReadToken();
            f.A.SetAttribute("data-after", kind);
            f.A.State |= ElementState.Invalid; // A new Bnext entry after joint publication.
            throw failure;
        }
        var diagnostic = "boundary-" + Guid.NewGuid().ToString("N");
        Action<CssStyleDiagnostic> listener = d => { if (d.Property == diagnostic) ChangeAndFail(); };
        if (kind == "diagnostic")
        {
            host.StylePublicationProbe = phase => { if (phase == "native-preview") CssDiagnostics.ReportUnsupportedValue(diagnostic, "value"); };
            CssDiagnostics.Reported += listener;
        }
        else if (kind == "layout") f.Measurer.Callback = ChangeAndFail;
        else f.Images.Callback = ChangeAndFail;
        Exception? error = null;
        try { host.Render(); } catch (Exception caught) { error = caught; }
        finally
        {
            host.StylePublicationProbe = null;
            CssDiagnostics.Reported -= listener;
            f.Measurer.Callback = null; f.Images.Callback = null;
        }
        // Native text measurement currently catches callback exceptions. Its newer tick/input
        // still invalidates derived layout and must surface rejection, never a valid frame.
        Check(reached && error is not null && (kind == "layout" || ReferenceEquals(error, failure) || ReferenceEquals(error.InnerException, failure)), "External failure did not reject derived work: " + kind + " reached=" + reached + " error=" + error);
        Equal(publication + 1, host.HostPublicationSerial, "Joint publication rolled back after external failure");
        Equal(published, Field<NativeStylePublicationToken>(host, "_publishedStyleToken"), "R1 certificate was overwritten during failure");
        Equal(newest, f.Transitions.ReadToken(), "A later tick was rolled back after publication");
        Equal(kind, f.A.GetAttribute("data-after"), "Callback's later DOM input was lost");
        AssertClosed(host, f.Renderer, frames);
        Check(Field<long[]>(host, "_invalidationProduced")[0] > Field<long[]>(host, "_invalidationAcknowledged")[0], "Callback Bnext disappeared.");
        Check(Field<HashSet<ElementNode>>(host, "_stateDirtyElements").Contains(f.A), "Callback's new same-node Bnext entry disappeared.");
        host.Render();
        Check(f.Renderer.Frames == frames + 1, "Explicit derived recovery failed.");
        Console.WriteLine("PASS 13.6 " + kind + " failure preserves R1 and newer tick/DOM inputs");
    }

    private sealed class Fixture : IDisposable
    {
        internal readonly TestRenderer Renderer = new();
        internal readonly TestMeasurer Measurer = new();
        internal readonly TestImages Images = new();
        internal readonly TestComponent Component;
        internal readonly UiDocumentHost Host;
        private readonly bool _images;
        internal Fixture(bool images = false)
        {
            _images = images;
            Component = new(images);
            Host = new(Component, Measurer, Renderer, new UiHostOptions { Width = 240, Height = 240, ImageResolver = images ? Images : null });
            Host.Render();
        }
        internal DomInterop Dom => Field<DomInterop>(Host, "_nativeDom");
        internal StyleRuntimeInterop Runtime => Field<StyleRuntimeInterop>(Host, "_nativeStyleRuntime");
        internal TransitionRuntimeInterop Transitions => Field<TransitionRuntimeInterop>(Host, "_transitionRuntime");
        internal ElementNode A => Host.Document!.GetElementById("a")!;
        internal void ConfirmNativeR2()
        {
            foreach (var id in new[] { "a", "b", "c" })
            {
                var element = Host.Document!.GetElementById(id)!;
                Dom.Native.SetAttribute(Dom.GetNativeHandle(element), "style", "width:121px;height:60px;opacity:.4;transform:translateX(6px)" +
                    (_images && id == "a" ? ";background-image:url('memory.png')" : ""));
            }
            _ = A.State; // Explicit successful import BEFORE fault arming.
            AssertR2Mirror();
        }
        internal void AssertR2Mirror()
        {
            foreach (var id in new[] { "a", "b", "c" })
                Check(Host.Document!.GetElementById(id)!.GetAttribute("style")!.Contains("121px", StringComparison.Ordinal), "Confirmed R2 mirror lost.");
        }
        internal void AssertFullOracle()
        {
            using var oracle = new StyleRuntimeInterop(240, 240, 1);
            oracle.Load(Dom.Native, "", TestComponent.Css);
            using var full = oracle.PreparePublication(null, 240, 240, 1);
            Check(full.Commit(), "Independent FULL failed.");
            using var map = full.Publish();
            using var layoutEngine = new LayoutInterop(new ApproximateTextMeasurer());
            using var fullLayout = layoutEngine.CreateNativeLayout(Dom, map, new SizeF(240, 240));
            var computed = Field<IReadOnlyDictionary<ElementNode, ComputedStyle>>(Host, "_computedStyles");
            foreach (var id in new[] { "a", "b", "c" })
            {
                var node = Host.Document!.GetElementById(id)!;
                var expected = full.Styles.Single(value => value.Node == Dom.GetNativeHandle(node)).Style;
                Equal(expected.Width.Value, computed[node].Width.Value, "FULL width mismatch");
                Equal(expected.Opacity, computed[node].Opacity, "FULL opacity mismatch");
                var expectedBox = Find(fullLayout.Root, Dom.GetNativeHandle(node)) ?? throw new Exception("FULL layout lacks node.");
                var actualBox = Host.Layout!.FindPrimaryBox(node)!.BorderBox;
                Equal(expectedBox.BorderBox.Width, actualBox.Width, "FULL layout width mismatch");
                Equal(expectedBox.BorderBox.Height, actualBox.Height, "FULL layout height mismatch");
            }
            NativeLayoutBox? Find(NativeLayoutBox box, IntPtr target)
            {
                if (box.Element == target && box.Kind != 2) return box;
                for (var i = 0; i < box.ChildCount; i++)
                    if (Find(fullLayout.GetChild(box, i), target) is { } found) return found;
                return null;
            }
        }
        public void Dispose() => Host.Dispose();
    }

    private static string NativeImage(DomInterop dom)
    {
        using var borrow = dom.Native.Borrow();
        using var snapshot = NativeDomSnapshot.Acquire(borrow.Handle);
        var image = new StringBuilder().Append(snapshot.Token);
        foreach (var node in snapshot.Nodes)
        {
            image.Append('|').Append(node.Identity).Append(':').Append(node.OwnerIdentity).Append(':').Append(node.ParentIndex)
                .Append(':').Append(node.DisconnectSerial).Append(':').Append(node.State).Append(':').Append(node.Tag).Append(':').Append(node.Text)
                .Append(':').Append(node.AuthoredValue ?? "<null>").Append(':').Append(node.RuntimeValue ?? "<null>");
            foreach (var pair in node.Attributes.OrderBy(pair => pair.Key, StringComparer.Ordinal)) image.Append(';').Append(pair.Key).Append('=').Append(pair.Value);
            image.Append('/').AppendJoin(',', node.Children).Append('/').AppendJoin(',', node.StateBitWriteSerial);
        }
        return image.ToString();
    }
    private static bool HasPendingElement(UiDocumentHost host, ElementNode element)
    {
        if (Field<HashSet<ElementNode>>(host, "_stateDirtyElements").Contains(element)) return true;
        for (var batch = Field<object?>(host, "_retainedStyleBatches"); batch is not null; batch = Field<object?>(batch, "Next"))
            if (Field<HashSet<ElementNode>>(batch, "Elements").Contains(element)) return true;
        return false;
    }
    private static void AssertClosed(UiDocumentHost host, TestRenderer renderer, int frames)
    {
        Check(host.ManagedProjectionOutOfDate && !host.NeedsRender, "Failed work did not retain dirty/out-of-date and suppress auto retry.");
        Equal(frames, renderer.Frames, "Failure presented a new frame");
        Equal(CursorKind.Default, host.CursorAt(new(10, 10)), "Failure certified a new hit");
    }
    private static T Field<T>(object value, string name) => (T)value.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(value)!;
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static void Equal<T>(T expected, T actual, string message) { if (!Equals(expected, actual)) throw new Exception($"{message}: expected {expected}, got {actual}"); }
    private static void Collect() { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); }
    private sealed class TestComponent(bool images) : UiComponent
    {
        internal const string Css = "body{margin:0}button{display:block;width:80px;height:60px;cursor:pointer;opacity:1;transform:translateX(0px);transition:opacity 1s linear,transform 1s linear}";
        internal string Version = "A";
        internal readonly List<string> Trace = [];
        internal void Invalidate() => StateHasChanged();
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            var version = Version;
            writer.WriteLiteral("<style>" + Css + "</style><body><button id='a'");
            writer.WriteEventAttribute("onclick", _ => Trace.Add(version));
            if (images) writer.WriteAttribute("style", "background-image:url('memory.png')");
            writer.WriteLiteral(">text A</button><button id='b'>text B</button><button id='c'>text C</button>");
            for (var i = 0; i < (Version == "A" ? 1 : 8); i++) writer.WriteLiteral($"<input id='field{i}' value='{Version}'>");
            writer.WriteLiteral("</body>");
        }
    }
    private sealed class TestRenderer : IRenderBackend
    {
        internal int Frames;
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList display, ColorRgba clear) => Frames++;
        public void Dispose() { }
    }
    private sealed class TestMeasurer : ITextMeasurer
    {
        private readonly ApproximateTextMeasurer _inner = new();
        internal Action? Callback;
        public TextMetrics MeasureText(string text, ComputedStyle style, float maxWidth = float.PositiveInfinity)
        { Callback?.Invoke(); return _inner.MeasureText(text, style, maxWidth); }
    }
    private sealed class TestImages : IImageResourceResolver
    {
        internal Action? Callback;
        public event EventHandler<ImageResourceChangedEventArgs>? Changed { add { } remove { } }
        public void Request(string source) { }
        public bool TryGetDimensions(string source, out ImageDimensions dimensions) { dimensions = new(1, 1); return true; }
        public bool TryGetResource(string source, out ImageResourceResult resource) { Callback?.Invoke(); resource = null!; return false; }
    }
}
#endif
