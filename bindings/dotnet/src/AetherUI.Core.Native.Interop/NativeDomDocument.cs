namespace AetherUI.Core.Native;

/// <summary>
/// Owns one native DOM document and exposes its stable node handles to the managed consumer.
/// The handles remain valid until this object is disposed or reparsed.
/// </summary>
public sealed unsafe class NativeDomDocument : IDisposable
{
    private readonly IntPtr _handle;
    private readonly NativeDomOwnerHandle? _owner;
    private readonly int _ownerThread = Environment.CurrentManagedThreadId;
    private bool _disposed;

    private NativeDomDocument(IntPtr handle, NativeDomOwnerHandle? owner = null) { _handle = handle; _owner = owner; }

    internal static NativeDomDocument CreateEmpty()
    {
        NativeAbiContract.EnsureCompatible();
        var owner = new NativeDomOwnerHandle();
        try
        {
            NativeDomSync.Throw(NativeMethods.DomOwnerCreate(out var value, out var document));
            owner.Initialize(value);
            try { return new NativeDomDocument(document, owner); }
            catch { NativeDomSync.Throw(NativeMethods.DomOwnerRetire(owner)); throw; }
        }
        catch { owner.Dispose(); throw; }
    }

    internal NativeDomBorrow Borrow()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (Environment.CurrentManagedThreadId != _ownerThread)
            throw new InvalidOperationException("Native DOM access requires its owner thread.");
        return new NativeDomBorrow(_owner, _handle);
    }

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
        // Production Parse remains on its existing explicit-lifetime route until
        // the finalizable-owner integration gate is independently validated.
        NativeAbiContract.EnsureCompatible();
        Throw(NativeMethods.DocCreate(out var handle));
        NativeDomDocument document;
        try { document = new NativeDomDocument(handle); }
        catch { NativeMethods.DocDestroy(handle); throw; }
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
        using var borrow = Borrow();
        var utf8 = System.Text.Encoding.UTF8.GetBytes(markup + '\0');
        fixed (byte* source = utf8)
        {
            NativeDomWorkCounters.RecordParse();
            Throw(NativeMethods.DocParseHtml(borrow.Handle, source));
        }
    }

    public IntPtr DocumentElement { get { using var borrow = Borrow(); return NativeMethods.DocGetDocumentElement(borrow.Handle); } }
    public IntPtr Body { get { using var borrow = Borrow(); return NativeMethods.DocGetBody(borrow.Handle); } }
    public int GetNodeType(IntPtr node) { using var borrow = Borrow(); return NativeMethods.NodeTypeGet(node); }
    public int GetChildCount(IntPtr node) { using var borrow = Borrow(); return NativeMethods.NodeGetChildCount(node); }
    public IntPtr GetChild(IntPtr node, int index) { using var borrow = Borrow(); return NativeMethods.NodeGetChild(node, index); }
    public string? GetTextContent(IntPtr node) { using var borrow = Borrow(); return NativeStrings.MarshalToStringAndFree(NativeMethods.NodeGetTextContent(node)); }
    public long GetStyleNodeId(IntPtr node) { using var borrow = Borrow(); return NativeMethods.NodeGetStyleNodeId(node); }
    public string? GetTagName(IntPtr element) { using var borrow = Borrow(); return NativeStrings.MarshalToStringAndFree(NativeMethods.ElementGetTagName(element)); }
    public int GetState(IntPtr element) { using var borrow = Borrow(); return NativeMethods.ElementGetState(element); }
    public void SetState(IntPtr element, int state) { using var borrow = Borrow(); NativeMethods.ElementSetState(element, state); }

    public void SetAttribute(IntPtr element, string name, string value)
    {
        ArgumentException.ThrowIfNullOrEmpty(name);
        ArgumentNullException.ThrowIfNull(value);
        using var borrow = Borrow();
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
        using var borrow = Borrow();
        return EnumerateElements(borrow.Handle).ToArray();
    }

    public IReadOnlyList<KeyValuePair<string, string>> GetAttributes(IntPtr element)
    {
        using var borrow = Borrow();
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
        using (var borrow = Borrow())
        {
            if (_owner is not null) NativeDomSync.Throw(NativeMethods.DomOwnerRetire(_owner));
            else NativeMethods.DocDestroy(borrow.Handle);
        }
        _disposed = true;
        _owner?.Dispose();
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
