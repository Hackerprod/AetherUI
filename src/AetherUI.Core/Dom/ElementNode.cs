using AetherUI.StyleRuntime;

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

public sealed class ElementNode : Node, IAetherStyleNode
{
    private static long _nextStyleNodeId = 1;

    private readonly Dictionary<string, string> _attributes = new(StringComparer.OrdinalIgnoreCase);
    private readonly long _styleNodeId = System.Threading.Interlocked.Increment(ref _nextStyleNodeId);

    public ElementNode(string tagName) : base(NodeType.Element)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(tagName);
        TagName = tagName.ToLowerInvariant();
    }

    public string TagName { get; }
    public IReadOnlyDictionary<string, string> Attributes => _attributes;
    public ElementState State { get; set; }
    public string? RuntimeValue { get; set; }

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

    // IAetherStyleNode: a minimal read view AetherUI.StyleRuntime uses to match selectors and
    // cascade styles against the live tree. Implemented directly on ElementNode (not a wrapper)
    // since ElementNode already owns stable identity, classes, attributes and tree structure.
    long IAetherStyleNode.StyleNodeId => _styleNodeId;
    string IAetherStyleNode.StyleType => TagName;
    string? IAetherStyleNode.StyleId => Id;
    IReadOnlyList<string> IAetherStyleNode.StyleClasses =>
        ClassName.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);

    StylePseudoState IAetherStyleNode.StyleStates
    {
        get
        {
            var states = StylePseudoState.None;
            if (State.HasFlag(ElementState.Hover)) states |= StylePseudoState.Hover;
            if (State.HasFlag(ElementState.Active)) states |= StylePseudoState.Active;
            if (State.HasFlag(ElementState.Focus)) states |= StylePseudoState.Focus;
            if (State.HasFlag(ElementState.Invalid)) states |= StylePseudoState.Invalid;
            if (IsDisabled) states |= StylePseudoState.Disabled;
            if (HasAttribute("checked")) states |= StylePseudoState.Checked;
            if (HasAttribute("readonly")) states |= StylePseudoState.ReadOnly;
            return states;
        }
    }

    string? IAetherStyleNode.InlineStyleText => GetAttribute("style");
    IAetherStyleNode? IAetherStyleNode.StyleParent => Parent as ElementNode;
    IReadOnlyList<IAetherStyleNode> IAetherStyleNode.StyleChildren => Children.OfType<ElementNode>().ToArray();

    bool IAetherStyleNode.HasStyleClass(string className) => HasClass(className);

    bool IAetherStyleNode.TryGetStyleAttribute(string name, out string? value)
    {
        value = GetAttribute(name);
        return value is not null;
    }
}
