namespace AetherUI.Dom;

public enum NodeType
{
    Document,
    Element,
    Text,
    Comment,
}

public abstract class Node
{
    private readonly List<Node> _children = [];

    protected Node(NodeType nodeType)
    {
        NodeType = nodeType;
    }

    public NodeType NodeType { get; }
    public Node? Parent { get; private set; }
    public IReadOnlyList<Node> Children => _children;

    public Node? FirstChild => _children.Count == 0 ? null : _children[0];
    public Node? LastChild => _children.Count == 0 ? null : _children[^1];

    public virtual string TextContent
    {
        get => string.Concat(_children.Select(static child => child.TextContent));
        set
        {
            ClearChildren();
            if (!string.IsNullOrEmpty(value))
            {
                AppendChild(new TextNode(value));
            }
        }
    }

    public void AppendChild(Node child)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (ReferenceEquals(child, this) || IsAncestorOf(child))
        {
            throw new InvalidOperationException("Un nodo no puede insertarse dentro de sí mismo.");
        }

        child.Parent?.RemoveChild(child);
        child.Parent = this;
        _children.Add(child);
        OnTreeChanged();
    }

    public void InsertChild(int index, Node child)
    {
        ArgumentNullException.ThrowIfNull(child);
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        if (index > _children.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }

        if (ReferenceEquals(child, this) || IsAncestorOf(child))
        {
            throw new InvalidOperationException("Un nodo no puede insertarse dentro de sí mismo.");
        }

        child.Parent?.RemoveChild(child);
        child.Parent = this;
        _children.Insert(index, child);
        OnTreeChanged();
    }

    public bool RemoveChild(Node child)
    {
        ArgumentNullException.ThrowIfNull(child);
        var removed = _children.Remove(child);
        if (removed)
        {
            child.Parent = null;
            OnTreeChanged();
        }

        return removed;
    }

    public void ClearChildren()
    {
        foreach (var child in _children)
        {
            child.Parent = null;
        }

        _children.Clear();
        OnTreeChanged();
    }

    public IEnumerable<Node> Descendants()
    {
        foreach (var child in _children)
        {
            yield return child;
            foreach (var descendant in child.Descendants())
            {
                yield return descendant;
            }
        }
    }

    public IEnumerable<ElementNode> DescendantElements() => Descendants().OfType<ElementNode>();

    private bool IsAncestorOf(Node candidate)
    {
        for (var current = this.Parent; current is not null; current = current.Parent)
        {
            if (ReferenceEquals(current, candidate))
            {
                return true;
            }
        }

        return false;
    }

    protected virtual void OnTreeChanged()
    {
        var document = this as DocumentNode ?? OwnerDocument;
        document?.NotifyTreeChanged();
    }

    public DocumentNode? OwnerDocument
    {
        get
        {
            for (Node? current = this; current is not null; current = current.Parent)
            {
                if (current is DocumentNode document)
                {
                    return document;
                }
            }

            return null;
        }
    }
}

public sealed class TextNode : Node
{
    private string _text;

    public TextNode(string text) : base(NodeType.Text)
    {
        _text = text ?? string.Empty;
    }

    public override string TextContent
    {
        get => _text;
        set
        {
            _text = value ?? string.Empty;
            OnTreeChanged();
        }
    }

    public override string ToString() => _text;
}

public sealed class CommentNode : Node
{
    public CommentNode(string text) : base(NodeType.Comment)
    {
        Text = text ?? string.Empty;
    }

    public string Text { get; }
}
