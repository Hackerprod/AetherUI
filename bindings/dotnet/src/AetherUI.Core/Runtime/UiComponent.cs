using AetherUI.Events;

namespace AetherUI.Runtime;

/// <summary>
/// Base type generated for every .cshtml view. Component state remains alive between renders.
/// </summary>
public abstract class UiComponent : IDisposable
{
    // Compatibility lookup for standalone RenderMarkup consumers only. A host pins the
    // registry from its own RenderCandidate, never this last-rendered registry.
    private EventRegistry? _events;
    private long _renderGeneration;
    private long _invalidationRevision;
    private int _renderOwnerThreadId;
    private bool _certificationFailed;
    private bool _disposed;

    public event EventHandler? Invalidated;

    public string RenderMarkup()
    {
        using var candidate = PrepareRenderCandidate();
        var markup = candidate.Markup;
        var registry = candidate.Registry;
        if (!IsRenderCandidateCurrent(candidate))
            throw new InvalidOperationException("The render candidate was superseded.");
        _events = registry;
        return markup;
    }

    /// <summary>
    /// Activates the host's owner-thread domain for every entry that changes render authority.
    /// Call before using the component in a host. Unbound components are not thread-safe.
    /// Binding is permanent; the owning host disposes its component rather than transferring it.
    /// </summary>
    internal void BindRenderOwnerThread()
    {
        var thread = Environment.CurrentManagedThreadId;
        var owner = Interlocked.CompareExchange(ref _renderOwnerThreadId, thread, 0);
        if (owner != 0 && owner != thread)
            throw new InvalidOperationException("The component belongs to another render thread.");
        ObjectDisposedException.ThrowIf(_disposed, this);
    }

    /// <summary>
    /// Runs all user rendering and formatting against private staging. Does not publish
    /// handlers. The host must validate this candidate AND its own DOM/binding/input bases
    /// immediately before publishing DOM and Registry together, without intervening callbacks.
    /// </summary>
    internal RenderCandidate PrepareRenderCandidate()
    {
        VerifyRenderThread();
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_certificationFailed)
            throw new InvalidOperationException("Component render authority is exhausted.");
        var generation = AdvanceAuthority(ref _renderGeneration);
        var revision = _invalidationRevision;
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("component.writer");
#endif
        var writer = new MarkupWriter();
        string markup;
        try
        {
            BuildRenderTree(writer);
            markup = writer.ToString();
        }
        finally
        {
            // User code can retain the writer even when Build or a value's ToString throws.
            writer.Seal();
        }

        var registry = writer.CreateEventRegistry(generation);
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("component.candidate");
#endif
        var candidate = new RenderCandidate(this, generation, revision, markup, registry);
        if (!IsRenderCandidateCurrent(candidate))
        {
            candidate.Dispose();
            throw new InvalidOperationException("The render candidate was superseded.");
        }
        return candidate;
    }

    // Pure, allocation-free precondition, not a substitute for host owner-thread serialization.
    internal bool IsRenderCandidateCurrent(RenderCandidate candidate) =>
        IsRenderThread && !_disposed && !_certificationFailed && !candidate.IsDisposed &&
        ReferenceEquals(candidate.Owner, this) && candidate.Generation == _renderGeneration &&
        candidate.InvalidationRevision == _invalidationRevision;

    private bool IsRenderThread => Volatile.Read(ref _renderOwnerThreadId) is var owner &&
        (owner == 0 || owner == Environment.CurrentManagedThreadId);

    private void VerifyRenderThread()
    {
        if (!IsRenderThread)
            throw new InvalidOperationException("The component belongs to another render thread.");
    }

    private long AdvanceAuthority(ref long counter)
    {
        if (counter == long.MaxValue)
        {
            // Never let an old candidate certify after an unrepresentable invalidation.
            _certificationFailed = true;
            throw new OverflowException("Component render authority is exhausted.");
        }
        return ++counter;
    }

    protected abstract void BuildRenderTree(MarkupWriter writer);

    protected void StateHasChanged()
    {
        VerifyRenderThread();
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_certificationFailed)
            throw new InvalidOperationException("Component render authority is exhausted.");
        AdvanceAuthority(ref _invalidationRevision);
        Invalidated?.Invoke(this, EventArgs.Empty);
    }

    internal bool TryGetEventHandler(string token, out UiEventHandler handler)
    {
        var registry = _events;
        if (registry is not null) return registry.TryGetEventHandler(token, out handler);
        handler = null!;
        return false;
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!disposing) return;
        VerifyRenderThread();
        _disposed = true;
        _events = null;
    }

    public void Dispose()
    {
        VerifyRenderThread();
        if (_disposed) return;
        // Invalidate before calling arbitrary override code, including overrides that omit
        // base.Dispose, reenter rendering or throw. Historical registry pins remain immutable.
        _disposed = true;
        _events = null;
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    internal sealed class RenderCandidate : IDisposable
    {
        private string? _markup;
        private EventRegistry? _registry;

        internal RenderCandidate(UiComponent owner, long generation, long revision,
            string markup, EventRegistry registry)
        {
            Owner = owner;
            Generation = generation;
            InvalidationRevision = revision;
            _markup = markup;
            _registry = registry;
        }

        internal UiComponent Owner { get; }
        internal long Generation { get; }
        internal long InvalidationRevision { get; }
        internal bool IsDisposed => _registry is null;
        internal string Markup => _markup ?? throw new ObjectDisposedException(nameof(RenderCandidate));
        internal EventRegistry Registry => _registry ?? throw new ObjectDisposedException(nameof(RenderCandidate));

        public void Dispose()
        {
            _markup = null;
            _registry = null;
        }
    }

    /// <summary>
    /// Immutable handler data. Its reference is its identity; Generation is scoped to its
    /// component. Retaining this reference pins old handlers for an in-flight dispatch or
    /// loss record, independently of candidate/component disposal. Lookup does not authorize
    /// delivery: a disposed host must discard its records without invoking handlers.
    /// </summary>
    internal sealed class EventRegistry
    {
        private readonly Dictionary<string, UiEventHandler> _handlers;

        // Takes exclusive ownership from a sealed MarkupWriter; never expose this dictionary.
        internal EventRegistry(Dictionary<string, UiEventHandler> handlers, long generation)
        {
            _handlers = handlers;
            Generation = generation;
        }

        internal long Generation { get; }
        internal bool TryGetEventHandler(string token, out UiEventHandler handler) =>
            _handlers.TryGetValue(token, out handler!);
    }
}
