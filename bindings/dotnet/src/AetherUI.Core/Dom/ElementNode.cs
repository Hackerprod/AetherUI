namespace AetherUI.Dom;

[Flags]
public enum ElementState
{
    None = 0,
    Hover = 1 << 0,
    Active = 1 << 1,
    Focus = 1 << 2,
    Invalid = 1 << 3,
}

public sealed class ElementNode : Node
{
    private readonly Dictionary<string, string> _attributes = new(StringComparer.OrdinalIgnoreCase);

    public ElementNode(string tagName) : base(NodeType.Element)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(tagName);
        TagName = tagName.ToLowerInvariant();
    }

    public string TagName { get; }
    public IReadOnlyDictionary<string, string> Attributes => _attributes;
    private ElementState _state;
    private string? _runtimeValue;
    public ElementState State
    {
        get => _state;
        set
        {
            if (_state == value) return;
            _state = value;
            OwnerDocument?.NotifyInputChanged();
        }
    }
    public string? RuntimeValue
    {
        get => _runtimeValue;
        set
        {
            if (string.Equals(_runtimeValue, value, StringComparison.Ordinal)) return;
            _runtimeValue = value;
            OwnerDocument?.NotifyInputChanged();
        }
    }

    public string? Id => GetAttribute("id");
    public string ClassName => GetAttribute("class") ?? string.Empty;
    public bool IsDisabled => HasAttribute("disabled");
    public bool IsRequired => HasAttribute("required");
    public bool IsFocusable => TagName is "input" or "button" or "textarea" or "select" || HasAttribute("tabindex");

    public string? GetAttribute(string name) =>
        _attributes.TryGetValue(name, out var value) ? value : null;

    public bool HasAttribute(string name) => _attributes.ContainsKey(name);

    public void SetAttribute(string name, string? value = "")
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        _attributes[name.ToLowerInvariant()] = value ?? string.Empty;
        OnTreeChanged();
    }

    public bool RemoveAttribute(string name)
    {
        var removed = _attributes.Remove(name);
        if (removed)
        {
            OnTreeChanged();
        }

        return removed;
    }

    public bool HasClass(string className)
    {
        if (string.IsNullOrWhiteSpace(className))
        {
            return false;
        }

        return ClassName.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
            .Contains(className, StringComparer.Ordinal);
    }

    public void AddClass(string className)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(className);
        if (HasClass(className))
        {
            return;
        }

        SetAttribute("class", string.IsNullOrWhiteSpace(ClassName) ? className : $"{ClassName} {className}");
    }

    public void RemoveClass(string className)
    {
        var classes = ClassName.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
            .Where(existing => !string.Equals(existing, className, StringComparison.Ordinal))
            .ToArray();
        SetAttribute("class", string.Join(' ', classes));
    }

    public IEnumerable<ElementNode> AncestorsAndSelf()
    {
        for (Node? current = this; current is ElementNode element; current = current.Parent)
        {
            yield return element;
        }
    }

    public ElementNode? Closest(string tagName) =>
        AncestorsAndSelf().FirstOrDefault(element => string.Equals(element.TagName, tagName, StringComparison.OrdinalIgnoreCase));

    public override string ToString() => $"<{TagName}{(Id is null ? string.Empty : $" id=\"{Id}\"")}>";
}
