using System.Collections;
using System.Text;

namespace AetherUI.Dom;

public enum NodeType { Document, Element, Text, Comment }

public abstract class Node
{
    private Node[] _children = [];
    private Node? _parent;
    private readonly ChildView _childrenView;

    protected Node(NodeType nodeType)
    {
        NodeType = nodeType;
        _childrenView = new ChildView(this);
    }

    public NodeType NodeType { get; }
    public Node? Parent { get { EnsureCurrent(); return _parent; } }
    public IReadOnlyList<Node> Children { get { EnsureCurrent(); return _childrenView; } }
    public Node? FirstChild { get { EnsureCurrent(); return _children.Length == 0 ? null : _children[0]; } }
    public Node? LastChild { get { EnsureCurrent(); return _children.Length == 0 ? null : _children[^1]; } }
    public DocumentNode? OwnerDocument { get { EnsureCurrent(); return OwnerDocumentCore; } }

    internal IDomBinding? BindingCore { get; set; }
    internal NativeDomNodeRecord? NativeRecordCore { get; set; }
    internal Node? ParentCore => _parent;
    internal Node[] ChildrenCore => _children;
    internal DocumentNode? OwnerDocumentCore
    {
        get
        {
            for (Node? node = this; node is not null; node = node._parent)
                if (node is DocumentNode document) return document;
            return null;
        }
    }
    internal void EnsureCurrent() => BindingCore?.EnsureCurrent();
    internal void PublishImportedLinks(Node? parent, Node[] children, IDomBinding? binding)
    {
        _parent = parent;
        _children = children;
        BindingCore = binding;
    }

    public virtual string TextContent
    {
        get { EnsureCurrent(); return ReadTextCore(this); }
        set
        {
            EnsureCurrent();
            var text = value ?? string.Empty;
            var replacement = text.Length == 0 ? Array.Empty<Node>() : new Node[] { new TextNode(text) };
            var change = PrepareVersions(OwnerDocumentCore, text.Length == 0 ? 1 : 2, true);
            var binding = BindingCore;
            using (var prepared = binding?.PrepareEdit(new(DomEditKind.ReplaceText, this, Child: replacement.FirstOrDefault(), Text: text)))
            {
                prepared?.Commit();
                foreach (var child in _children) child._parent = null;
                _children = replacement;
                foreach (var child in replacement) child._parent = this;
                change.Publish();
            }
            CompleteMutation(binding, change.Document);
        }
    }

    // Certification/import must never call an unknown override. Pure managed callers
    // retain their historical custom TextContent behavior outside a binding.
    internal static string ReadTextCore(Node node)
    {
        if (node is TextNode text) return text.TextCore;
        var result = new StringBuilder();
        foreach (var child in node._children)
        {
            if (child.GetType() == typeof(ElementNode) || child.GetType() == typeof(DocumentNode) ||
                child.GetType() == typeof(TextNode) || child.GetType() == typeof(CommentNode))
                result.Append(ReadTextCore(child));
            else if (node.BindingCore is null) result.Append(child.TextContent);
            else throw new InvalidOperationException("Custom nodes cannot be synchronized.");
        }
        return result.ToString();
    }

    public void AppendChild(Node child) => InsertCore(0, child, append: true);
    public void InsertChild(int index, Node child) => InsertCore(index, child, append: false);

    private void InsertCore(int index, Node child, bool append)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (BindingCore is null && child.BindingCore is NativeDomBinding nativeBinding)
            nativeBinding.EnsureMaterializationInputs(this, child);
        else
        {
            EnsureCurrent();
            if (child.BindingCore is { } childBinding && !ReferenceEquals(childBinding, BindingCore)) childBinding.EnsureCurrent();
        }
        for (Node? current = this; current is not null; current = current._parent)
            if (ReferenceEquals(current, child)) throw new InvalidOperationException("Un nodo no puede insertarse dentro de sí mismo.");
        var source = child._parent;
        var same = ReferenceEquals(source, this);
        var destinationCount = _children.Length - (same ? 1 : 0);
        if (append) index = destinationCount;
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        if (index > destinationCount) throw new ArgumentOutOfRangeException(nameof(index));
        var destination = new Node[destinationCount + 1];
        var output = 0;
        foreach (var existing in _children)
        {
            if (same && ReferenceEquals(existing, child)) continue;
            if (output == index) output++;
            destination[output++] = existing;
        }
        destination[index] = child;
        var sourceChildren = source is not null && !same ? Without(source._children, child) : null;
        var sourceDocument = source?.OwnerDocumentCore;
        var destinationDocument = OwnerDocumentCore;
        var destinationChange = PrepareVersions(destinationDocument, ReferenceEquals(sourceDocument, destinationDocument) && source is not null ? 2 : 1, true);
        var sourceChange = ReferenceEquals(sourceDocument, destinationDocument) ? default : PrepareVersions(sourceDocument, 1, true);
        var binding = BindingCore ?? child.BindingCore;
        using (var prepared = binding?.PrepareEdit(new(DomEditKind.InsertChild, this, child, source, index)))
        {
            prepared?.Commit();
            if (sourceChildren is not null) source!._children = sourceChildren;
            _children = destination;
            child._parent = this;
            destinationChange.Publish();
            sourceChange.Publish();
        }
        CompleteMutation(binding, destinationDocument, sourceChange.Document);
    }

    public bool RemoveChild(Node child)
    {
        ArgumentNullException.ThrowIfNull(child);
        EnsureCurrent();
        if (Array.IndexOf(_children, child) < 0) return false;
        var replacement = Without(_children, child);
        var change = PrepareVersions(OwnerDocumentCore, 1, true);
        var binding = BindingCore;
        using (var prepared = binding?.PrepareEdit(new(DomEditKind.RemoveChild, this, child, this)))
        {
            prepared?.Commit();
            _children = replacement;
            child._parent = null;
            change.Publish();
        }
        CompleteMutation(binding, change.Document);
        return true;
    }

    public void ClearChildren()
    {
        EnsureCurrent();
        var change = PrepareVersions(OwnerDocumentCore, 1, true);
        var binding = BindingCore;
        using (var prepared = binding?.PrepareEdit(new(DomEditKind.ClearChildren, this)))
        {
            prepared?.Commit();
            foreach (var child in _children) child._parent = null;
            _children = [];
            change.Publish();
        }
        CompleteMutation(binding, change.Document);
    }

    public IEnumerable<Node> Descendants()
    {
        EnsureCurrent();
        return DescendantsCore().ToArray();
    }
    public IEnumerable<ElementNode> DescendantElements()
    {
        EnsureCurrent();
        return DescendantsCore().OfType<ElementNode>().ToArray();
    }
    internal IEnumerable<Node> DescendantsCore()
    {
        foreach (var child in _children)
        {
            yield return child;
            foreach (var descendant in child.DescendantsCore()) yield return descendant;
        }
    }
    private static Node[] Without(Node[] source, Node child)
    {
        var result = new Node[source.Length - 1];
        var index = 0;
        foreach (var node in source) if (!ReferenceEquals(node, child)) result[index++] = node;
        return result;
    }
    internal static DocumentVersionChange PrepareVersions(DocumentNode? document, int count, bool tree) =>
        document?.PrepareVersionChange(tree ? count : 0, count) ?? default;

    internal static void CompleteMutation(IDomBinding? binding, DocumentNode? first, DocumentNode? second = null)
    {
        // Legacy unbound notifications happen only after the whole edit is visible.
        // Bound hosts prepare/publish invalidation inside the native edit instead.
        if (binding is not null) binding.MutationCompleted();
        else { first?.NotifyCommittedInputs(); second?.NotifyCommittedInputs(); }
    }

    protected virtual void OnTreeChanged()
    {
        var document = OwnerDocumentCore;
        var change = PrepareVersions(document, 1, true);
        change.Publish();
        CompleteMutation(BindingCore, document);
    }

    private sealed class ChildView(Node owner) : IReadOnlyList<Node>
    {
        public int Count { get { owner.EnsureCurrent(); return owner._children.Length; } }
        public Node this[int index] { get { owner.EnsureCurrent(); return owner._children[index]; } }
        public IEnumerator<Node> GetEnumerator()
        {
            owner.EnsureCurrent();
            return ((IEnumerable<Node>)owner._children).GetEnumerator();
        }
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}

public sealed class TextNode : Node
{
    private string _text;
    public TextNode(string text) : base(NodeType.Text) { _text = text ?? string.Empty; }
    internal string TextCore => _text;
    internal void PublishImportedText(string text) => _text = text;
    public override string TextContent
    {
        get { EnsureCurrent(); return _text; }
        set
        {
            EnsureCurrent();
            var text = value ?? string.Empty;
            var change = PrepareVersions(OwnerDocumentCore, 1, true);
            var binding = BindingCore;
            using (var prepared = binding?.PrepareEdit(new(DomEditKind.SetText, this, Text: text)))
            {
                prepared?.Commit();
                _text = text;
                change.Publish();
            }
            CompleteMutation(binding, change.Document);
        }
    }
    public override string ToString() { EnsureCurrent(); return _text; }
}

public sealed class CommentNode : Node
{
    private string _text;
    public CommentNode(string text) : base(NodeType.Comment) { _text = text ?? string.Empty; }
    public string Text { get { EnsureCurrent(); return _text; } }
    internal string TextCore => _text;
    internal void PublishImportedText(string text) => _text = text;
}
