namespace AetherUI.Dom;

public sealed class DocumentNode : Node
{
    private Dictionary<string, ElementNode>? _idIndex;
    private long _version;
    private long _inputVersion;
    internal event Action? InputsChanged;

    public DocumentNode() : base(NodeType.Document) { }
    public long Version => _version;
    /// <summary>Pure serial of attached tree, attribute, text, state and runtime-value mutations.</summary>
    public long InputVersion => _inputVersion;
    public ElementNode? DocumentElement
    {
        get
        {
            EnsureCurrent();
            return ChildrenCore.OfType<ElementNode>().FirstOrDefault(element => element.TagNameCore == "html")
                ?? ChildrenCore.OfType<ElementNode>().FirstOrDefault();
        }
    }
    public ElementNode? Body
    {
        get { EnsureCurrent(); return DescendantsCore().OfType<ElementNode>().FirstOrDefault(element => element.TagNameCore == "body"); }
    }
    public ElementNode? GetElementById(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        EnsureCurrent();
        return GetElementByIdCore(id);
    }
    private ElementNode? GetElementByIdCore(string id)
    {
        if (_idIndex is null)
        {
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("dom.document.id-map");
#endif
            var index = new Dictionary<string, ElementNode>(StringComparer.Ordinal);
            foreach (var element in DescendantsCore().OfType<ElementNode>())
                if (element.GetAttributeCore("id") is { Length: > 0 } key)
                {
#if AETHER_DOM_FAULT_TESTING
                    AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(index, key, "dom.document.id-map-add");
#endif
                    index.TryAdd(key, element);
                }
            _idIndex = index;
        }
        return _idIndex.TryGetValue(id, out var result) ? result : null;
    }
    public IEnumerable<ElementNode> GetElementsByTagName(string tagName)
    {
        EnsureCurrent();
        var name = DomNames.Canonicalize(tagName);
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.document.tag-query-aggregate");
#endif
        return DescendantsCore().OfType<ElementNode>().Where(element => element.TagNameCore == name).ToArray();
    }
    public ElementNode? QuerySelector(string selector)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(selector);
        EnsureCurrent();
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("dom.document.selector-trim");
#endif
        selector = selector.Trim();
        if (selector[0] == '#') return GetElementByIdCore(selector[1..]);
        if (selector[0] == '.') return DescendantsCore().OfType<ElementNode>().FirstOrDefault(element => element.HasClassCore(selector[1..]));
        var name = DomNames.Canonicalize(selector);
        return DescendantsCore().OfType<ElementNode>().FirstOrDefault(element => element.TagNameCore == name);
    }

    internal DocumentVersionChange PrepareVersionChange(int versionDelta, int inputDelta)
    {
        // Overflow is rejected before preparing native edits or changing mirror fields.
        return new(this, checked(_version + versionDelta), checked(_inputVersion + inputDelta));
    }
    internal void PublishVersions(long version, long inputVersion)
    {
        if (_version != version) _idIndex = null;
        _version = version;
        _inputVersion = inputVersion;
    }
    internal void NotifyCommittedInputs() => InputsChanged?.Invoke();
    internal void NotifyTreeChanged()
    {
        PrepareVersionChange(1, 1).Publish();
        CompleteMutation(BindingCore, this);
    }
    internal void NotifyInputChanged()
    {
        PrepareVersionChange(0, 1).Publish();
        CompleteMutation(BindingCore, this);
    }
}
