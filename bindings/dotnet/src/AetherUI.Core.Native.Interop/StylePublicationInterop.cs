using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeStylePublicationToken(
    uint StructSize, int InputsCurrent, ulong RevisionIdentity, ulong Generation,
    ulong DocumentIdentity, ulong InputEpoch, ulong StructureEpoch, ulong StyleTreeEpoch,
    ulong SheetEpoch, ulong RegistryIdentity)
{
    internal static NativeStylePublicationToken Empty => new((uint)Marshal.SizeOf<NativeStylePublicationToken>(), 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

internal static unsafe partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_prepare_revision")]
    internal static partial int StylePrepareRevision(IntPtr runtime, IntPtr root, in StyleRuntimeInterop.RuntimeEnvironment environment, out IntPtr transaction);
    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_commit_revision")]
    internal static partial int StyleCommitRevision(IntPtr runtime, IntPtr transaction, out IntPtr revision);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_destroy")]
    internal static partial void StyleTransactionDestroy(IntPtr transaction);
    [LibraryImport(Lib, EntryPoint = "aether_style_revision_destroy")]
    internal static partial void StyleRevisionDestroy(IntPtr revision);
    [LibraryImport(Lib, EntryPoint = "aether_style_revision_style_count")]
    internal static partial int StyleRevisionCount(IntPtr revision);
    [LibraryImport(Lib, EntryPoint = "aether_style_revision_change_count")]
    internal static partial int StyleRevisionChangeCount(IntPtr revision);
    [LibraryImport(Lib, EntryPoint = "aether_style_revision_get_style")]
    internal static partial int StyleRevisionGetStyle(IntPtr revision, int index, out IntPtr node, out IntPtr style, out uint impact);
    [LibraryImport(Lib, EntryPoint = "aether_style_revision_get_change")]
    internal static partial int StyleRevisionGetChange(IntPtr revision, int index, out IntPtr node, out IntPtr style, out uint impact);
    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_read_publication_token")]
    internal static partial int StyleReadPublicationToken(IntPtr runtime, ref NativeStylePublicationToken token);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_read_publication_token")]
    internal static partial int StyleTransactionReadPublicationToken(IntPtr transaction, ref NativeStylePublicationToken token);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_preview_revision")]
    internal static partial int StyleTransactionPreviewRevision(IntPtr transaction, out IntPtr revision);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_preview_legacy_map")]
    internal static partial int StyleTransactionPreviewLegacyMap(IntPtr transaction, out IntPtr map);
}

public sealed unsafe partial class StyleRuntimeInterop
{
    private IntPtr _managedPublicationRevision;

    /// <summary>Pure owner-thread inspection. No diagnostics, allocation, or implicit resolve.</summary>
    public bool IsCurrent(NativeStylePublicationToken expected)
    {
        if (_disposed) return false;
        var current = NativeStylePublicationToken.Empty;
        return NativeMethods.StyleReadPublicationToken(_runtime, ref current) == 0 &&
            current.InputsCurrent != 0 && current == expected;
    }

    /// <summary>Full, fallible staging. Never enables the experimental state resolver.</summary>
    public PreparedPublication PreparePublication(IReadOnlyList<int>? states, float width, float height, float dpiScale)
    {
        ThrowIfDisposed();
        if (states is not null && states.Count != _elements.Count)
            throw new InvalidOperationException("Managed and native DOM element counts differ.");
        if (states is not null)
            for (var i = 0; i < states.Count; ++i) NativeMethods.ElementSetState(_elements[i], states[i]);
        var environment = new RuntimeEnvironment(width, height, dpiScale, 16, 0, 0, 0);
        Throw(NativeMethods.StylePrepareRevision(_runtime, _document, in environment, out var transaction));
        try { return new PreparedPublication(this, transaction); }
        catch { NativeMethods.StyleTransactionDestroy(transaction); throw; }
    }

    /// <summary>Prepared styles/map are staging-only until Commit and IsCurrent succeed.</summary>
    public sealed class PreparedPublication : IDisposable
    {
        private readonly StyleRuntimeInterop _owner;
        private IntPtr _transaction;
        private IntPtr _preview;
        private IntPtr _committed;
        private NativeStyleMap? _map;
        private readonly Dictionary<IntPtr, NativeComputedStyle> _styles;
        public IReadOnlyList<NativeStyleUpdate> Styles { get; }
        public NativeStyleImpact Impact { get; }
        public bool StackingOrderChanged { get; }
        public NativeStylePublicationToken Token { get; }

        internal PreparedPublication(StyleRuntimeInterop owner, IntPtr transaction)
        {
            _owner = owner;
            // The caller owns transaction until construction succeeds.
            try
            {
                var token = NativeStylePublicationToken.Empty;
                Throw(NativeMethods.StyleTransactionReadPublicationToken(transaction, ref token));
                Token = token;
                Throw(NativeMethods.StyleTransactionPreviewRevision(transaction, out _preview));
                var count = NativeMethods.StyleRevisionCount(_preview);
                var updates = new NativeStyleUpdate[count];
                _styles = new Dictionary<IntPtr, NativeComputedStyle>(count);
                var aggregate = NativeStyleImpact.None;
                for (var i = 0; i < count; ++i)
                {
                    Throw(NativeMethods.StyleRevisionGetStyle(_preview, i, out var node, out var handle, out var impact));
                    try
                    {
                        var style = ReadStyle(handle);
                        updates[i] = new NativeStyleUpdate(node, style, (NativeStyleImpact)impact);
                        _styles.Add(node, style);
                        aggregate |= (NativeStyleImpact)impact;
                        StackingOrderChanged |= owner._styles.TryGetValue(node, out var previous) && previous.ZIndex != style.ZIndex;
                    }
                    finally { NativeMethods.ComputedStyleHandleDestroy(handle); }
                }
                // Includes retired/generated entries, without projecting them as live nodes.
                var changes = NativeMethods.StyleRevisionChangeCount(_preview);
                for (var i = 0; i < changes; ++i)
                {
                    Throw(NativeMethods.StyleRevisionGetChange(_preview, i, out _, out var handle, out var impact));
                    aggregate |= (NativeStyleImpact)impact;
                    if (handle != IntPtr.Zero) NativeMethods.ComputedStyleHandleDestroy(handle);
                }
                Styles = updates; Impact = aggregate;
                Throw(NativeMethods.StyleTransactionPreviewLegacyMap(transaction, out var map));
                _map = new NativeStyleMap(map);
                _transaction = transaction;
            }
            catch { Dispose(); throw; }
        }

        public bool Commit()
        {
            ObjectDisposedException.ThrowIf(_transaction == IntPtr.Zero, this);
            if (_committed != IntPtr.Zero) throw new InvalidOperationException("Style publication was already committed.");
            Throw(NativeMethods.StyleCommitRevision(_owner._runtime, _transaction, out _committed));
            return _owner.IsCurrent(Token);
        }

        // No allocations/callbacks: only the host's certified publication segment calls this.
        public NativeStyleMap Publish()
        {
            if (_committed == IntPtr.Zero || _map is null) throw new InvalidOperationException("Style publication is not committed.");
            _owner._styles = _styles;
            var oldRevision = _owner._managedPublicationRevision;
            _owner._managedPublicationRevision = _committed; _committed = IntPtr.Zero;
            if (oldRevision != IntPtr.Zero) NativeMethods.StyleRevisionDestroy(oldRevision);
            var map = _map; _map = null;
            return map;
        }

        public void Dispose()
        {
            _map?.Dispose(); _map = null;
            if (_committed != IntPtr.Zero) NativeMethods.StyleRevisionDestroy(_committed);
            if (_preview != IntPtr.Zero) NativeMethods.StyleRevisionDestroy(_preview);
            if (_transaction != IntPtr.Zero) NativeMethods.StyleTransactionDestroy(_transaction);
            _committed = _preview = _transaction = IntPtr.Zero;
        }
    }
}
