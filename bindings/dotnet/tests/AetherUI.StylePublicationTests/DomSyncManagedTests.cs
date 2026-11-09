using AetherUI.Dom;

internal static class DomSyncManagedTests
{
    internal static void Run()
    {
        AtomicTreeEdits();
        ReadOnlySnapshots();
        AsciiNamesAndDetachedDomain();
        GetterBarrierAndFailure();
        MutationFailures();
        CounterExhaustion();
        Console.WriteLine("PASS managed DOM staging, views, barriers and failures");
    }

    private static void AtomicTreeEdits()
    {
        var document = new DocumentNode();
        var a = new ElementNode("a"); var b = new ElementNode("b"); var text = new TextNode("old");
        document.AppendChild(a); document.AppendChild(b); a.AppendChild(text);
        var v = document.Version; var notifications = 0;
        Action listener = () =>
        {
            notifications++;
            True(ReferenceEquals(text.ParentCore, b));
            Equal(0, a.ChildrenCore.Length); Equal(1, b.ChildrenCore.Length);
        };
        document.InputsChanged += listener;
        b.AppendChild(text);
        Equal(v + 2, document.Version); Equal(1, notifications);
        document.InputsChanged -= listener;
        var second = new TextNode("second"); b.AppendChild(second);
        b.InsertChild(0, second);
        True(ReferenceEquals(b.FirstChild, second)); True(ReferenceEquals(b.LastChild, text));
        // Index is validated against the post-extraction vector, without first removing.
        var before = b.Children.ToArray(); v = document.Version;
        Throws<ArgumentOutOfRangeException>(() => b.InsertChild(2, second));
        True(before.SequenceEqual(b.Children)); Equal(v, document.Version);
        Throws<InvalidOperationException>(() => b.AppendChild(document));
        var other = new DocumentNode(); var ov = other.Version; v = document.Version;
        other.AppendChild(b);
        Equal(v + 1, document.Version); Equal(ov + 1, other.Version);
        True(ReferenceEquals(b.OwnerDocument, other));
        var clear = new ElementNode("clear"); other.AppendChild(clear);
        v = other.Version; clear.TextContent = "replacement";
        Equal(v + 2, other.Version); Equal("replacement", clear.TextContent);
        v = other.Version; clear.TextContent = ""; Equal(v + 1, other.Version);
        v = other.Version; clear.ClearChildren(); Equal(v + 1, other.Version);
        True(!clear.RemoveChild(text)); Equal(v + 1, other.Version);
    }

    private static void ReadOnlySnapshots()
    {
        var root = new ElementNode("root"); var a = new ElementNode("a"); root.AppendChild(a);
        var view = root.Children;
        True(view is not IList<Node>); True(view is not Node[]);
        var enumerator = view.GetEnumerator();
        root.AppendChild(new TextNode("b"));
        True(enumerator.MoveNext()); True(ReferenceEquals(a, enumerator.Current)); True(!enumerator.MoveNext());
        Equal(2, view.Count);
        root.SetAttribute("A", "old");
        var attrs = root.Attributes;
        True(attrs is not IDictionary<string, string>);
        var attrEnumerator = attrs.GetEnumerator(); root.SetAttribute("a", "new"); root.SetAttribute("b", "b");
        True(attrEnumerator.MoveNext()); Equal("old", attrEnumerator.Current.Value); True(!attrEnumerator.MoveNext());
        Equal("new", attrs["A"]); Equal(2, attrs.Count);
        True(root.RemoveAttribute("A")); True(!attrs.ContainsKey("a"));
    }

    private static void AsciiNamesAndDetachedDomain()
    {
        var document = new DocumentNode(); var upper = new ElementNode("É"); var lower = new ElementNode("é");
        document.AppendChild(upper); document.AppendChild(lower);
        upper.SetAttribute("É", "one"); upper.SetAttribute("é", "two"); upper.SetAttribute("CLASS", "MyClass");
        Equal("one", upper.GetAttribute("É")); Equal("two", upper.GetAttribute("é"));
        True(upper.HasAttribute("class")); True(upper.HasClass("MyClass")); True(!upper.HasClass("myclass"));
        True(ReferenceEquals(document.QuerySelector("É"), upper)); True(ReferenceEquals(document.QuerySelector("é"), lower));
        Equal(1, document.GetElementsByTagName("É").Count()); True(upper.Closest("é") is null);
        var input = new ElementNode("INPUT"); upper.AppendChild(input); True(input.IsFocusable); Equal("input", input.TagName);
        var raw = "\0\ud800"; var text = new TextNode(raw); upper.AppendChild(text); Equal(raw, text.TextContent);
        upper.SetAttribute(raw, raw); Equal(raw, upper.GetAttribute(raw));
        var nested = new DocumentNode(); upper.AppendChild(nested); nested.AppendChild(new CustomNode());
        True(ReferenceEquals(nested, nested.Children[0].OwnerDocument));
    }

    private static void GetterBarrierAndFailure()
    {
        var document = new DocumentNode(); var element = new ElementNode("input"); document.AppendChild(element);
        var binding = new FakeBinding(); Bind(document, binding);
        binding.Import = () => element.PublishImportedInputs(new(StringComparer.Ordinal) { ["x"] = "R2" }, ElementState.Invalid, "native");
        element.State |= ElementState.Hover;
        Equal(ElementState.Invalid | ElementState.Hover, element.StateCore);
        Equal(1, binding.Completions); Equal(1, binding.Commits);
        Equal(2, binding.Reads); // One getter + one setter, no reads in commit.
        var v = document.InputVersion; element.State = element.StateCore; element.RuntimeValue = element.RuntimeValueCore;
        Equal(v, document.InputVersion); Equal(1, binding.Commits);
        binding.Import = () => element.PublishImportedInputs(new(StringComparer.Ordinal) { ["x"] = "R3" }, ElementState.Focus, "new native");
        binding.FailPrepare = true;
        Throws<OutOfMemoryException>(() => element.SetAttribute("attempt", "not published"));
        Equal(ElementState.Focus, element.StateCore); Equal("R3", element.GetAttributeCore("x"));
        True(element.GetAttributeCore("attempt") is null); Equal("new native", element.RuntimeValueCore!);
        Equal(1, binding.Completions);
        binding.FailPrepare = false;
        var reads = binding.Reads; _ = document.Version; _ = document.InputVersion; _ = element.ParentCore; _ = element.StateCore;
        Equal(reads, binding.Reads);
        var children = document.Children; reads = binding.Reads; _ = children.Count; Equal(reads + 1, binding.Reads);
        var attrs = element.Attributes; reads = binding.Reads; _ = attrs.Count; Equal(reads + 1, binding.Reads);
        reads = binding.Reads; _ = document.QuerySelector("input"); Equal(reads + 1, binding.Reads);
        reads = binding.Reads; _ = element.TextContent; Equal(reads + 1, binding.Reads);
        binding.AfterCommit = () => { True(element.StateCore == ElementState.Hover); throw new ApplicationException("listener"); };
        Throws<ApplicationException>(() => element.State = ElementState.Hover);
        Equal(ElementState.Hover, element.StateCore); // Listener failure is not rollback.
    }

    private static void MutationFailures()
    {
        var document = new DocumentNode(); var root = new ElementNode("root"); var text = new TextNode("text");
        document.AppendChild(root); root.AppendChild(text); root.SetAttribute("a", "original");
        var binding = new FakeBinding(); Bind(document, binding);
        Action[] edits = [() => root.AppendChild(new TextNode("new")), () => root.InsertChild(0, new TextNode("new")),
            () => root.RemoveChild(text), () => root.ClearChildren(), () => root.TextContent = "new", () => text.TextContent = "new",
            () => root.SetAttribute("a", "new"), () => root.RemoveAttribute("a"), () => root.State = ElementState.Focus,
            () => root.RuntimeValue = "new"];
        foreach (var commitFailure in new[] { false, true })
        {
            binding.FailPrepare = !commitFailure; binding.FailCommit = commitFailure;
            foreach (var edit in edits)
            {
                var v = document.Version; var iv = document.InputVersion;
                Throws<OutOfMemoryException>(edit);
                Equal(v, document.Version); Equal(iv, document.InputVersion);
                Equal(1, root.ChildrenCore.Length); True(ReferenceEquals(text, root.ChildrenCore[0]));
                True(ReferenceEquals(root, text.ParentCore)); Equal("text", text.TextCore);
                Equal("original", root.GetAttributeCore("a")!); Equal(ElementState.None, root.StateCore); True(root.RuntimeValueCore is null);
            }
        }
        binding.FailPrepare = binding.FailCommit = false;
        root.RemoveChild(text); True(text.OwnerDocument is null); True(ReferenceEquals(binding, text.BindingCore));
        binding.Import = () => text.PublishImportedText("detached R2"); Equal("detached R2", text.TextContent);
        root.AppendChild(text); Equal("detached R2", text.TextContent);
    }

    private static void CounterExhaustion()
    {
        var document = new DocumentNode(); var node = new ElementNode("a"); document.AppendChild(node);
        document.PublishVersions(long.MaxValue, long.MaxValue);
        Throws<OverflowException>(() => node.SetAttribute("a", "b")); True(node.GetAttribute("a") is null);
        Throws<OverflowException>(() => node.State = ElementState.Focus); Equal(ElementState.None, node.State);
        Throws<OverflowException>(() => node.ClearChildren()); Equal(long.MaxValue, document.Version);
        node.State = ElementState.None; node.RuntimeValue = null; // Exact no-op remains legal.
        True(!node.RemoveAttribute("absent")); True(!node.RemoveChild(new TextNode("absent")));
    }

    private static void Bind(Node node, IDomBinding binding)
    {
        node.BindingCore = binding;
        foreach (var child in node.ChildrenCore) Bind(child, binding);
    }
    private sealed class FakeBinding : IDomBinding
    {
        internal Action? Import, AfterCommit;
        internal bool FailPrepare, FailCommit;
        internal int Reads, Commits, Completions;
        public void EnsureCurrent() { Reads++; var action = Import; Import = null; action?.Invoke(); }
        public IDomPreparedEdit PrepareEdit(DomEdit edit)
        {
            if (FailPrepare) throw new OutOfMemoryException("prepare");
            return new Edit(this);
        }
        public void MutationCompleted() { Completions++; AfterCommit?.Invoke(); }
        private sealed class Edit(FakeBinding owner) : IDomPreparedEdit
        {
            public void Commit() { if (owner.FailCommit) throw new OutOfMemoryException("commit precondition"); owner.Commits++; }
            public void Dispose() { }
        }
    }
    private sealed class CustomNode() : Node(NodeType.Element);
    private static void True(bool value) { if (!value) throw new Exception("DOM assertion failed"); }
    private static void Equal<T>(T expected, T actual) { if (!EqualityComparer<T>.Default.Equals(expected, actual)) throw new Exception($"DOM expected {expected}, actual {actual}"); }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}");
    }
}
