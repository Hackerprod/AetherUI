namespace AetherUI.Dom;

public sealed class DocumentNode : Node
{
    private Dictionary<string, ElementNode>? _idIndex;
    private long _version;

    public DocumentNode() : base(NodeType.Document)
    {
    }

    public long Version => _version;
    public ElementNode? DocumentElement => Children.OfType<ElementNode>().FirstOrDefault(element => element.TagName == "html")
        ?? Children.OfType<ElementNode>().FirstOrDefault();
    public ElementNode? Body => DescendantElements().FirstOrDefault(element => element.TagName == "body");

    public ElementNode? GetElementById(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        EnsureIndex();
        return _idIndex!.TryGetValue(id, out var element) ? element : null;
    }

    public IEnumerable<ElementNode> GetElementsByTagName(string tagName) =>
        DescendantElements().Where(element => string.Equals(element.TagName, tagName, StringComparison.OrdinalIgnoreCase));

    public ElementNode? QuerySelector(string selector)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(selector);
        selector = selector.Trim();
        if (selector[0] == '#')
        {
            return GetElementById(selector[1..]);
        }

        if (selector[0] == '.')
        {
            return DescendantElements().FirstOrDefault(element => element.HasClass(selector[1..]));
        }

        return GetElementsByTagName(selector).FirstOrDefault();
    }

    internal void NotifyTreeChanged()
    {
        _version++;
        _idIndex = null;
    }

    private void EnsureIndex()
    {
        if (_idIndex is not null)
        {
            return;
        }

        _idIndex = new Dictionary<string, ElementNode>(StringComparer.Ordinal);
        foreach (var element in DescendantElements())
        {
            if (element.Id is { Length: > 0 } id)
            {
                _idIndex.TryAdd(id, element);
            }
        }
    }
}
