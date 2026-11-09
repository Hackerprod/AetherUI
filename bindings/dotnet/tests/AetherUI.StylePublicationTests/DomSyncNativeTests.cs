using AetherUI.Core.Native;
using AetherUI.Dom;

internal static class DomSyncNativeTests
{
    internal static void Run()
    {
        NativeR2AndTypedWrites();
        TypedNonRoundtripAndDomain();
        AdoptionAndRetirement();
        MaterializeAroundCurrentSubtree();
        FinalizableOwnerBorrows();
        ImplicitDocumentAdoption();
        ImplicitDocumentFailures();
        ImplicitDocumentLifetime();
        ConnectedToBoundDetachedParent();
        Console.WriteLine("PASS native DOM adapter R2, typed inputs, ownership and retirement");
    }
    private static void NativeR2AndTypedWrites()
    {
        using var dom = DomInterop.Parse("<html><body><input id='a' value='base'></body></html>");
        var host = new Bookkeeping(); var binding = dom.EnableSynchronization(host);
        var input = dom.Document.GetElementById("a")!;
        var handle = dom.GetNativeHandle(input);
        var callbackCount = host.Notifications;
        dom.Native.SetState(handle, (int)ElementState.Invalid);
        input.State |= ElementState.Hover;
        Equal(ElementState.Invalid | ElementState.Hover, (ElementState)dom.Native.GetState(handle));
        Equal(callbackCount + 1, host.Notifications);
        input.RuntimeValue = "override"; input.SetAttribute("value", "edited base");
        Equal("edited base", input.GetAttribute("value")!); Equal("override", input.RuntimeValue!);
        dom.Native.SetAttribute(handle, "value", "native R2");
        callbackCount = host.Notifications;
        Equal("native R2", input.GetAttribute("value")!); True(input.RuntimeValue is null);
        Equal(callbackCount, host.Notifications); // Imports never notify user.
        input.RuntimeValue = null; Equal("native R2", input.GetAttribute("value")!);
        dom.Native.SetAttribute(handle, "r2", "preserved");
        host.FailPrepare = true;
        Throws<OutOfMemoryException>(() => input.SetAttribute("attempt", "no"));
        host.FailPrepare = false;
        Equal("preserved", input.GetAttribute("r2")!); True(!input.HasAttribute("attempt"));
        var token = binding.TokenCore;
        input.State = input.State; input.RuntimeValue = input.RuntimeValue;
        Equal(token, binding.TokenCore);
    }
    private static void TypedNonRoundtripAndDomain()
    {
        using var dom = DomInterop.Parse("<div id='a'></div>"); dom.EnableSynchronization(new Bookkeeping());
        var root = dom.Document.GetElementById("a")!;
        var input = new ElementNode("INPUT");
        input.AppendChild(new TextNode("")); input.AppendChild(new TextNode("one")); input.AppendChild(new TextNode("two"));
        var comment = new CommentNode("<raw>"); comment.AppendChild(new TextNode("comment child")); input.AppendChild(comment);
        input.SetAttribute("É", "upper"); input.SetAttribute("é", "lower"); input.RuntimeValue = "";
        root.AppendChild(input);
        Equal(4, input.Children.Count); Equal("", ((TextNode)input.Children[0]).TextContent);
        Equal("upper", input.GetAttribute("É")!); Equal("lower", input.GetAttribute("é")!);
        using (var snapshot = NativeDomSnapshot.Acquire(dom.Native.Handle))
        {
            var nativeInput = snapshot.Nodes.Single(node => node.Handle == dom.GetNativeHandle(input));
            Equal(4, nativeInput.Children.Length); Equal("", nativeInput.RuntimeValue!);
        }
        var v = dom.Document.InputVersion;
        Throws<ArgumentException>(() => input.SetAttribute("bad", "\0"));
        Throws<System.Text.EncoderFallbackException>(() => input.RuntimeValue = "\ud800");
        Equal(v, dom.Document.InputVersion);
        var malformed = new ElementNode("raw"); malformed.AppendChild(new TextNode("\ud800"));
        Throws<System.Text.EncoderFallbackException>(() => root.AppendChild(malformed)); True(malformed.Parent is null);
        Throws<InvalidOperationException>(() => root.AppendChild(new DocumentNode()));
        Throws<InvalidOperationException>(() => root.AppendChild(new CustomNode()));
        var children = root.Children; var e = children.GetEnumerator(); root.TextContent = "replacement";
        True(e.MoveNext()); True(ReferenceEquals(input, e.Current)); True(!e.MoveNext()); Equal(1, children.Count);
        True(input.OwnerDocument is null); True(input.BindingCore is not null);
        dom.Native.SetAttribute(dom.GetNativeHandle(input), "detached", "native"); Equal("native", input.GetAttribute("detached")!);
        root.AppendChild(input); Equal("native", input.GetAttribute("detached")!);
    }
    private static void AdoptionAndRetirement()
    {
        using var left = DomInterop.Parse("<div id='left'><span id='keep'>old</span></div>");
        using var right = DomInterop.Parse("<div id='right'></div>");
        left.EnableSynchronization(new Bookkeeping()); right.EnableSynchronization(new Bookkeeping());
        var source = left.Document.GetElementById("left")!; var keep = left.Document.GetElementById("keep")!;
        var target = right.Document.GetElementById("right")!;
        var identity = keep.NativeRecordCore!.Identity;
        target.AppendChild(keep);
        Equal(identity, keep.NativeRecordCore!.Identity); True(ReferenceEquals(keep.OwnerDocument, right.Document)); Equal(0, source.Children.Count);
        Equal("old", keep.TextContent); True(ReferenceEquals(right.GetManagedElement(right.GetNativeHandle(keep)), keep));
        // Move back with native typed edit, then discover from the destination first.
        using (var edit = NativeDomEdit.Prepare(NativeDomSync.ReadToken(left.Native.Handle),
            new(NativeDomEditKind.InsertChild, left.GetNativeHandle(source), right.GetNativeHandle(keep)))) edit.Commit();
        True(ReferenceEquals(left.Document.GetElementById("keep"), keep)); Equal(0, target.Children.Count);
        left.Native.SetAttribute(left.GetNativeHandle(keep), "final", "before retire");
        left.Native.Reparse("<div id='new'></div>");
        Equal("before retire", keep.GetAttribute("final")!); True(keep.BindingCore is null); True(keep.OwnerDocument is null);
        var replacement = left.Document.GetElementById("new")!; replacement.AppendChild(keep);
        True(keep.NativeRecordCore!.Identity != identity); Equal("before retire", keep.GetAttribute("final")!);
    }
    private static void MaterializeAroundCurrentSubtree()
    {
        using var dom = DomInterop.Parse("<div id='source'><input id='keep' value='base'></div>");
        var host = new Bookkeeping(); dom.EnableSynchronization(host);
        var source = dom.Document.GetElementById("source")!; var keep = dom.Document.GetElementById("keep")!;
        var identity = keep.NativeRecordCore!.Identity;
        var oldDisconnect = keep.NativeRecordCore.DisconnectSerial;
        dom.Native.SetAttribute(dom.GetNativeHandle(keep), "r2", "fresh");
        dom.Native.SetState(dom.GetNativeHandle(keep), (int)ElementState.Invalid);
        using (var edit = NativeDomEdit.Prepare(NativeDomSync.ReadToken(dom.Native.Handle), new(
            NativeDomEditKind.InsertSubtree, dom.GetNativeHandle(keep), Nodes:
            new[] { new NativeDomNodeSpecData((int)NodeType.Text, 0, -1, "", "native child", null, new Dictionary<string, string>()) }))) edit.Commit();
        var root = new ElementNode("outer"); var target = new ElementNode("target");
        root.AppendChild(new TextNode("prefix")); root.AppendChild(target); root.AppendChild(new CommentNode("suffix"));
        target.AppendChild(new TextNode("first"));
        target.InsertChild(0, keep);
        Equal(identity, keep.NativeRecordCore!.Identity);
        Equal(oldDisconnect + 1, keep.NativeRecordCore.DisconnectSerial);
        True(keep.OwnerDocument is null); True(root.BindingCore is not null); True(target.BindingCore is not null);
        Equal("fresh", keep.GetAttribute("r2")!); Equal(ElementState.Invalid, keep.State); Equal("native child", keep.TextContent);
        Equal(0, source.Children.Count); Equal(2, target.Children.Count); True(ReferenceEquals(keep, target.FirstChild));
        using (var snapshot = NativeDomSnapshot.Acquire(dom.Native.Handle))
        {
            var nativeRoot = snapshot.Nodes.Single(n => n.Identity == root.NativeRecordCore!.Identity);
            Equal(-1L, nativeRoot.ParentIndex); True(!nativeRoot.Attached); Equal(3, nativeRoot.Children.Length);
            var nativeKeep = snapshot.Nodes.Single(n => n.Identity == identity);
            Equal("fresh", nativeKeep.Attributes["r2"]); Equal(1, nativeKeep.Children.Length);
        }
        // A failed forest leaves all native and managed links unchanged, retaining R2.
        var failed = new ElementNode("new-target"); failed.AppendChild(new TextNode("\ud800"));
        Throws<System.Text.EncoderFallbackException>(() => failed.AppendChild(keep));
        True(ReferenceEquals(target, keep.Parent)); True(failed.BindingCore is null);
        var detachedTarget = new ElementNode("another"); oldDisconnect = keep.NativeRecordCore.DisconnectSerial;
        detachedTarget.AppendChild(keep);
        Equal(oldDisconnect, keep.NativeRecordCore.DisconnectSerial); // Detached move, same owner.
        True(ReferenceEquals(detachedTarget, keep.Parent));
    }
    private static void ImplicitDocumentAdoption()
    {
        using var source = DomInterop.Parse("<div id='source'><input id='keep' value='base'></div>");
        var host = new Bookkeeping(); source.EnableSynchronization(host);
        var oldParent = source.Document.GetElementById("source")!;
        var keep = source.Document.GetElementById("keep")!;
        keep.RuntimeValue = "old override";
        var identity = keep.NativeRecordCore!.Identity;
        source.Native.SetAttribute(source.GetNativeHandle(keep), "value", "R2");
        source.Native.SetAttribute(source.GetNativeHandle(keep), "onclick", "aether:abc");
        source.Native.SetState(source.GetNativeHandle(keep), (int)ElementState.Invalid);
        var document = new DocumentNode();
        var before = new TextNode(""); var ancestor = new ElementNode("section"); var after = new CommentNode("after");
        var target = new ElementNode("target"); ancestor.AppendChild(target); target.AppendChild(new TextNode("suffix"));
        document.AppendChild(before); document.AppendChild(ancestor); document.AppendChild(after);
        var version = document.Version; var sourceNotifications = host.Notifications; var destinationNotifications = 0;
        Action listener = () =>
        {
            destinationNotifications++;
            True(ReferenceEquals(keep.ParentCore, target)); Equal(0, oldParent.ChildrenCore.Length);
            Equal(version + 1, document.Version);
        };
        document.InputsChanged += listener;
        target.InsertChild(0, keep);
        document.InputsChanged -= listener;
        Equal(version + 1, document.Version); Equal(sourceNotifications + 1, host.Notifications); Equal(1, destinationNotifications);
        Equal(identity, keep.NativeRecordCore!.Identity); True(ReferenceEquals(document, keep.OwnerDocument));
        Equal(ElementState.Invalid, keep.State); Equal("R2", keep.GetAttribute("value")!); True(keep.RuntimeValue is null);
        Equal("aether:abc", keep.GetAttribute("onclick")!); Equal(3, document.Children.Count);
        True(ReferenceEquals(document.FirstChild, before)); True(ReferenceEquals(document.LastChild, after));
        var binding = (NativeDomBinding)document.BindingCore!;
        using (var snapshot = NativeDomSnapshot.Acquire(binding.Native))
        {
            var nativeRoot = snapshot.Nodes.Single(n => n.Identity == document.NativeRecordCore!.Identity);
            Equal(3, nativeRoot.Children.Length);
            var moved = snapshot.Nodes.Single(n => n.Identity == identity);
            True(moved.Attached); Equal("R2", moved.Attributes["value"]); Equal("aether:abc", moved.Attributes["onclick"]);
            Equal(ancestor.NativeRecordCore!.Owner, keep.NativeRecordCore.Owner);
        }
        // Root-target variant, preserving original siblings and another canonical identity.
        var next = new DocumentNode(); var prefix = new CommentNode("prefix"); next.AppendChild(prefix);
        next.AppendChild(ancestor);
        True(ReferenceEquals(next, keep.OwnerDocument)); True(ReferenceEquals(prefix, next.FirstChild));
        Equal(identity, keep.NativeRecordCore!.Identity); Equal(2, document.Children.Count);
        Equal(2, next.Children.Count);
    }
    private static void ImplicitDocumentFailures()
    {
        using var source = DomInterop.Parse("<div id='source'><span id='keep'>current</span></div>");
        var host = new Bookkeeping(); source.EnableSynchronization(host);
        var parent = source.Document.GetElementById("source")!; var keep = source.Document.GetElementById("keep")!;
        var handle = source.GetNativeHandle(keep); var identity = keep.NativeRecordCore!.Identity;
        var document = new DocumentNode(); var sibling = new ElementNode("sibling"); document.AppendChild(sibling);
        var owners = RegistryCount("Owners"); var nodes = RegistryCount("Nodes");
        var version = source.Document.Version; var destinationVersion = document.Version;
        host.FailPrepare = true;
        Throws<OutOfMemoryException>(() => document.AppendChild(keep));
        host.FailPrepare = false;
        True(document.BindingCore is null && document.NativeRecordCore is null && sibling.BindingCore is null);
        True(ReferenceEquals(parent, keep.Parent)); Equal(identity, keep.NativeRecordCore!.Identity);
        Equal(version, source.Document.Version); Equal(destinationVersion, document.Version);
        Equal(owners, RegistryCount("Owners")); Equal(nodes, RegistryCount("Nodes"));
        // An intervening source-domain write invalidates the entire new-owner edit.
        host.OnPrepare = () => source.Native.SetState(handle, (int)ElementState.Focus);
        Throws<InvalidOperationException>(() => document.AppendChild(keep));
        True(document.BindingCore is null && document.NativeRecordCore is null);
        Equal(owners, RegistryCount("Owners")); Equal(nodes, RegistryCount("Nodes"));
        Equal(ElementState.Focus, keep.State); True(ReferenceEquals(parent, keep.Parent));
        Equal(destinationVersion, document.Version); Equal("current", keep.TextContent);
        document.AppendChild(keep); True(ReferenceEquals(document, keep.OwnerDocument));
        var unsupported = new DocumentNode(); unsupported.AppendChild(new DocumentNode());
        Throws<InvalidOperationException>(() => unsupported.AppendChild(keep));
        True(unsupported.BindingCore is null); True(ReferenceEquals(document, keep.OwnerDocument));
    }
    private static int RegistryCount(string name)
    {
        var thread = typeof(NativeDomBinding).GetField("_thread", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!.GetValue(null)!;
        return ((System.Collections.IDictionary)thread.GetType().GetField(name, System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!.GetValue(thread)!).Count;
    }
    private static void ImplicitDocumentLifetime()
    {
        var retained = CreateImplicitWrapper();
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        True(retained.Owner.IsAlive); retained.Node.SetAttribute("after-gc", "live"); Equal("live", retained.Node.GetAttribute("after-gc")!);
        var leaseOnly = CreateImplicitLease();
        using var lease = leaseOnly.Lease;
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        True(!leaseOnly.Owner.IsAlive); True(lease.ReadToken().OwnerIdentity != 0);
        GC.KeepAlive(retained.Node);
    }
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    private static (ElementNode Node, WeakReference Owner) CreateImplicitWrapper()
    {
        using var source = DomInterop.Parse("<span id='keep'>retained</span>"); source.EnableSynchronization(new Bookkeeping());
        var keep = source.Document.GetElementById("keep")!; var document = new DocumentNode(); document.AppendChild(keep);
        return (keep, new WeakReference(((NativeDomBinding)document.BindingCore!).Native));
    }
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    private static (NativeDomLease Lease, WeakReference Owner) CreateImplicitLease()
    {
        var retained = CreateImplicitWrapper();
        var record = retained.Node.NativeRecordCore!;
        using var borrow = record.Owner.Native.Borrow();
        return (NativeDomLease.Retain(record.Handle), retained.Owner);
    }
    private static void ConnectedToBoundDetachedParent()
    {
        using var dom = DomInterop.Parse("<div id='source'><span id='keep'>x</span><div id='target'></div></div>");
        dom.EnableSynchronization(new Bookkeeping());
        var source = dom.Document.GetElementById("source")!; var keep = dom.Document.GetElementById("keep")!;
        var target = dom.Document.GetElementById("target")!; source.RemoveChild(target);
        var before = keep.NativeRecordCore!.DisconnectSerial;
        target.AppendChild(keep);
        True(keep.OwnerDocument is null); Equal(before + 1, keep.NativeRecordCore.DisconnectSerial);
        using var snapshot = NativeDomSnapshot.Acquire(dom.Native);
        var data = snapshot.Nodes.Single(n => n.Identity == keep.NativeRecordCore.Identity);
        Equal(before + 1, data.DisconnectSerial); True(!data.Attached);
    }
    private static void FinalizableOwnerBorrows()
    {
        var retained = CreateOwnerRetainedByNode();
        using var lease = retained.Lease;
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        True(!retained.Owner.IsAlive);
        True(lease.ReadToken().OwnerIdentity != 0); // GC release is not logical retirement.
        using var native = NativeDomDocument.CreateEmpty();
        using var borrowed = native.Borrow();
        using var rootLease = NativeDomLease.Retain(borrowed.Handle);
        var wrongThread = Task.Run(() =>
        {
            try { native.Dispose(); return false; }
            catch (InvalidOperationException) { return true; }
        }).GetAwaiter().GetResult();
        True(wrongThread); True(NativeDomSync.ReadToken(native).OwnerIdentity != 0);
        native.Dispose();
        Throws<InvalidOperationException>(() => rootLease.ReadToken());
        Throws<ObjectDisposedException>(() => native.GetChildCount(borrowed.Handle));
        // The outstanding borrow/lease still pins physical storage after explicit retirement.
    }
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    private static (NativeDomLease Lease, WeakReference Owner) CreateOwnerRetainedByNode()
    {
        var native = NativeDomDocument.CreateEmpty();
        using var borrow = native.Borrow();
        using var edit = NativeDomEdit.Prepare(NativeDomSync.ReadToken(native), new(
            NativeDomEditKind.InsertSubtree, borrow.Handle, Nodes:
            new[] { new NativeDomNodeSpecData((int)NodeType.Element, 0, -1, "retained", "", null, new Dictionary<string, string>()) }));
        var lease = NativeDomLease.Retain(edit.CreatedNode(0));
        try { edit.Commit(); return (lease, new WeakReference(native)); }
        catch { lease.Dispose(); native.Dispose(); throw; }
    }
    private sealed class CustomNode() : Node(NodeType.Element);
    private sealed class Bookkeeping : IDomHostBookkeeping
    {
        internal int Notifications;
        internal bool FailPrepare;
        internal Action? OnPrepare;
        public IDomPreparedBookkeeping Prepare(DomEdit? edit, bool import)
        {
            if (FailPrepare && !import) throw new OutOfMemoryException("Host prepare fault");
            if (!import) { var callback = OnPrepare; OnPrepare = null; callback?.Invoke(); }
            return new Prepared();
        }
        public void NotifyMutationCompleted() => Notifications++;
        private sealed class Prepared : IDomPreparedBookkeeping
        {
            public void Validate() { }
            public void Publish() { }
            public void Dispose() { }
        }
    }
    private static void True(bool value) { if (!value) throw new Exception("Native DOM assertion failed"); }
    private static void Equal<T>(T expected, T actual) { if (!EqualityComparer<T>.Default.Equals(expected, actual)) throw new Exception($"Native DOM expected {expected}, actual {actual}"); }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}");
    }
}
