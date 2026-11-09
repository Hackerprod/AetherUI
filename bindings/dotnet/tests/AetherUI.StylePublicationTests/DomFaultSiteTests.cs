#if AETHER_DOM_FAULT_TESTING
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

internal static class DomFaultSiteTests
{
    private static readonly string[] Families = ["import-owners", "implicit-document-many", "materialize-opaque-capacity", "cross-owner-many", "legacy-parse", "legacy-reparse", "query-selector", "insert-many", "materialize-many", "claim-owner", "claim-wrapper", "claim-acquire", "claim-cleanup", "activate-binding", "add-class", "remove-class", "query-id", "query-tag", "read-text", "descendants", "ancestors", "attribute-enumerator", "child-enumerator", "state", "runtime", "attribute", "remove-attribute", "text", "replace-text", "insert", "move", "cross-owner", "remove", "clear", "materialize", "implicit-document", "import-attribute", "import-insert", "import-then-state", "import-then-attribute", "import-then-insert"];
    internal static void Run()
    {
        Controls();
        VisualDifferenceControl();
        var rows = new List<string> { "family\tsite\toccurrence\tboundary\treached" };
        var families = Families.Where(f => Environment.GetEnvironmentVariable("AETHER_DOM_FAULT_FAMILY") is not { Length: > 0 } filter || f == filter);
        var failures = 0;
        foreach (var family in families)
        {
            Collect();
            var baseline = ManagedDomFault.Resources;
            Check(baseline.Valid, "Resource ledger invalid before fixture");
            string? importedMetadata = null;
            if (family.StartsWith("import-", StringComparison.Ordinal))
            {
                using var expected = new Fixture(family);
                expected.Source.BindingCore!.EnsureCurrent(); expected.Target.BindingCore!.EnsureCurrent();
                importedMetadata = expected.PublicationMetadata();
            }
            Collect();
            var successVisual = ExpectedVisual(family, applyOperation: true);
            var recoveryVisual = ExpectedVisual(family, applyOperation: false);
            Collect();
            var trace = RunCase(family, null, 0, importedMetadata, successVisual, recoveryVisual).Trace;
            Collect(); Equal(baseline, ManagedDomFault.Resources, family + " baseline resource teardown");
            var occurrences = new Dictionary<string, int>(StringComparer.Ordinal);
            foreach (var site in trace)
            {
                occurrences.TryGetValue(site, out var previous); var occurrence = previous + 1; occurrences[site] = occurrence;
                var result = RunCase(family, site, occurrence, importedMetadata, successVisual, recoveryVisual);
                Collect(); Equal(baseline, ManagedDomFault.Resources, family + "/" + site + " resource teardown");
                rows.Add($"{family}\t{site}\t{occurrence}\t{result.Boundary}\ttrue"); failures++;
            }
            Console.WriteLine($"PASS fault sites {family}: {trace.Length} occurrences / {occurrences.Count} distinct source operations");
        }
        if (Environment.GetEnvironmentVariable("AETHER_DOM_FAULT_TRACE") is { Length: > 0 } output) File.WriteAllLines(output, rows);
        Console.WriteLine($"PASS {failures} managed operation-site failures, fresh fixture per site occurrence; not a CLR/BCL allocator replacement");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void VisualDifferenceControl()
    {
        using var fixture = new Fixture("state");
        var before = fixture.VisualImage(true);
        fixture.Element.SetAttribute("style", "width:109px;color:red");
        var changed = fixture.VisualImage(true);
        Check(before.Inputs != changed.Inputs && before.Projection != changed.Projection, "visual control failed to change typed inputs and projected style/layout");
        Throws<Exception>(() => Equal(before, changed, "deliberately changed real twin"));
    }
    private sealed record Visual(string Inputs, string Projection);
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static Visual ExpectedVisual(string family, bool applyOperation)
    {
        using var fixture = new Fixture(family);
        if (applyOperation) fixture.Operation();
        fixture.Source.BindingCore!.EnsureCurrent(); fixture.Target.BindingCore!.EnsureCurrent();
        return fixture.VisualImage(true);
    }
    private sealed record Outcome(string[] Trace, string Boundary);
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static Outcome RunCase(string family, string? selected, int occurrence, string? importedMetadata, Visual successVisual, Visual recoveryVisual)
    {
        using var fixture = new Fixture(family);
        var before = fixture.ManagedImage();
        var sourceToken = fixture.Source.BindingCore!.TokenCore;
        var targetToken = fixture.Target.BindingCore!.TokenCore;
        var native = fixture.NativeImage();
        var hostSerial = fixture.SourceHost.HostInputSerial;
        var targetSerial = fixture.TargetHost.HostInputSerial;
        var notify = fixture.Notifications;
        var frames = fixture.SourceRenderer.Frames + fixture.TargetRenderer.Frames;
        var resources = ManagedDomFault.Resources;
        var session = selected is null ? ManagedDomFault.Arm() : ManagedDomFault.ArmSite(selected, occurrence);
        Exception? error = null;
        try { fixture.Operation(); }
        catch (Exception exception) { error = exception; }
        finally { session.Dispose(); }
        var trace = session.Sites.Take(session.Count).ToArray();
        if (selected is null)
        {
            if (error is not null) throw new Exception("Recording operation failed: " + family, error);
            fixture.AssertNativeMirror();
            Equal(successVisual, fixture.VisualImage(false), "success current inputs and FULL style/static layout twin");
            return new(trace, "success");
        }
        Check(ReferenceEquals(error, session.Failure), $"{family}/{selected}/{occurrence} did not throw its selected sentinel: {error}");
        Equal(native, fixture.NativeImage(), "native current inputs/slot changed after failure");
        Equal(notify, fixture.Notifications, "failure/import invoked user notifications");
        Equal(frames, fixture.SourceRenderer.Frames + fixture.TargetRenderer.Frames, "failure painted a new frame");
        var importPublished = sourceToken != fixture.Source.BindingCore!.TokenCore || targetToken != fixture.Target.BindingCore!.TokenCore;
        string boundary;
        if (importPublished)
        {
            boundary = "13.2";
            Equal(importedMetadata, fixture.PublicationMetadata(), "import versions and host bookkeeping");
            Check(family.StartsWith("import-", StringComparison.Ordinal), "Unexpected import publication in current fixture");
            fixture.AssertNativeMirror();
            // Host serial may advance for the legitimate R2 import, but the requested
            // edit must not install its new value, links, identity or notification.
            Check(fixture.Element.GetAttributeCore("attempt") is null, "requested attribute committed after injected failure");
            Check(fixture.NewChild.ParentCore is null, "requested child committed after injected failure");
        }
        else
        {
            boundary = family.StartsWith("import-", StringComparison.Ordinal) ? "13.1" : "13.3";
            Equal(before, fixture.ManagedImage(), "managed base changed before allowed publication");
            Equal(hostSerial, fixture.SourceHost.HostInputSerial, "source host serial changed");
            Equal(targetSerial, fixture.TargetHost.HostInputSerial, "target host serial changed");
            Equal(resources, ManagedDomFault.Resources, "owned staging handles escaped failed operation");
            if (boundary == "13.1")
                Check(!fixture.Source.BindingCore!.HasCurrentInputs, "failed import certified old mirror as current");
        }
        // A distinct explicit operation after disarming is permitted recovery, not
        // an automatic retry by the failed call. It must recover from current R2.
        fixture.Source.BindingCore!.EnsureCurrent();
        fixture.Target.BindingCore!.EnsureCurrent();
        fixture.AssertNativeMirror();
        Equal(recoveryVisual, fixture.VisualImage(false), "recovery current inputs and FULL style/static layout twin");
        return new(trace, boundary);
    }
    private sealed class Fixture : IDisposable
    {
        internal readonly Renderer SourceRenderer = new(), TargetRenderer = new();
        internal readonly UiDocumentHost SourceHost, TargetHost;
        internal readonly DomInterop Source, Target;
        internal readonly DomInterop? Unbound;
        private readonly List<DomInterop> _extraOwners = [];
        private NativeDomDocument? _legacy;
        private NativeInteractionOwner? _preparedOwner;
        private NativeInteractionClaim? _claim;
        private NativeInteractionOwner InteractionOwner => Field<NativeInteractionOwner>(Field<object>(SourceHost, "_interactionSession"), "Owner");
        internal readonly ElementNode Element, Parent, Other, NewChild;
        internal readonly DocumentNode Implicit = new();
        internal readonly ElementNode UnboundParent = new("section");
        internal readonly TextNode Text;
        internal readonly string Family;
        internal int Notifications;
        internal Fixture(string family)
        {
            Family = family;
            if (family == "legacy-reparse") _legacy = NativeDomDocument.Parse("<div>before</div>");
            SourceHost = new(new Component(), new ApproximateTextMeasurer(), SourceRenderer, new UiHostOptions { Width = 200, Height = 200 });
            TargetHost = new(new Component(), new ApproximateTextMeasurer(), TargetRenderer, new UiHostOptions { Width = 200, Height = 200 });
            SourceHost.Render(); TargetHost.Render();
            Source = Field<DomInterop>(SourceHost, "_nativeDom"); Target = Field<DomInterop>(TargetHost, "_nativeDom");
            Element = Source.Document.GetElementById("i")!; Parent = Source.Document.GetElementById("parent")!;
            Other = Source.Document.GetElementById("other")!; Text = (TextNode)Source.Document.GetElementById("text")!.FirstChild!;
            if (family is "activate-binding" or "claim-owner" or "claim-wrapper") Unbound = DomInterop.Parse("<div><input value='base'></div>");
            if (family is "add-class" or "remove-class") { Element.SetAttribute("class", "one two"); SourceHost.Render(); }
            NewChild = new ElementNode("ASIDE"); NewChild.SetAttribute("id", "new"); NewChild.AppendChild(new TextNode("new text"));
            UnboundParent.AppendChild(new TextNode("prefix")); Implicit.AppendChild(new ElementNode("prefix"));
            if (family == "materialize-opaque-capacity")
                for (var i = 0; i < 2; i++) UnboundParent.AppendChild(new ElementNode("x"));
            if (family is "cross-owner-many" or "implicit-document-many")
            {
                for (var i = 0; i < 40; i++) Parent.AppendChild(new ElementNode("x"));
                SourceHost.Render();
            }
            if (family is "insert-many" or "materialize-many")
                for (var i = 0; i < 40; i++) (family == "insert-many" ? NewChild : UnboundParent).AppendChild(new ElementNode("x"));
            if (family == "claim-wrapper")
            {
                var input = Unbound!.Document.GetElementsByTagName("input").First();
                Unbound.Native.SetState(Unbound.GetNativeHandle(input), (int)ElementState.Focus);
                _preparedOwner = NativeInteractionOwner.Prepare(Unbound.Native, 100000);
            }
            if (family == "claim-cleanup")
            {
                using var edit = Source.BindingCore!.PrepareInteractionAcquire(InteractionOwner, Element, ElementState.Focus, 1);
                edit.Commit(); _claim = edit.Claim;
            }
            SourceHost.RenderRequested += (_, _) => Notifications++;
            TargetHost.RenderRequested += (_, _) => Notifications++;
            if (family.StartsWith("import-", StringComparison.Ordinal))
                Source.Native.SetAttribute(Source.GetNativeHandle(Element), "r2", "current native");
            if (family == "import-owners")
                for (var i = 0; i < 4; i++)
                {
                    var extra = DomInterop.Parse("<body><div id='extra'></div></body>");
                    _extraOwners.Add(extra); extra.EnableSynchronization(new EmptyBookkeeping());
                    extra.Native.SetAttribute(extra.GetNativeHandle(extra.Document.GetElementById("extra")!), "r2", "current native");
                }
            if (family == "import-insert")
            {
                using var borrow = Source.Native.Borrow();
                using var edit = NativeDomEdit.Prepare(NativeDomSync.ReadToken(Source.Native), new(NativeDomEditKind.InsertSubtree,
                    Source.GetNativeHandle(Parent), Nodes: new[] { new NativeDomNodeSpecData((int)NodeType.Element, 0, -1, "added-native", "", null, new Dictionary<string, string>()) }));
                edit.Commit();
            }
        }
        internal void Operation()
        {
            switch (Family)
            {
                case "legacy-parse": _legacy = NativeDomDocument.Parse("<div>after</div>"); break;
                case "legacy-reparse": _legacy!.Reparse("<div>after</div>"); break;
                case "query-selector": _ = Source.Document.QuerySelector("  #i  "); break;
                case "claim-owner": _preparedOwner = NativeInteractionOwner.Prepare(Unbound!.Native, 100000); break;
                case "claim-wrapper": _claim = _preparedOwner!.PrepareClaim(Unbound!.GetNativeHandle(Unbound.Document.GetElementsByTagName("input").First()), (int)ElementState.Focus, 1); break;
                case "claim-acquire":
                    using (var edit = Source.BindingCore!.PrepareInteractionAcquire(InteractionOwner, Element, ElementState.Focus, 1)) { edit.Commit(); _claim = edit.Claim; }
                    break;
                case "claim-cleanup":
                    using (var edit = Source.BindingCore!.PrepareInteractionCleanup(Element, _claim!, _claim!.Read().Generation)) edit.Commit();
                    break;
                case "activate-binding": Unbound!.EnableSynchronization(new EmptyBookkeeping()); break;
                case "add-class": Element.AddClass("three"); break;
                case "remove-class": Element.RemoveClass("two"); break;
                case "query-id": _ = Target.Document.GetElementById("i"); break;
                case "query-tag": _ = Source.Document.GetElementsByTagName("INPUT"); break;
                case "read-text": _ = Parent.TextContent; break;
                case "descendants": _ = Parent.Descendants(); _ = Parent.DescendantElements(); break;
                case "ancestors": _ = Element.AncestorsAndSelf(); break;
                case "attribute-enumerator": using (var e = Element.Attributes.GetEnumerator()) { while (e.MoveNext()) { } } break;
                case "child-enumerator": using (var e = Parent.Children.GetEnumerator()) { while (e.MoveNext()) { } } break;
                case "state": Element.State = ElementState.Invalid; break;
                case "runtime": Element.RuntimeValue = "requested"; break;
                case "attribute": Element.SetAttribute("DATA-NEW", "requested"); break;
                case "remove-attribute": Element.RemoveAttribute("value"); break;
                case "text": Text.TextContent = "requested"; break;
                case "replace-text": Other.TextContent = "requested"; break;
                case "insert-many": case "insert": Parent.AppendChild(NewChild); break;
                case "move": Other.AppendChild(Element); break;
                case "cross-owner-many": Target.Document.GetElementById("parent")!.AppendChild(Parent); break;
                case "cross-owner": Target.Document.GetElementById("parent")!.AppendChild(Element); break;
                case "remove": Parent.RemoveChild(Element); break;
                case "clear": Parent.ClearChildren(); break;
                case "materialize-opaque-capacity": case "materialize-many": case "materialize": UnboundParent.AppendChild(Element); break;
                case "implicit-document-many": Implicit.AppendChild(Parent); break;
                case "implicit-document": Implicit.AppendChild(Element); break;
                case "import-owners": case "import-attribute": case "import-insert": _ = Element.State; break;
                case "import-then-state": Element.State = ElementState.Focus; break;
                case "import-then-attribute": Element.SetAttribute("attempt", "requested"); break;
                case "import-then-insert": Parent.AppendChild(NewChild); break;
                default: throw new Exception("Unknown family");
            }
        }
        internal string NativeImage()
        {
            return string.Join("\n", _extraOwners.Select(e => NativeTree(e.Native))) + (_legacy is null ? "" : NativeTree(_legacy)) + NativeTree(Source.Native) + "\nDEST\n" + NativeTree(Target.Native) + (Unbound is null ? "" : NativeTree(Unbound.Native)) + "\nSLOTS\n" +
                Field<StyleRuntimeInterop>(SourceHost, "_nativeStyleRuntime").ReadDomSourceSlot() + "\n" +
                Field<StyleRuntimeInterop>(TargetHost, "_nativeStyleRuntime").ReadDomSourceSlot();
        }
        internal Visual VisualImage(bool fullProjection)
        {
            var inputs = new StringBuilder();
            Capture(Source.Document); Capture(Target.Document);
            var projection = Render(SourceHost) + "\nDEST\n" + Render(TargetHost);
            return new(inputs.ToString(), projection);
            void Capture(Node node)
            {
                inputs.Append('(').Append(node.NodeType);
                if (node is ElementNode element)
                {
                    inputs.Append('/').Append(element.TagNameCore).Append('/').Append(element.StateCore).Append('/').Append(Encode(element.RuntimeValueCore));
                    foreach (var pair in element.AttributesCore.OrderBy(p => p.Key, StringComparer.Ordinal)) inputs.Append('/').Append(Encode(pair.Key)).Append('=').Append(Encode(pair.Value));
                }
                if (node is TextNode text) inputs.Append('/').Append(Encode(text.TextCore));
                if (node is CommentNode comment) inputs.Append('/').Append(Encode(comment.TextCore));
                foreach (var child in node.ChildrenCore) Capture(child);
                inputs.Append(')');
            }
            string Render(UiDocumentHost host)
            {
                var type = typeof(UiDocumentHost);
                if (fullProjection) type.GetField("_forceFullProjectionRecovery", BindingFlags.NonPublic | BindingFlags.Instance)!.SetValue(host, true);
                var invalidation = type.GetNestedType("VisualInvalidation", BindingFlags.NonPublic)!;
                if (fullProjection) type.GetMethod("ProduceInvalidation", BindingFlags.NonPublic | BindingFlags.Instance)!.Invoke(host, [Enum.ToObject(invalidation, 1)]);
                var before = Field<long>(host, "_layoutBuildCount");
                host.Render();
                if (fullProjection) Check(Field<long>(host, "_layoutBuildCount") > before, "twin reused stale layout cache");
                var result = new StringBuilder();
                Box(host.Layout!.Root);
                return result.ToString();
                void Box(LayoutBox box)
                {
                    result.Append('(').Append(box.Kind).Append('/').Append(box.Element.TagNameCore).Append('/').Append(Encode(box.Element.GetAttributeCore("id")))
                        .Append('/').Append(Encode(box.Text)).Append('/').Append(box.MarginBox).Append('/').Append(box.BorderBox)
                        .Append('/').Append(box.PaddingBox).Append('/').Append(box.ContentBox).Append('/').Append(box.Scroll).Append('/').Append(box.Scrollbar)
                        .Append('/').Append(box.IsAtomicControl).Append('/').Append(System.Text.Json.JsonSerializer.Serialize(box.Style));
                    foreach (var child in box.Children) Box(child);
                    result.Append(')');
                }
            }
        }
        internal string PublicationMetadata()
        {
            return Owner(SourceHost, Source.Document) + "\n" + Owner(TargetHost, Target.Document) + "\n" + string.Join(";", _extraOwners.Select(e => $"{e.Document.Version}/{e.Document.InputVersion}"));
            static string Owner(UiDocumentHost host, DocumentNode document)
            {
                var result = new StringBuilder().Append(document.Version).Append('/').Append(document.InputVersion).Append('/').Append(host.HostInputSerial);
                foreach (var name in new[] { "_hostPublicationSerial", "_invalidation", "_managedProjectionOutOfDate", "_styleRetryBlocked", "_forceFullProjectionRecovery", "_hasPendingPointerTarget", "_domNotificationPending" })
                    result.Append('/').Append(Field<object>(host, name));
                result.Append('/').AppendJoin(',', Field<long[]>(host, "_invalidationProduced"));
                foreach (var element in Field<HashSet<ElementNode>>(host, "_stateDirtyElements").OrderBy(n => n.GetAttributeCore("id")))
                    result.Append('/').Append(element.GetAttributeCore("id"));
                foreach (var entry in Field<Dictionary<string, string>>(host, "_uncontrolledInputValues").OrderBy(p => p.Key))
                    result.Append('/').Append(Encode(entry.Key)).Append('=').Append(Encode(entry.Value));
                return result.ToString();
            }
        }
        internal string ManagedImage()
        {
            var result = new StringBuilder(); var seen = new HashSet<Node>(ReferenceEqualityComparer.Instance);
            foreach (var extra in _extraOwners) Capture(extra.Document);
            if (Unbound is not null) Capture(Unbound.Document);
            Capture(Source.Document); Capture(Target.Document); Capture(Element); Capture(Text); Capture(NewChild); Capture(UnboundParent); Capture(Implicit);
            return result.ToString();
            void Capture(Node node)
            {
                if (!seen.Add(node)) return;
                result.Append(RuntimeHelpers.GetHashCode(node)).Append('/').Append(node.NodeType).Append('/').Append(node.ParentCore is null ? 0 : RuntimeHelpers.GetHashCode(node.ParentCore));
                if (node is DocumentNode doc) result.Append('/').Append(doc.Version).Append('/').Append(doc.InputVersion);
                if (node is ElementNode element)
                {
                    result.Append('/').Append(element.TagNameCore).Append('/').Append((int)element.StateCore).Append('/').Append(Encode(element.RuntimeValueCore));
                    foreach (var pair in element.AttributesCore.OrderBy(p => p.Key, StringComparer.Ordinal)) result.Append('/').Append(Encode(pair.Key)).Append('=').Append(Encode(pair.Value));
                }
                else if (node is TextNode text) result.Append('/').Append(Encode(text.TextCore));
                else if (node is CommentNode comment) result.Append('/').Append(Encode(comment.TextCore));
                if (node.NativeRecordCore is { } record)
                    result.Append('/').Append(record.Identity).Append('/').Append(record.Owner.TokenCore.OwnerIdentity).Append('/').Append(record.DisconnectSerial).Append('/').Append(record.Attached)
                        .Append('/').Append(record.StyleNodeId).Append('/').AppendJoin(',', record.StateBitWriteSerial);
                foreach (var child in node.ChildrenCore) result.Append('>').Append(RuntimeHelpers.GetHashCode(child));
                result.AppendLine(); foreach (var child in node.ChildrenCore) Capture(child);
            }
        }
        internal void AssertNativeMirror()
        {
            CheckOwner(Source.BindingCore!); CheckOwner(Target.BindingCore!);
            foreach (var auxiliary in _extraOwners) CheckOwner(auxiliary.BindingCore!);
            if (Implicit.BindingCore is NativeDomBinding extra) CheckOwner(extra);
            if (Unbound?.BindingCore is { } activated) CheckOwner(activated);
            void CheckOwner(NativeDomBinding binding)
            {
                using var snapshot = NativeDomSnapshot.Acquire(binding.Native);
                Equal(snapshot.Token, binding.TokenCore, "mirror watermark");
                foreach (var data in snapshot.Nodes)
                {
                    if (!data.Alive) continue;
                    var node = binding.GetNodeCore(data.Handle);
                    Check(node is not null, "missing live wrapper");
                    Equal(data.Identity, node!.NativeRecordCore!.Identity, "identity");
                    Equal(binding, node.BindingCore, "binding identity");
                    Equal(binding, node.NativeRecordCore.Owner, "record owner binding");
                    Equal(data.OwnerIdentity, node.NativeRecordCore.Owner.TokenCore.OwnerIdentity, "record owner identity");
                    Equal(data.StyleNodeId, node.NativeRecordCore.StyleNodeId, "style node identity");
                    Check(data.StateBitWriteSerial.SequenceEqual(node.NativeRecordCore.StateBitWriteSerial), "per-bit write serials");
                    Equal(data.DisconnectSerial, node.NativeRecordCore.DisconnectSerial, "continuity");
                    Equal(data.Attached, node.NativeRecordCore.Attached, "attachment");
                    Equal(data.ParentIndex < 0 ? null : binding.GetNodeCore(snapshot.Nodes[(int)data.ParentIndex].Handle), node.ParentCore, "parent");
                    Check(data.Children.Select(i => binding.GetNodeCore(snapshot.Nodes[i].Handle)).SequenceEqual(node.ChildrenCore), "children reciprocity");
                    if (node is ElementNode element)
                    {
                        Equal(data.Tag, element.TagNameCore, "tag"); Equal(data.State, (int)element.StateCore, "state"); Equal(data.RuntimeValue, element.RuntimeValueCore, "runtime");
                        Check(data.Attributes.OrderBy(p => p.Key).SequenceEqual(element.AttributesCore.OrderBy(p => p.Key)), "authored attributes");
                    }
                    else if (node is TextNode text) Equal(data.Text, text.TextCore, "text");
                    else if (node is CommentNode comment) Equal(data.Text, comment.TextCore, "comment");
                }
            }
        }
        public void Dispose()
        {
            if (Implicit.BindingCore is NativeDomBinding extra) { extra.Dispose(); extra.Native.Dispose(); }
            _claim?.Dispose(); _preparedOwner?.Dispose(); _legacy?.Dispose();
            foreach (var auxiliary in _extraOwners) auxiliary.Dispose();
            Unbound?.Dispose();
            SourceHost.Dispose(); TargetHost.Dispose(); SourceRenderer.Dispose(); TargetRenderer.Dispose();
        }
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static void RunCold(string site)
    {
        var baseline = ManagedDomFault.Resources;
        ColdCase(site);
        Collect(); Equal(baseline, ManagedDomFault.Resources, "cold activation resource teardown");
        Console.WriteLine("PASS cold activation source site " + site);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void ColdCase(string site)
    {
        using var dom = DomInterop.Parse("<body><input id='cold' value='current'></body>");
        var native = NativeTree(dom.Native);
        var version = dom.Document.Version; var input = dom.Document.InputVersion;
        var session = ManagedDomFault.ArmSite(site, 1);
        Exception? error = null;
        try { dom.EnableSynchronization(new EmptyBookkeeping()); }
        catch (Exception exception) { error = exception; }
        finally { session.Dispose(); }
        Check(ReferenceEquals(error, session.Failure), "cold target sentinel");
        Check(dom.BindingCore is null, "failed cold activation published binding");
        Equal(native, NativeTree(dom.Native), "cold activation native inputs");
        Equal(version, dom.Document.Version, "cold activation Version");
        Equal(input, dom.Document.InputVersion, "cold activation InputVersion");
        dom.EnableSynchronization(new EmptyBookkeeping());
        Check(dom.BindingCore!.HasCurrentInputs, "cold activation explicit retry");
    }
    internal static void CounterOverflowControl()
    {
        typeof(ManagedDomFault).GetField("_buffers", BindingFlags.Static | BindingFlags.NonPublic)!.SetValue(null, long.MaxValue);
        ManagedDomFault.ResourceAcquired(ManagedDomFault.Resource.NativeBuffer);
        Check(!ManagedDomFault.Resources.Valid, "Resource overflow did not invalidate ledger");
        Console.WriteLine("PASS isolated resource overflow invalidity control");
    }
    private sealed class EmptyBookkeeping : IDomHostBookkeeping
    {
        public IDomPreparedBookkeeping Prepare(DomEdit? edit, bool import) => new EmptyPrepared();
        public void NotifyMutationCompleted() { }
        private sealed class EmptyPrepared : IDomPreparedBookkeeping
        {
            public void Validate() { }
            public void Publish() { }
            public void Dispose() { }
        }
    }
    private static string NativeTree(NativeDomDocument document)
    {
        using var snapshot = NativeDomSnapshot.Acquire(document);
        var result = new StringBuilder().Append(snapshot.Token).AppendLine();
        foreach (var node in snapshot.Nodes.OrderBy(n => n.Identity))
        {
            result.Append(node.Identity).Append('/').Append(node.OwnerIdentity).Append('/').Append(node.DisconnectSerial).Append('/').Append(node.Type).Append('/').Append(node.State)
                .Append('/').Append(node.Alive).Append('/').Append(node.Attached).Append('/').Append(node.StyleNodeId).Append('/').Append(Encode(node.Tag)).Append('/').Append(Encode(node.Text))
                .Append('/').Append(Encode(node.AuthoredValue)).Append('/').Append(Encode(node.RuntimeValue)).Append('/').AppendJoin(',', node.StateBitWriteSerial)
                .Append('/').Append(node.ParentIndex < 0 ? 0 : snapshot.Nodes[(int)node.ParentIndex].Identity);
            foreach (var pair in node.Attributes.OrderBy(p => p.Key, StringComparer.Ordinal)) result.Append('/').Append(Encode(pair.Key)).Append('=').Append(Encode(pair.Value));
            foreach (var child in node.Children) result.Append('>').Append(snapshot.Nodes[child].Identity);
            result.AppendLine();
        }
        return result.ToString();
    }
    private static string Encode(string? value) => value is null ? "NULL" : Convert.ToBase64String(Encoding.UTF8.GetBytes(value));
    private static void Controls()
    {
        Throws<Exception>(() => Equal(new Visual("inputs", "layoutA"), new Visual("inputs", "layoutB"), "deliberately distinct twin"));
        ManagedDomFault.Before("unarmed-control");
        var s = ManagedDomFault.Arm(2); ManagedDomFault.Before("one");
        try { ManagedDomFault.Before("two"); throw new Exception("missing sentinel"); } catch (OutOfMemoryException e) { Check(ReferenceEquals(e, s.Failure), "wrong sentinel"); }
        s.Dispose(); Equal(2, s.Count, "trace ordinal");
        using (var active = ManagedDomFault.Arm())
        { Throws<InvalidOperationException>(() => ManagedDomFault.Arm()); _ = new byte[19]; Equal(0, active.Count, "unrelated CLR allocation counted"); }
        var missing = ManagedDomFault.Arm(1); Throws<InvalidOperationException>(missing.Dispose);
        var full = ManagedDomFault.Arm(0, 1); ManagedDomFault.Before("one"); Throws<InvalidOperationException>(() => ManagedDomFault.Before("overflow")); Throws<InvalidOperationException>(full.Dispose);
    }
    private static void Collect() { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); }
    private static T Field<T>(object target, string name) => (T)target.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(target)!;
    private static void Check(bool value, string label) { if (!value) throw new Exception(label); }
    private static void Equal<T>(T expected, T actual, string label) { if (!EqualityComparer<T>.Default.Equals(expected, actual)) throw new Exception(label + ": expected " + expected + ", actual " + actual); }
    private static void Throws<T>(Action action) where T : Exception { try { action(); } catch (T) { return; } throw new Exception("Expected " + typeof(T).Name); }
    private sealed class Component : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral("<style id='css'>body{margin:0} input{width:80px;height:20px} input:focus{width:99px} input:invalid{height:24px}</style><body><div id='parent'><input id='i' value='base'><span id='text'>text</span></div><div id='other'></div></body>");
    }
    private sealed class Renderer : IRenderBackend
    {
        internal int Frames;
        public void Resize(int width, int height, float dpi) { }
        public void Render(DisplayList list, ColorRgba color) => Frames++;
        public void Dispose() { }
    }
}
#endif
