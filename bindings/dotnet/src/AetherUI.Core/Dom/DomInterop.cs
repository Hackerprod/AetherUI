using AetherUI.Core.Native;

namespace AetherUI.Dom;

/// <summary>
/// Bridges the native DOM to the managed identity model required by input and event routing.
/// The managed nodes are stable wrappers for the lifetime of this parsed document; parsing and
/// selector identity remain native and are shared with the native style runtime.
/// </summary>
internal sealed class DomInterop : IDisposable
{
    private readonly Dictionary<IntPtr, ElementNode> _elementsByHandle = [];
    private readonly Dictionary<ElementNode, IntPtr> _handlesByElement = [];

    private DomInterop(NativeDomDocument native, DocumentNode document)
    {
        Native = native;
        Document = document;
    }

    public NativeDomDocument Native { get; }
    public DocumentNode Document { get; }

    public static DomInterop Parse(string markup)
    {
        var native = NativeDomDocument.Parse(markup);
        try
        {
            var interop = new DomInterop(native, new DocumentNode());
            interop.CopyChildren(native.Handle, interop.Document);
            return interop;
        }
        catch
        {
            native.Dispose();
            throw;
        }
    }

    public IntPtr GetNativeHandle(ElementNode element) =>
        _handlesByElement.TryGetValue(element, out var handle)
            ? handle
            : throw new InvalidOperationException("The element does not belong to this native document.");

    public ElementNode? GetManagedElement(IntPtr handle) =>
        _elementsByHandle.GetValueOrDefault(handle);

    /// <summary>Pushes interactive managed values into the native DOM before its next layout pass.</summary>
    public void SyncRuntimeValues()
    {
        foreach (var (element, handle) in _handlesByElement)
        {
            if (element.TagName == "input" && element.RuntimeValue is { } value)
            {
                Native.SetAttribute(handle, "value", value);
            }
        }
    }

    public void Dispose() => Native.Dispose();

    private void CopyChildren(IntPtr nativeParent, Node managedParent)
    {
        var count = Native.GetChildCount(nativeParent);
        for (var index = 0; index < count; index++)
        {
            var nativeChild = Native.GetChild(nativeParent, index);
            var managedChild = CreateNode(nativeChild);
            managedParent.AppendChild(managedChild);
            if (managedChild is ElementNode)
            {
                CopyChildren(nativeChild, managedChild);
            }
        }
    }

    private Node CreateNode(IntPtr nativeNode)
    {
        return (NodeType)Native.GetNodeType(nativeNode) switch
        {
            NodeType.Element => CreateElement(nativeNode),
            NodeType.Text => new TextNode(Native.GetTextContent(nativeNode) ?? string.Empty),
            NodeType.Comment => new CommentNode(Native.GetTextContent(nativeNode) ?? string.Empty),
            _ => throw new InvalidOperationException("The native DOM contained an unexpected document node."),
        };
    }

    private ElementNode CreateElement(IntPtr nativeElement)
    {
        var element = new ElementNode(Native.GetTagName(nativeElement) ?? "div")
        {
            State = (ElementState)Native.GetState(nativeElement),
        };
        foreach (var (name, value) in Native.GetAttributes(nativeElement))
        {
            element.SetAttribute(name, value);
        }

        _elementsByHandle.Add(nativeElement, element);
        _handlesByElement.Add(element, nativeElement);
        return element;
    }
}
