namespace AetherUI.Core.Native;

public sealed unsafe partial class StyleRuntimeInterop
{
    private IntPtr _domStyleBinding;
    private readonly int _domBindingOwnerThread = Environment.CurrentManagedThreadId;

    private void VerifyDomBindingThread()
    {
        if (Environment.CurrentManagedThreadId != _domBindingOwnerThread)
            throw new InvalidOperationException("The DOM style binding belongs to another thread.");
    }

    private void RejectLoadWithDomBinding()
    {
        if (_domStyleBinding != IntPtr.Zero)
            throw new InvalidOperationException("A bound DOM must be replaced through a prepared binding transaction.");
    }

    /// <summary>
    /// Prepares the exact document's source slot, element index and empty projection cache.
    /// Replacement preserves the runtime and sheets not owned by this binding. The host must
    /// stage all other state, validate its inputs, then call Commit immediately before its
    /// own no-callback reference swaps. Dispose retires old resources after that segment.
    /// </summary>
    internal PreparedDomBinding PrepareDomBinding(NativeDomDocument document)
    {
        VerifyDomBindingThread();
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(document);
        if (_domStyleBinding == IntPtr.Zero && _document != IntPtr.Zero)
            throw new InvalidOperationException("A legacy-loaded runtime cannot acquire a DOM source binding.");
        EnsureDomDefaults();
        return new PreparedDomBinding(this, document);
    }

    // Persistent initialization, separate from candidate publication. The host calls this
    // before capturing its input bases, so any explicit stylesheet diagnostics are accounted for.
    internal void EnsureDomDefaults()
    {
        VerifyDomBindingThread();
        ThrowIfDisposed();
        if (_defaults != 0) return;
        long defaults = 0;
        Utf8(ReadDefaultsCss(), source => Utf8("aether-defaults.css", name =>
            NativeDomSync.Throw(NativeMethods.StyleRuntimeAddStyleSheet(
                _runtime, source, 0, name, out defaults, out _))));
        _defaults = defaults;
    }

    internal PreparedDomIndex PrepareDomIndex(NativeDomSnapshot snapshot) => new(this, snapshot);

    internal sealed class PreparedDomIndex
    {
        private readonly StyleRuntimeInterop _owner;
        private readonly IntPtr _document;
        private readonly Dictionary<IntPtr, int> _baseIndex;
        private readonly List<IntPtr> _elements = [];
        private readonly Dictionary<IntPtr, int> _index = [];
        internal PreparedDomIndex(StyleRuntimeInterop owner, NativeDomSnapshot snapshot)
        {
            owner.VerifyDomBindingThread();
            owner.ThrowIfDisposed();
            _owner = owner;
            _document = owner._document;
            _baseIndex = owner._elementIndex;
            var root = FindSnapshotRoot(snapshot);
            if (snapshot.Nodes[root].Handle != _document)
                throw new InvalidOperationException("The index snapshot belongs to another document.");
            BuildDomIndex(snapshot, root, _elements, _index);
        }
        internal void Validate()
        {
            _owner.VerifyDomBindingThread();
            if (_owner._disposed || _owner._document != _document || !ReferenceEquals(_owner._elementIndex, _baseIndex))
                throw new InvalidOperationException("The runtime index base changed during preparation.");
        }
        // Only called in the binding's owner-thread commit after Validate. No late checks.
        internal void Publish()
        {
            _owner._elements = _elements;
            _owner._elementIndex = _index;
        }
    }

    private static int FindSnapshotRoot(NativeDomSnapshot snapshot)
    {
        for (var i = 0; i < snapshot.Nodes.Length; i++)
            if (snapshot.Nodes[i].Identity == snapshot.Token.OwnerIdentity) return i;
        throw new InvalidOperationException("DOM snapshot has no root.");
    }

    private static void BuildDomIndex(NativeDomSnapshot snapshot, int root,
        List<IntPtr> elements, Dictionary<IntPtr, int> index)
    {
        foreach (var child in snapshot.Nodes[root].Children)
        {
            var node = snapshot.Nodes[child];
            if (!node.Alive || !node.Attached) continue;
            if (node.Type == 1)
            {
                index.Add(node.Handle, elements.Count);
                elements.Add(node.Handle);
            }
            BuildDomIndex(snapshot, child, elements, index);
        }
    }

    internal (long Handle, bool Present, ulong SheetEpoch) ReadDomSourceSlot()
    {
        VerifyDomBindingThread();
        ThrowIfDisposed();
        NativeDomSync.Throw(NativeMethods.DomStyleBindingReadSlot(_domStyleBinding, out var handle, out var present, out var epoch));
        return (handle, present != 0, epoch);
    }

    internal sealed class PreparedDomBinding : IDisposable
    {
        private readonly StyleRuntimeInterop _owner;
        private readonly IntPtr _runtime;
        private readonly IntPtr _previousBinding;
        private readonly IntPtr _previousDocument;
        private readonly IntPtr _document;
        private readonly Dictionary<IntPtr, NativeComputedStyle> _styles = [];
        private readonly Dictionary<IntPtr, int> _index = [];
        private readonly List<IntPtr> _elements = [];
        private NativeDomSnapshot? _snapshot;
        private IntPtr _transaction;
        private IntPtr _retiredBinding;
        private IntPtr _retiredRevision;
        private bool _committed;
        private bool _disposed;

        internal PreparedDomBinding(StyleRuntimeInterop owner, NativeDomDocument document)
        {
            _owner = owner;
            _runtime = owner._runtime;
            _previousBinding = owner._domStyleBinding;
            _previousDocument = owner._document;
            _document = document.Handle;
            try
            {
                // Snapshot pins all storage until validation/publication finishes. A disposed
                // NativeDomDocument can invalidate this stage but cannot leave a dangling root.
                _snapshot = NativeDomSnapshot.Acquire(_document);
                BuildDomIndex(_snapshot, FindSnapshotRoot(_snapshot), _elements, _index);
                if (_previousBinding != IntPtr.Zero)
                    NativeDomSync.Throw(NativeMethods.DomStyleBindingPrepareRebind(_previousBinding, _document, out _transaction));
                Validate();
            }
            catch { Dispose(); throw; }
        }

        internal void Validate()
        {
            _owner.VerifyDomBindingThread();
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (_committed || _owner._disposed || _owner._runtime != _runtime ||
                _owner._domStyleBinding != _previousBinding || _owner._document != _previousDocument)
                throw new InvalidOperationException("The DOM binding base changed during preparation.");
            if (NativeDomSync.ReadToken(_document) != _snapshot!.Token)
                throw new InvalidOperationException("The candidate document changed during preparation.");
            if (_transaction != IntPtr.Zero)
                NativeDomSync.Throw(NativeMethods.DomStyleBindingValidateRebind(_transaction));
        }

        internal void Commit()
        {
            Validate();
            IntPtr binding;
            // Initial create is the last fallible operation before the first publication.
            // It has the native strong guarantee. Replacement commit uses only preallocated
            // native storage after validating both DOMs, runtime, sheets and binding identity.
            var status = _transaction != IntPtr.Zero
                ? NativeMethods.DomStyleBindingCommitRebind(_transaction, out binding)
                : NativeMethods.DomStyleBindingCreate(_document, _runtime, out binding);
            NativeDomSync.Throw(status);
            _retiredBinding = _owner._domStyleBinding;
            _retiredRevision = _owner._managedPublicationRevision;
            _owner._domStyleBinding = binding;
            _owner._document = _document;
            _owner._elements = _elements;
            _owner._elementIndex = _index;
            _owner._styles = _styles;
            _owner._managedPublicationRevision = IntPtr.Zero;
            _committed = true;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _owner.VerifyDomBindingThread();
            _disposed = true;
            if (_transaction != IntPtr.Zero) NativeMethods.DomStyleBindingRebindDestroy(_transaction);
            if (_retiredBinding != IntPtr.Zero) NativeMethods.DomStyleBindingDestroy(_retiredBinding);
            if (_retiredRevision != IntPtr.Zero) NativeMethods.StyleRevisionDestroy(_retiredRevision);
            _transaction = _retiredBinding = _retiredRevision = IntPtr.Zero;
            _snapshot?.Dispose();
            _snapshot = null;
        }
    }
}
