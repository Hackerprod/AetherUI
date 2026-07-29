namespace AetherUI.Core.Native;

/// <summary>
/// Owns one native DOM document and exposes its stable node handles to the managed consumer.
/// The handles remain valid until this object is disposed or reparsed.
/// </summary>
public sealed unsafe class NativeDomDocument : IDisposable
{
    private IntPtr _handle;
    private bool _disposed;

    private NativeDomDocument(IntPtr handle) => _handle = handle;

    public IntPtr Handle
    {
        get
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            return _handle;
        }
    }

    public static NativeDomDocument Parse(string markup)
    {
        ArgumentNullException.ThrowIfNull(markup);
        NativeAbiContract.EnsureCompatible();
        Throw(NativeMethods.DocCreate(out var handle));
        var document = new NativeDomDocument(handle);
        try
        {
            document.Reparse(markup);
            return document;
        }
        catch
        {
            document.Dispose();
            throw;
        }
    }

    public void Reparse(string markup)
    {
        ArgumentNullException.ThrowIfNull(markup);
        ObjectDisposedException.ThrowIf(_disposed, this);
        var utf8 = System.Text.Encoding.UTF8.GetBytes(markup + '\0');
        fixed (byte* source = utf8)
        {
            Throw(NativeMethods.DocParseHtml(_handle, source));
        }
    }

    public IntPtr DocumentElement => NativeMethods.DocGetDocumentElement(Handle);
    public IntPtr Body => NativeMethods.DocGetBody(Handle);
    public int GetNodeType(IntPtr node) => NativeMethods.NodeTypeGet(node);
    public int GetChildCount(IntPtr node) => NativeMethods.NodeGetChildCount(node);
    public IntPtr GetChild(IntPtr node, int index) => NativeMethods.NodeGetChild(node, index);
    public string? GetTextContent(IntPtr node) => NativeStrings.MarshalToStringAndFree(NativeMethods.NodeGetTextContent(node));
    public long GetStyleNodeId(IntPtr node) => NativeMethods.NodeGetStyleNodeId(node);
    public string? GetTagName(IntPtr element) => NativeStrings.MarshalToStringAndFree(NativeMethods.ElementGetTagName(element));
    public int GetState(IntPtr element) => NativeMethods.ElementGetState(element);
    public void SetState(IntPtr element, int state) => NativeMethods.ElementSetState(element, state);

    public void SetAttribute(IntPtr element, string name, string value)
    {
        ArgumentException.ThrowIfNullOrEmpty(name);
        ArgumentNullException.ThrowIfNull(value);
        ObjectDisposedException.ThrowIf(_disposed, this);
        var utf8Name = System.Text.Encoding.UTF8.GetBytes(name + '\0');
        var utf8Value = System.Text.Encoding.UTF8.GetBytes(value + '\0');
        fixed (byte* nativeName = utf8Name)
        fixed (byte* nativeValue = utf8Value)
        {
            Throw(NativeMethods.ElementSetAttribute(element, nativeName, nativeValue));
        }
    }

    public IEnumerable<IntPtr> EnumerateElementHandles()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return EnumerateElements(Handle).ToArray();
    }

    public IReadOnlyList<KeyValuePair<string, string>> GetAttributes(IntPtr element)
    {
        var attributes = new List<KeyValuePair<string, string>>();
        var count = NativeMethods.ElementGetAttributeCount(element);
        for (var index = 0; index < count; index++)
        {
            var name = NativeStrings.MarshalToStringAndFree(NativeMethods.ElementGetAttributeName(element, index));
            if (name is null)
            {
                continue;
            }

            attributes.Add(new KeyValuePair<string, string>(
                name,
                NativeStrings.MarshalToStringAndFree(NativeMethods.ElementGetAttributeValue(element, index)) ?? string.Empty));
        }

        return attributes;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        if (_handle != IntPtr.Zero) NativeMethods.DocDestroy(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    private IEnumerable<IntPtr> EnumerateElements(IntPtr node)
    {
        if (NativeMethods.NodeTypeGet(node) == 1)
        {
            yield return node;
        }

        var count = NativeMethods.NodeGetChildCount(node);
        for (var index = 0; index < count; index++)
        {
            foreach (var descendant in EnumerateElements(NativeMethods.NodeGetChild(node, index)))
            {
                yield return descendant;
            }
        }
    }

    private static void Throw(int status)
    {
        if (status != 0)
        {
            throw new InvalidOperationException($"Native AetherUI call failed with status {status}.");
        }
    }
}
