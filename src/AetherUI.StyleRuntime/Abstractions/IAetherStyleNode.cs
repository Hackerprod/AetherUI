namespace AetherUI.StyleRuntime;

/// <summary>
/// Minimal bridge between AetherUI's live visual tree and the style runtime.
/// It deliberately exposes no DOM, CSSOM, navigation, script or browser concepts.
/// </summary>
public interface IAetherStyleNode
{
    /// <summary>A stable identifier for the lifetime of this node.</summary>
    long StyleNodeId { get; }

    /// <summary>The native Aether node kind used by type selectors, for example "button" or "panel".</summary>
    string StyleType { get; }

    /// <summary>Optional node identifier used by #id selectors.</summary>
    string? StyleId { get; }

    /// <summary>Current class names. Class matching is ordinal and case-sensitive.</summary>
    IReadOnlyList<string> StyleClasses { get; }

    /// <summary>Current native interaction and control states.</summary>
    StylePseudoState StyleStates { get; }

    /// <summary>Optional inline declarations, without a wrapping selector.</summary>
    string? InlineStyleText { get; }

    IAetherStyleNode? StyleParent { get; }
    IReadOnlyList<IAetherStyleNode> StyleChildren { get; }

    /// <summary>Fast path for class matching. Implementations should avoid allocating.</summary>
    bool HasStyleClass(string className);

    /// <summary>Looks up a live Aether attribute/property exposed to style selectors.</summary>
    bool TryGetStyleAttribute(string name, out string? value);
}
