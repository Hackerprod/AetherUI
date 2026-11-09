#if AETHER_DOM_FAULT_TESTING
using System.Collections;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Text;
using AetherUI.Core.Native;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

// Real source-operation failures, not phase probes or a CLR allocator replacement.
internal static class ClaimsFaultSiteTests
{
    private const int LossCount = 5;
    internal static void Run()
    {
        var rows = new List<string> { "family\tsite\toccurrence\tboundary\treached" };
        foreach (var family in new[] { "losses", "acquire", "restore" })
        {
            Collect(); var baseline = ManagedDomFault.Resources;
            var trace = Sweep(family, null, 0);
            Collect(); Equal(baseline, ManagedDomFault.Resources, family + " recording teardown");
            var counts = new Dictionary<string, int>(StringComparer.Ordinal);
            foreach (var site in trace)
            {
                counts.TryGetValue(site, out var occurrence); counts[site] = ++occurrence;
                var boundary = family == "losses" ? "8.2-capture-or-delivery-prefix" : "13.3";
                Sweep(family, site, occurrence);
                Collect(); Equal(baseline, ManagedDomFault.Resources, family + "/" + site + " teardown");
                rows.Add($"{family}\t{site}\t{occurrence}\t{boundary}\ttrue");
            }
            Check(trace.Length != 0, "Empty fault trace for " + family);
            Console.WriteLine($"PASS claims {family}: {trace.Length} occurrences / {counts.Count} source operations");
        }
        Collect(); var callbacksBaseline = ManagedDomFault.Resources;
        CallbackBoundaries();
        Collect(); Equal(callbacksBaseline, ManagedDomFault.Resources, "callback teardown");
        if (Environment.GetEnvironmentVariable("AETHER_CLAIMS_FAULT_TRACE") is { Length: > 0 } path) File.WriteAllLines(path, rows);
        Console.WriteLine("PASS claims capture/delivery source sites and separate callback boundaries; no global D1 or native/CLR allocator claim");
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static string[] Sweep(string family, string? selected, int occurrence)
    {
        using var f = new Fixture();
        if (family == "losses") f.PrepareLosses();
        else
        {
            Invoke(f.Host, "UpdateHoverTarget", f.Nodes[0], new PointF(10, 10), false);
            Invoke(f.Host, "Focus", f.Nodes[1]);
            if (family == "restore") f.Component.Invalidate();
        }
        var before = f.Image(); var native = NativeImage(f.Dom);
        var claims = f.ClaimSnapshots();
        var token = NativeDomSync.ReadToken(f.Dom.Native);
        var document = f.Host.Document;
        var registry = Field<object>(f.Host, "_componentRegistry");
        var inputVersion = document!.InputVersion; var version = document.Version;
        var serial = f.Host.HostInputSerial; var publication = f.Host.HostPublicationSerial;
        var style = Field<NativeStylePublicationToken>(f.Host, "_publishedStyleToken");
        var resources = ManagedDomFault.Resources; var frames = f.Renderer.Frames;
        var notifications = f.Notifications;
        var fault = selected is null ? ManagedDomFault.Arm() : ManagedDomFault.ArmSite(selected, occurrence);
        var captureEnd = -1; var deliveryEnd = -1;
        f.Host.InteractionLossCaptureProbe = phase => { if (phase == "before-array") captureEnd = fault.Count; };
        if (family == "losses") f.Component.Blur = _ => deliveryEnd = fault.Count;
        Exception? error = null;
        try
        {
            if (family == "acquire") Invoke(f.Host, "AcquireInteractionClaim", f.Nodes[2], ElementState.Hover);
            else if (family == "losses") f.Host.Render();
            else f.Host.Render();
        }
        catch (Exception caught) { error = caught; }
        finally { f.Host.InteractionLossCaptureProbe = null; f.Component.Blur = null; fault.Dispose(); }
        var trace = fault.Sites.Take(selected is null && family == "losses" ? deliveryEnd : fault.Count).Where(site => family switch
        {
            "losses" => true,
            "acquire" => site.StartsWith("host.claims.acquire-", StringComparison.Ordinal),
            _ => site.StartsWith("host.claims.restore-", StringComparison.Ordinal) || site == "host.claims.session-aggregate"
        }).ToArray();
        if (selected is null)
        {
            if (error is not null) throw new Exception("Recording failed " + family, error);
            if (family == "losses") f.AssertDelivered(LossCount);
            return trace;
        }
        Check(fault.Fired && ReferenceEquals(error, fault.Failure), $"Wrong failure {family}/{selected}/{occurrence}: {error}");
        // Never use a freshness getter/import before checking the rejection gate.
        if (family != "acquire")
        {
            Check(f.Host.ManagedProjectionOutOfDate && !f.Host.NeedsRender && Field<bool>(f.Host, "_styleRetryBlocked"), "Retry gate open after failure");
            Equal(CursorKind.Default, f.Host.CursorAt(new(10, 10)), "Failure certified a new hit");
        }
        Equal(frames, f.Renderer.Frames, "Failure painted a frame");
        Equal(notifications, f.Notifications, "Failure notified user");
        Equal(publication, f.Host.HostPublicationSerial, "Failure published managed styles");
        Equal(style, Field<NativeStylePublicationToken>(f.Host, "_publishedStyleToken"), "Failure replaced style token");
        Check(ReferenceEquals(document, f.Host.Document) && ReferenceEquals(registry, Field<object>(f.Host, "_componentRegistry")), "Failure replaced DOM or registry A");
        Equal(0, Field<int>(f.Host, "_interactionEntryDepth"), "Leaked entry depth");
        Check(!Field<bool>(f.Host, "_drainingInteractionLosses"), "Leaked loss guard");
        if (family == "losses")
        {
            var prefix = f.Component.Trace.Count;
            var captureFailure = selected.StartsWith("host.losses.", StringComparison.Ordinal) || selected == "interop.utf8.decode";
            if (captureFailure) Equal(0, prefix, "Capture failed after prior delivery/ack/cleanup");
            Equal(LossCount, claims.Length, "Fixture lacks multiple losses");
            for (var i = 0; i < claims.Length; i++)
            {
                var c = claims[i];
                if (i < prefix)
                {
                    Check(c.Native.IsClosed && !c.Element.StateCore.HasFlag(ElementState.Focus), "Completed prefix retained authority/state");
                    for (var bit = 0; bit < 32; bit++)
                        Equal(c.Stamps[bit] + (((uint)ElementState.Focus & (1u << bit)) != 0 ? 1UL : 0UL), c.Element.NativeRecordCore!.StateBitWriteSerial[bit], "Prefix bit stamp");
                }
                else
                {
                    Check(!c.Native.IsClosed && c.Native.TryReadLoss(out _), "Pending suffix was acknowledged");
                    Equal(c.Info, c.Native.Read(), "Pending authority/generation changed");
                    Equal(c.Loss, LossImage(c.Native), "Pending loss scalars/frozen route changed");
                    Equal(c.State, c.Element.StateCore, "Pending State changed");
                    Check(c.Stamps.SequenceEqual(c.Element.NativeRecordCore!.StateBitWriteSerial), "Pending bit stamps changed");
                }
            }
            f.AssertDelivered(prefix);
            Equal(version, document.Version, "Loss cleanup changed structural version");
            Equal(inputVersion + prefix, document.InputVersion, "Wrong input prefix");
            Equal(serial + prefix, f.Host.HostInputSerial, "Wrong host bookkeeping prefix");
            if (prefix == 0)
            {
                Equal(token, NativeDomSync.ReadToken(f.Dom.Native), "Capture changed native token");
                Equal(native, NativeImage(f.Dom), "Capture changed native DOM/stamps");
                Equal(before, f.Image(), "Capture changed managed DOM/claims/bookkeeping");
                Equal(resources, ManagedDomFault.Resources, "Capture leaked ownership");
            }
            // This is a fresh explicit attempt after disarming, never an automatic retry.
            f.Host.Render(); f.AssertDelivered(LossCount);
            Check(!f.Pending(), "Explicit retry retained the old loss batch");
            f.Host.Render(); f.AssertDelivered(LossCount);
        }
        else
        {
            Equal(token, NativeDomSync.ReadToken(f.Dom.Native), "Failed claim preparation changed token");
            Equal(native, NativeImage(f.Dom), "Failed claim preparation changed DOM");
            Equal(before, f.Image(), "Failed claim preparation changed authority/bookkeeping");
            Equal(resources, ManagedDomFault.Resources, "Claim staging leaked ownership");
            if (family == "acquire") Invoke(f.Host, "AcquireInteractionClaim", f.Nodes[2], ElementState.Hover);
            else f.Host.Render();
        }
        return trace;
    }

    // Callback effects are real user code and are deliberately not counted as injected allocation sites.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void CallbackBoundaries()
    {
        using (var f = new Fixture())
        {
            f.PrepareLosses(); var once = false;
            f.Component.Blur = e =>
            {
                if (once) return; once = true;
                Check(ReferenceEquals(e.Target, e.CurrentTarget), "Old blur bubbled");
                Invoke(f.Host, "Focus", f.Nodes[5]);
                f.Parent.RemoveChild(f.Nodes[5]); f.Parent.AppendChild(f.Nodes[5]);
                Invoke(f.Host, "Focus", f.Nodes[6]);
                f.Nodes[6].State |= ElementState.Invalid;
                f.Host.Render(); // Reentry must not extend this entry's finite batch.
            };
            f.Host.Render(); f.AssertDelivered(LossCount);
            Check(f.Pending() && f.Host.NeedsRender, "Callback Bnext loss was consumed/hidden");
            Check(ReferenceEquals(f.Host.FocusedElement, f.Nodes[6]) && f.Nodes[6].StateCore.HasFlag(ElementState.Focus | ElementState.Invalid), "Old cleanup erased callback focus/input");
            f.Host.Render();
            Equal(LossCount + 1, f.Component.Trace.Count, "Next entry did not deliver Bnext exactly once");
            Check(ReferenceEquals(f.Host.FocusedElement, f.Nodes[6]) && f.Nodes[6].StateCore.HasFlag(ElementState.Focus | ElementState.Invalid), "Bnext cleanup erased newer focus");
        }
        using (var source = new Fixture())
        using (var destination = new Fixture())
        {
            source.PrepareLosses(); var moved = source.Nodes[0];
            destination.Parent.AppendChild(moved); destination.Host.Render();
            Invoke(destination.Host, "Focus", moved);
            var stamp = moved.NativeRecordCore!.StateBitWriteSerial.ToArray();
            source.Host.Render(); source.AssertDelivered(LossCount);
            Check(ReferenceEquals(destination.Host.FocusedElement, moved) && moved.StateCore.HasFlag(ElementState.Focus), "Source cleanup revoked destination authority");
            Check(stamp.SequenceEqual(moved.NativeRecordCore.StateBitWriteSerial), "Source cleanup rewrote destination stamp");
            Equal(0, destination.Component.Trace.Count, "Source loss used destination registry");
        }
        using (var f = new Fixture())
        {
            f.PrepareLosses(); var error = new FormatException("callback after acknowledged loss");
            f.Component.Blur = _ => { f.Nodes[6].State |= ElementState.Invalid; throw error; };
            var frames = f.Renderer.Frames;
            try { f.Host.Render(); throw new Exception("Missing callback failure"); }
            catch (FormatException caught) { Check(ReferenceEquals(error, caught), "Wrong callback failure"); }
            Equal(1, f.Component.Trace.Count, "Throwing callback repeated/continued batch");
            Check(!f.Host.NeedsRender && f.Host.ManagedProjectionOutOfDate, "Callback failure opened retry gate");
            Equal(frames, f.Renderer.Frames, "Callback failure painted");
            Check(f.Nodes[6].StateCore.HasFlag(ElementState.Invalid) && f.Pending(), "Callback input or pending suffix was rolled back");
            f.Component.Blur = null; f.Host.Render(); f.AssertDelivered(LossCount);
        }
        using (var f = new Fixture())
        {
            f.PrepareLosses(); f.Component.Blur = _ => f.Host.Dispose();
            f.Host.Render(); Equal(1, f.Component.Trace.Count, "Disposed host invoked remaining loss callbacks");
        }
        Console.WriteLine("PASS separate callbacks: finite batch/Bnext, new focus, adoption, throwing callback, disposal");
    }

    private sealed record ClaimSnapshot(NativeInteractionClaim Native, ElementNode Element,
        NativeInteractionClaimInfo Info, string Loss, ElementState State, ulong[] Stamps);
    private sealed class Fixture : IDisposable
    {
        internal readonly TestComponent Component = new();
        internal readonly TestRenderer Renderer = new();
        internal readonly UiDocumentHost Host;
        internal readonly ElementNode[] Nodes;
        internal readonly ElementNode Parent;
        internal int Notifications;
        internal DomInterop Dom => Field<DomInterop>(Host, "_nativeDom");
        internal object Session => Field<object>(Host, "_interactionSession");
        internal Fixture()
        {
            Host = new(Component, new ApproximateTextMeasurer(), Renderer, new UiHostOptions { Width = 300, Height = 600 });
            Host.Render();
            Nodes = Enumerable.Range(0, 7).Select(i => Host.Document!.GetElementById("n" + i)!).ToArray();
            Parent = Host.Document!.GetElementById("parent")!;
            Host.RenderRequested += (_, _) => Notifications++;
        }
        internal void PrepareLosses()
        {
            // Public host operations nested in one genuine entry create several prior
            // records without fabricating native descriptors or changing drain flags.
            using ((IDisposable)Invoke(Host, "EnterInteractionEntry")!)
            {
                var tokens = Nodes.Select(n => n.GetAttribute("onblur")).ToArray();
                for (var i = 0; i < LossCount; i++)
                {
                    Invoke(Host, "Focus", Nodes[i]);
                    Nodes[i].SetAttribute("onblur", tokens[i + 1]);
                    Parent.RemoveChild(Nodes[i]); Parent.AppendChild(Nodes[i]);
                    Nodes[i].SetAttribute("onblur", tokens[6]);
                }
            }
            // Reuses aether:* tokens in registry B. Loss delivery must still use A.
            Component.Version = "B"; Component.RenderMarkup();
            Equal(LossCount, ClaimSnapshots().Length, "Expected all pending focus claims");
            Check(Pending(), "Fixture has no pending loss");
        }
        internal ClaimSnapshot[] ClaimSnapshots() => ((IEnumerable)Field<object>(Session, "Claims")).Cast<object>()
            .Select(c => { var n = Field<NativeInteractionClaim>(c, "Native"); var e = Field<ElementNode>(c, "Element"); return new ClaimSnapshot(n, e, n.Read(), LossImage(n), e.StateCore, e.NativeRecordCore!.StateBitWriteSerial.ToArray()); }).ToArray();
        internal bool Pending() => (bool)Invoke(Host, "HasPendingInteractionLosses")!;
        internal void AssertDelivered(int count)
        {
            Equal(count, Component.Trace.Count, "Unexpected loss callback count");
            for (var i = 0; i < count; i++) Equal($"A:{i + 1}:n{i}", Component.Trace[i], "Frozen registry/token/target route");
        }
        internal string Image()
        {
            var text = new StringBuilder();
            void Visit(Node n)
            {
                text.Append(n.NodeType).Append('/').Append(n.NativeRecordCore?.Identity).Append('/').Append(n.NativeRecordCore?.DisconnectSerial).Append('/').Append(n.NativeRecordCore?.Attached);
                if (n.NativeRecordCore is { } r) text.Append('/').AppendJoin(',', r.StateBitWriteSerial);
                if (n is ElementNode e) { text.Append('/').Append(e.StateCore).Append('/').Append(e.RuntimeValueCore); foreach (var p in e.AttributesCore.OrderBy(p => p.Key)) text.Append('/').Append(p.Key).Append('=').Append(p.Value); }
                text.Append('('); foreach (var c in n.ChildrenCore) Visit(c); text.Append(')');
            }
            Visit(Host.Document!);
            foreach (var c in ClaimSnapshots()) text.Append('|').Append(c.Info).Append('/').Append(c.Loss);
            return text.Append('|').Append(Host.Document!.Version).Append('/').Append(Host.Document.InputVersion).Append('/').Append(Host.HostInputSerial)
                .Append('/').AppendJoin(',', Field<long[]>(Host, "_invalidationProduced")).Append('/').AppendJoin(',', Field<long[]>(Host, "_invalidationAcknowledged")).ToString();
        }
        public void Dispose() => Host.Dispose();
    }
    private static string LossImage(NativeInteractionClaim native)
    {
        if (!native.TryReadLoss(out var loss)) return "none";
        return $"{loss.Claim}/{loss.RecordId}/{loss.RecordSerial}/{loss.DisconnectSerial}/{loss.StateBitWriteSerial}/{loss.Reason}/{loss.HandlerPresent}/" +
            (loss.HandlerPresent != 0 ? NativeDomCodec.Decode(loss.Handler) : "<absent>");
    }
    private static string NativeImage(DomInterop dom)
    {
        using var borrow = dom.Native.Borrow(); using var snapshot = NativeDomSnapshot.Acquire(borrow.Handle);
        var text = new StringBuilder().Append(snapshot.Token);
        foreach (var n in snapshot.Nodes)
        {
            text.Append('|').Append(n.Identity).Append('/').Append(n.OwnerIdentity).Append('/').Append(n.ParentIndex).Append('/').Append(n.DisconnectSerial).Append('/').Append(n.State).Append('/').Append(n.Tag).Append('/').Append(n.Text).Append('/').Append(n.RuntimeValue).Append('/').Append(n.AuthoredValue);
            foreach (var p in n.Attributes.OrderBy(p => p.Key)) text.Append('/').Append(p.Key).Append('=').Append(p.Value);
            text.Append('/').AppendJoin(',', n.Children).Append('/').AppendJoin(',', n.StateBitWriteSerial);
        }
        return text.ToString();
    }
    private static object? Invoke(object target, string method, params object?[] args)
    {
        try { return target.GetType().GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(target, args); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
    }
    private static T Field<T>(object target, string field) => (T)target.GetType().GetField(field, BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(target)!;
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Equal<T>(T expected, T actual, string message) { if (!Equals(expected, actual)) throw new Exception($"{message}: expected {expected}, got {actual}"); }
    private static void Collect() { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); }
    private sealed class TestComponent : UiComponent
    {
        internal string Version = "A";
        internal readonly List<string> Trace = [];
        internal Action<UiEvent>? Blur;
        internal void Invalidate() => StateHasChanged();
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            var version = Version;
            writer.WriteLiteral("<style>body{margin:0}button{display:block;width:80px;height:40px;cursor:pointer}</style><body><div id='parent'");
            writer.WriteEventAttribute("onblur", () => Trace.Add("ancestor:blur"));
            writer.WriteLiteral(">");
            for (var i = 0; i < 7; i++)
            {
                var index = i;
                writer.WriteLiteral($"<button id='n{i}' type='button'");
                writer.WriteEventAttribute("onblur", e => { Trace.Add($"{version}:{index}:{e.Target.GetAttributeCore("id")}"); Blur?.Invoke(e); });
                writer.WriteLiteral(">button</button>");
            }
            writer.WriteLiteral("</div></body>");
        }
    }
    private sealed class TestRenderer : IRenderBackend
    {
        internal int Frames;
        public void Resize(int width, int height, float dpiScale) { }
        public void Render(DisplayList displayList, ColorRgba color) => Frames++;
        public void Dispose() { }
    }
}
#endif
