using System.Collections;
namespace AetherUI.Dom;

[Flags]
public enum ElementState { None = 0, Hover = 1 << 0, Active = 1 << 1, Focus = 1 << 2, Invalid = 1 << 3 }

public sealed class ElementNode : Node
{
    private Dictionary<string, string> _attributes = new(StringComparer.Ordinal);
    private readonly AttributeView _attributeView;
    private readonly string _tagName;
    private ElementState _state;
    private string? _runtimeValue;
    public ElementNode(string tagName) : base(NodeType.Element)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(tagName);
        _tagName = DomNames.Canonicalize(tagName);
        _attributeView = new AttributeView(this);
    }
    public string TagName { get { EnsureCurrent(); return _tagName; } }
    public IReadOnlyDictionary<string, string> Attributes { get { EnsureCurrent(); return _attributeView; } }
    internal string TagNameCore => _tagName;
    internal Dictionary<string, string> AttributesCore => _attributes;
    internal ElementState StateCore => _state;
    internal string? RuntimeValueCore => _runtimeValue;
    internal string? GetAttributeCore(string canonicalName) => _attributes.TryGetValue(canonicalName, out var value) ? value : null;
    internal void PublishImportedInputs(Dictionary<string, string> attributes, ElementState state, string? runtimeValue)
    {
        _attributes = attributes;
        _state = state;
        _runtimeValue = runtimeValue;
    }
    public ElementState State
    {
        get { EnsureCurrent(); return _state; }
        set
        {
            EnsureCurrent();
            if (_state == value) return;
            var change = PrepareVersions(OwnerDocumentCore, 1, false);
            var binding = BindingCore;
            using (var prepared = binding?.PrepareEdit(new(DomEditKind.SetState, this, State: value)))
            {
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
                DomCommitAllocationLedger.Begin(DomCommitAllocationKind.State);
                var allocationCommitSucceeded = false;
                try
                {
#endif
                prepared?.Commit();
                _state = value;
                change.Publish();
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
                DomCommitAllocationLedger.MarkPhase(DomCommitAllocationPhase.ScalarMirror);
                    allocationCommitSucceeded = true;
                }
                finally { DomCommitAllocationLedger.End(allocationCommitSucceeded); }
#endif
            }
            CompleteMutation(binding, change.Document);
        }
    }
    public string? RuntimeValue
    {
        get { EnsureCurrent(); return _runtimeValue; }
        set
        {
            EnsureCurrent();
            if (string.Equals(_runtimeValue, value, StringComparison.Ordinal)) return;
            var change = PrepareVersions(OwnerDocumentCore, 1, false);
            var binding = BindingCore;
            using (var prepared = binding?.PrepareEdit(new(DomEditKind.SetRuntimeValue, this, Text: value)))
            {
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
                DomCommitAllocationLedger.Begin(DomCommitAllocationKind.RuntimeValue);
                var allocationCommitSucceeded = false;
                try
                {
#endif
                prepared?.Commit();
                _runtimeValue = value;
                change.Publish();
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
                    allocationCommitSucceeded = true;
                }
                finally { DomCommitAllocationLedger.End(allocationCommitSucceeded); }
#endif
            }
            CompleteMutation(binding, change.Document);
        }
    }
    public string? Id { get { EnsureCurrent(); return GetAttributeCore("id"); } }
    public string ClassName { get { EnsureCurrent(); return GetAttributeCore("class") ?? string.Empty; } }
    public bool IsDisabled { get { EnsureCurrent(); return _attributes.ContainsKey("disabled"); } }
    public bool IsRequired { get { EnsureCurrent(); return _attributes.ContainsKey("required"); } }
    public bool IsFocusable
    {
        get { EnsureCurrent(); return _tagName is "input" or "button" or "textarea" or "select" || _attributes.ContainsKey("tabindex"); }
    }
    public string? GetAttribute(string name) { EnsureCurrent(); return GetAttributeCore(DomNames.Canonicalize(name)); }
    public bool HasAttribute(string name) { EnsureCurrent(); return _attributes.ContainsKey(DomNames.Canonicalize(name)); }
    public void SetAttribute(string name, string? value = "")
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        EnsureCurrent();
        SetAttributeCore(DomNames.Canonicalize(name), value ?? string.Empty);
    }
    private void SetAttributeCore(string name, string value)
    {
        var attributes = new Dictionary<string, string>(_attributes, StringComparer.Ordinal) { [name] = value };
        var change = PrepareVersions(OwnerDocumentCore, 1, true);
        var binding = BindingCore;
        using (var prepared = binding?.PrepareEdit(new(DomEditKind.SetAttribute, this, Name: name, Text: value)))
        {
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
            DomCommitAllocationLedger.Begin(DomCommitAllocationKind.SetAttribute);
            var allocationCommitSucceeded = false;
            try
            {
#endif
            prepared?.Commit();
            _attributes = attributes;
            change.Publish();
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
                allocationCommitSucceeded = true;
            }
            finally { DomCommitAllocationLedger.End(allocationCommitSucceeded); }
#endif
        }
        CompleteMutation(binding, change.Document);
    }
    public bool RemoveAttribute(string name)
    {
        EnsureCurrent();
        name = DomNames.Canonicalize(name);
        if (!_attributes.ContainsKey(name)) return false;
        var attributes = new Dictionary<string, string>(_attributes, StringComparer.Ordinal);
        attributes.Remove(name);
        var change = PrepareVersions(OwnerDocumentCore, 1, true);
        var binding = BindingCore;
        using (var prepared = binding?.PrepareEdit(new(DomEditKind.RemoveAttribute, this, Name: name)))
        {
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
            DomCommitAllocationLedger.Begin(DomCommitAllocationKind.RemoveAttribute);
            var allocationCommitSucceeded = false;
            try
            {
#endif
            prepared?.Commit();
            _attributes = attributes;
            change.Publish();
#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
                allocationCommitSucceeded = true;
            }
            finally { DomCommitAllocationLedger.End(allocationCommitSucceeded); }
#endif
        }
        CompleteMutation(binding, change.Document);
        return true;
    }
    internal bool HasClassCore(string className) => !string.IsNullOrWhiteSpace(className) &&
        (GetAttributeCore("class") ?? string.Empty).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries).Contains(className, StringComparer.Ordinal);
    public bool HasClass(string className) { EnsureCurrent(); return HasClassCore(className); }
    public void AddClass(string className)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(className);
        EnsureCurrent();
        if (HasClassCore(className)) return;
        var current = GetAttributeCore("class");
        SetAttributeCore("class", string.IsNullOrWhiteSpace(current) ? className : $"{current} {className}");
    }
    public void RemoveClass(string className)
    {
        EnsureCurrent();
        var classes = (GetAttributeCore("class") ?? string.Empty).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
            .Where(existing => !string.Equals(existing, className, StringComparison.Ordinal));
        SetAttributeCore("class", string.Join(' ', classes));
    }
    public IEnumerable<ElementNode> AncestorsAndSelf()
    {
        EnsureCurrent();
        var result = new List<ElementNode>();
        for (Node? node = this; node is ElementNode element; node = node.ParentCore) result.Add(element);
        return result.ToArray();
    }
    public ElementNode? Closest(string tagName)
    {
        EnsureCurrent();
        var name = DomNames.Canonicalize(tagName);
        for (Node? node = this; node is ElementNode element; node = node.ParentCore)
            if (element._tagName == name) return element;
        return null;
    }
    public override string ToString()
    {
        EnsureCurrent();
        var id = GetAttributeCore("id");
        return $"<{_tagName}{(id is null ? string.Empty : $" id=\"{id}\"")}>";
    }
    private sealed class AttributeView(ElementNode owner) : IReadOnlyDictionary<string, string>
    {
        public int Count { get { owner.EnsureCurrent(); return owner._attributes.Count; } }
        public IEnumerable<string> Keys { get { owner.EnsureCurrent(); return owner._attributes.Keys.ToArray(); } }
        public IEnumerable<string> Values { get { owner.EnsureCurrent(); return owner._attributes.Values.ToArray(); } }
        public string this[string key] { get { owner.EnsureCurrent(); return owner._attributes[DomNames.Canonicalize(key)]; } }
        public bool ContainsKey(string key) { owner.EnsureCurrent(); return owner._attributes.ContainsKey(DomNames.Canonicalize(key)); }
        public bool TryGetValue(string key, out string value) { owner.EnsureCurrent(); return owner._attributes.TryGetValue(DomNames.Canonicalize(key), out value!); }
        public IEnumerator<KeyValuePair<string, string>> GetEnumerator()
        {
            owner.EnsureCurrent();
            return owner._attributes.GetEnumerator();
        }
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}
