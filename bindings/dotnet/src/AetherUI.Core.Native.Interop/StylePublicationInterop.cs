using System.Collections.Immutable;
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

[StructLayout(LayoutKind.Sequential)]
internal struct NativePublicationDelta
{
    internal uint StructSize;
    internal int Compact;
    internal int SemanticCount;
    internal uint Impact;
}

internal static unsafe partial class NativeMethods
{
    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_prepare_state_revision")]
    internal static partial int StylePrepareStateRevision(IntPtr runtime, IntPtr root, IntPtr baseRevision, IntPtr changes, int count, in StyleRuntimeInterop.RuntimeEnvironment environment, out IntPtr transaction);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_publication_delta")]
    internal static partial int StylePublicationDelta(IntPtr transaction, IntPtr baseMap, ref NativePublicationDelta result);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_get_publication_delta")]
    internal static partial int StylePublicationGetDelta(IntPtr transaction, IntPtr baseMap, int index, out IntPtr node, out IntPtr style, out uint impact);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_preview_compact_map")]
    internal static partial int StylePublicationCompactMap(IntPtr transaction, IntPtr baseMap, out IntPtr map);
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
    [LibraryImport(Lib, EntryPoint = "aether_style_revision_get_change")]
    internal static partial int StyleRevisionGetChangeImpact(IntPtr revision, int index, IntPtr node, IntPtr style, out uint impact);
    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_read_publication_token")]
    internal static partial int StyleReadPublicationToken(IntPtr runtime, ref NativeStylePublicationToken token);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_read_publication_token")]
    internal static partial int StyleTransactionReadPublicationToken(IntPtr transaction, ref NativeStylePublicationToken token);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_preview_revision")]
    internal static partial int StyleTransactionPreviewRevision(IntPtr transaction, out IntPtr revision);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_preview_legacy_patch")]
    internal static partial int StyleTransactionPreviewLegacyPatch(IntPtr transaction, IntPtr baseMap, out IntPtr map);
    [LibraryImport(Lib, EntryPoint = "aether_style_transaction_get_publication_style")]
    internal static partial int StyleTransactionGetPublicationStyle(IntPtr transaction, IntPtr baseMap, int index, out IntPtr node, out IntPtr style, out uint impact);
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

    /// <summary>Fallible staging. The default uses the full resolver; the internal opt-in
    /// requests state deltas, still subject to native feature gates and certified coverage.</summary>
    public PreparedPublication PreparePublication(IReadOnlyList<int>? states, float width, float height, float dpiScale, NativeStyleMap? previousMap = null, bool experimentalDelta = false)
    {
        ThrowIfDisposed();
        if (states is not null && states.Count != _elements.Count)
            throw new InvalidOperationException("Managed and native DOM element counts differ.");
        if (states is not null)
            for (var i = 0; i < states.Count; ++i) NativeMethods.ElementSetState(_elements[i], states[i]);
        var environment = new RuntimeEnvironment(width, height, dpiScale, 16, 0, 0, 0);
        IntPtr transaction;
        if (experimentalDelta && states is null && _managedPublicationRevision != IntPtr.Zero)
            Throw(NativeMethods.StylePrepareStateRevision(_runtime, _document, _managedPublicationRevision, IntPtr.Zero, 0, in environment, out transaction));
        else Throw(NativeMethods.StylePrepareRevision(_runtime, _document, in environment, out transaction));
        try { return new PreparedPublication(this, transaction, previousMap, experimentalDelta); }
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
        private readonly ImmutableDictionary<IntPtr, NativeComputedStyle> _styles;
        public IReadOnlyList<NativeStyleUpdate> Styles { get; }
        public NativeStyleImpact Impact { get; }
        public bool StackingOrderChanged { get; }
        public NativeStylePublicationToken Token { get; }
        public bool MapPatched { get; }
        public bool Compact { get; }
        public bool MapChanged => !Compact || Styles.Count != 0;
        public int ChangedStyleCount { get; }

        internal PreparedPublication(StyleRuntimeInterop owner, IntPtr transaction, NativeStyleMap? previousMap, bool experimentalDelta)
        {
            _owner = owner;
            // The caller owns transaction until construction succeeds.
            try
            {
                var token = NativeStylePublicationToken.Empty;
                Throw(NativeMethods.StyleTransactionReadPublicationToken(transaction, ref token));
                Token = token;
                Throw(NativeMethods.StyleTransactionPreviewRevision(transaction, out _preview));
                var delta = new NativePublicationDelta { StructSize = (uint)Marshal.SizeOf<NativePublicationDelta>() };
                if (experimentalDelta && previousMap is not null)
                    Throw(NativeMethods.StylePublicationDelta(transaction, previousMap.Handle, ref delta));
                Compact = delta.Compact != 0;
                var count = Compact ? delta.SemanticCount : NativeMethods.StyleRevisionCount(_preview);
                var updates = new NativeStyleUpdate[count];
                _styles = Compact ? owner._styles : ImmutableDictionary<IntPtr, NativeComputedStyle>.Empty;
                var aggregate = Compact ? (NativeStyleImpact)delta.Impact : NativeStyleImpact.None;
                for (var i = 0; i < count; ++i)
                {
                    IntPtr node, handle; uint impact;
                    if (Compact) Throw(NativeMethods.StylePublicationGetDelta(transaction, previousMap!.Handle, i, out node, out handle, out impact));
                    else Throw(NativeMethods.StyleTransactionGetPublicationStyle(transaction, previousMap?.Handle ?? IntPtr.Zero, i, out node, out handle, out impact));
                    try
                    {
                        var changed = handle != IntPtr.Zero;
                        var style = changed ? ReadStyle(handle) : owner._styles[node];
                        if (changed) ++ChangedStyleCount;
                        updates[i] = new NativeStyleUpdate(node, style, (NativeStyleImpact)impact) { ProjectionChanged = changed };
                        _styles = _styles.SetItem(node, style);
                        aggregate |= (NativeStyleImpact)impact;
                        StackingOrderChanged |= owner._styles.TryGetValue(node, out var previous) && previous.ZIndex != style.ZIndex;
                    }
                    finally { if (handle != IntPtr.Zero) NativeMethods.ComputedStyleHandleDestroy(handle); }
                }
                // Includes retired/generated entries, without projecting them as live nodes.
                var changes = Compact ? 0 : NativeMethods.StyleRevisionChangeCount(_preview);
                for (var i = 0; i < changes; ++i)
                {
                    Throw(NativeMethods.StyleRevisionGetChangeImpact(_preview, i, IntPtr.Zero, IntPtr.Zero, out var impact));
                    aggregate |= (NativeStyleImpact)impact;
                }
                Styles = updates; Impact = aggregate;
                IntPtr map;
                if (Compact) Throw(NativeMethods.StylePublicationCompactMap(transaction, previousMap!.Handle, out map));
                else if (previousMap is null) Throw(NativeMethods.StyleTransactionPreviewLegacyMap(transaction, out map));
                else Throw(NativeMethods.StyleTransactionPreviewLegacyPatch(transaction, previousMap.Handle, out map));
                MapPatched = previousMap is not null;
                try { _map = new NativeStyleMap(map); }
                catch { NativeMethods.StyleMapDestroy(map); throw; }
                _transaction = transaction;
            }
            catch { Dispose(); throw; }
        }

        public bool Commit()
        {
            ObjectDisposedException.ThrowIf(_transaction == IntPtr.Zero, this);
            if (_committed != IntPtr.Zero) throw new InvalidOperationException("Style publication was already committed.");
            var status = NativeMethods.StyleCommitRevision(_owner._runtime, _transaction, out _committed);
            if (status == -2) return false; // Inputs/base changed. Host retains B and permits one recovery.
            Throw(status); // Allocation/marshalling failures never become zero updates or automatic retries.
            return _owner.IsCurrent(Token);
        }

        // No allocations/callbacks: only the host's certified publication segment calls this.
        public NativeStyleMap Publish()
        {
            if (_committed == IntPtr.Zero || _map is null) throw new InvalidOperationException("Style publication is not committed.");
            if (!_owner.IsCurrent(Token)) throw new InvalidOperationException("A newer native revision or input superseded this publication.");
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
