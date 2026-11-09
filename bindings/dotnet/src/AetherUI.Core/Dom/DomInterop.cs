using AetherUI.Core.Native;

namespace AetherUI.Dom;

/// <summary>Exact typed DOM mapping; synchronized mutations are explicitly enabled only
/// after the host has installed its native author slot and staged bookkeeping adapter.</summary>
internal sealed class DomInterop : IDisposable
{
    private Dictionary<IntPtr, Node> _nodesByHandle = [];
    private Dictionary<Node, IntPtr> _handlesByNode = [];
    private NativeDomBinding? _binding;
    private bool _disposed;
    private DomInterop(NativeDomDocument native, DocumentNode document) { Native = native; Document = document; }
    internal static DomInterop CreateStaged(NativeDomDocument native, DocumentNode document) => new(native, document);
    internal void PublishStagedBinding(NativeDomBinding binding) => _binding = binding;
    public NativeDomDocument Native { get; }
    public DocumentNode Document { get; }
    internal IReadOnlyDictionary<IntPtr, Node> InitialNodesCore => _nodesByHandle;
    internal NativeDomBinding? BindingCore => _binding;

    public static DomInterop Parse(string markup)
    {
        ArgumentNullException.ThrowIfNull(markup);
        NativeDomCodec.Validate(markup);
        var native = NativeDomDocument.Parse(markup);
        try
        {
            var interop = new DomInterop(native, new DocumentNode());
            interop.ImportInitial();
            return interop;
        }
        catch { native.Dispose(); throw; }
    }
    // This is intentionally not called from Parse/UiDocumentHost until all native
    // binding/slot/lifecycle prerequisites are wired. NativeFlags remain unchanged.
    internal NativeDomBinding EnableSynchronization(IDomHostBookkeeping bookkeeping)
    {
        ArgumentNullException.ThrowIfNull(bookkeeping);
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_binding is not null) throw new InvalidOperationException("This DOM already has a synchronization adapter.");
        // Loader resolution may run application code. Finish it before capturing
        // binding snapshots/links/versions, and recheck this owner after reentry.
        NativeDomSync.PreparePublicationImports();
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_binding is not null) throw new InvalidOperationException("This DOM already has a synchronization adapter.");
        var binding = new NativeDomBinding(this, bookkeeping);
        _binding = binding;
        _nodesByHandle.Clear(); _handlesByNode.Clear();
        return binding;
    }
    internal IntPtr GetNativeHandleCore(Node node) => _binding is not null ? _binding.GetHandleCore(node) :
        _handlesByNode.TryGetValue(node, out var handle) ? handle : throw new InvalidOperationException("The node does not belong to this native document.");
    public IntPtr GetNativeHandle(ElementNode element) => GetNativeHandleCore(element);
    public long GetStyleNodeId(ElementNode element) => Native.GetStyleNodeId(GetNativeHandleCore(element));
    public ElementNode? GetManagedElement(IntPtr handle) => (_binding is not null ? _binding.GetNodeCore(handle) : _nodesByHandle.GetValueOrDefault(handle)) as ElementNode;

    public void SyncRuntimeValues()
    {
        if (_binding is not null) { _binding.EnsureCurrent(); return; }
        // Compatibility path only until host activation. An active binding never
        // replays its managed runtime override over a newer native authored value.
        foreach (var pair in _handlesByNode)
            if (pair.Key is ElementNode element && element.TagNameCore == "input" && element.RuntimeValueCore is { } value)
                Native.SetAttribute(pair.Value, "value", value);
    }
    public void Dispose()
    {
        if (_disposed) return;
        _binding?.Dispose();
        _disposed = true;
        Native.Dispose();
    }
    private void ImportInitial()
    {
        using var snapshot = NativeDomSnapshot.Acquire(Native.Handle);
        var nodes = new Node[snapshot.Nodes.Length];
        var root = -1;
        for (var i = 0; i < nodes.Length; i++)
        {
            var data = snapshot.Nodes[i];
            nodes[i] = data.Type switch
            {
                (int)NodeType.Document when data.Identity == snapshot.Token.OwnerIdentity => Document,
                (int)NodeType.Element => new ElementNode(data.Tag),
                (int)NodeType.Text => new TextNode(data.Text),
                (int)NodeType.Comment => new CommentNode(data.Text),
                _ => throw new InvalidOperationException("Unexpected native DOM node type."),
            };
            NativeDomWorkCounters.RecordNodesImported(1);
            NativeDomWorkCounters.RecordTraversal((ulong)data.Attributes.Count, (ulong)data.Children.Length);
            if (ReferenceEquals(nodes[i], Document)) root = i;
            _nodesByHandle.Add(data.Handle, nodes[i]);
            _handlesByNode.Add(nodes[i], data.Handle);
            if (nodes[i] is ElementNode element) element.PublishImportedInputs(data.Attributes, (ElementState)data.State, data.RuntimeValue);
        }
        if (root < 0) throw new InvalidOperationException("Native snapshot lacks its document root.");
        for (var i = 0; i < nodes.Length; i++)
        {
            var data = snapshot.Nodes[i];
            var children = new Node[data.Children.Length];
            for (var child = 0; child < children.Length; child++) children[child] = nodes[data.Children[child]];
            nodes[i].PublishImportedLinks(data.ParentIndex < 0 ? null : nodes[(int)data.ParentIndex], children, null);
        }
        Document.PublishVersions(Math.Max(0, nodes.Length - 1), Math.Max(0, nodes.Length - 1));
    }
}
