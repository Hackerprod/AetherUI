namespace AetherUI.Dom;

/// <summary>
/// Compatibility entry point that parses with the native DOM and returns the host-facing managed
/// snapshot. UiDocumentHost retains the owning <see cref="DomInterop"/> for native layout and paint.
/// </summary>
public static class HtmlParser
{
    public static DocumentNode Parse(string source)
    {
        ArgumentNullException.ThrowIfNull(source);
        using var interop = DomInterop.Parse(source);
        return interop.Document;
    }
}
