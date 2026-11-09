#if AETHER_DOM_FAULT_TESTING
namespace AetherUI.Core.Native;

// Entire type/state is absent in normal builds. This is source-operation-site
// injection, NOT a replacement CLR/BCL allocator. Before/Arm/factories never run
// in a commit. Resource counters are scalar-only observation, not fault points.
internal static class ManagedDomFault
{
    [ThreadStatic] private static Session? _current;
    private static int _invalidResources;
    private static long _snapshots, _edits, _leases, _owners, _interactionOwners, _claims, _interactionEdits, _buffers, _bookkeeping;
    internal enum Resource { Snapshot, Edit, NodeLease, Owner, InteractionOwner, InteractionClaim, InteractionEdit, NativeBuffer, Bookkeeping }
    internal readonly record struct Balance(long Snapshots, long Edits, long Leases, long Owners,
        long InteractionOwners, long Claims, long InteractionEdits, long Buffers, long Bookkeeping, bool Valid);
    internal static Balance Resources => new(Interlocked.Read(ref _snapshots), Interlocked.Read(ref _edits),
        Interlocked.Read(ref _leases), Interlocked.Read(ref _owners), Interlocked.Read(ref _interactionOwners),
        Interlocked.Read(ref _claims), Interlocked.Read(ref _interactionEdits), Interlocked.Read(ref _buffers), Interlocked.Read(ref _bookkeeping), Volatile.Read(ref _invalidResources) == 0);
    internal static void ResourceAcquired(Resource kind)
    {
        if (Interlocked.Increment(ref Counter(kind)) <= 0) Interlocked.Exchange(ref _invalidResources, 1);
    }
    internal static void ResourceReleased(Resource kind)
    {
        var value = Interlocked.Decrement(ref Counter(kind));
        if (value < 0 || value == long.MaxValue) Interlocked.Exchange(ref _invalidResources, 1);
    }
    private static ref long Counter(Resource kind)
    {
        switch (kind)
        {
            case Resource.Snapshot: return ref _snapshots;
            case Resource.Edit: return ref _edits;
            case Resource.NodeLease: return ref _leases;
            case Resource.Owner: return ref _owners;
            case Resource.InteractionOwner: return ref _interactionOwners;
            case Resource.InteractionClaim: return ref _claims;
            case Resource.InteractionEdit: return ref _interactionEdits;
            case Resource.NativeBuffer: return ref _buffers;
            case Resource.Bookkeeping: return ref _bookkeeping;
            default: throw new ArgumentOutOfRangeException(nameof(kind));
        }
    }
    internal static Session Arm(int failAt = 0, int capacity = 16384)
    {
        if (_current is not null) throw new InvalidOperationException("Nested DOM fault session.");
        ArgumentOutOfRangeException.ThrowIfNegative(failAt);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(capacity);
        var session = new Session(failAt, capacity);
        _current = session;
        return session;
    }
    internal static Session ArmSite(string site, int occurrence, int capacity = 16384)
    {
        ArgumentNullException.ThrowIfNull(site);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(occurrence);
        var session = Arm(0, capacity);
        session.TargetSite = site; session.TargetOccurrence = occurrence;
        return session;
    }
    internal static void Before(string site)
    {
        var session = _current;
        if (session is null) return;
        if (session.Count == session.Sites.Length) { session.Overflowed = true; throw session.Overflow; }
        session.Sites[session.Count++] = site;
        var selected = session.TargetSite is null ? session.Count == session.FailAt :
            string.Equals(site, session.TargetSite, StringComparison.Ordinal) && ++session.MatchingSites == session.TargetOccurrence;
        if (selected) { session.Fired = true; throw session.Failure; }
    }
    internal static void BeforeDictionaryInsert<TKey, TValue>(Dictionary<TKey, TValue> map, TKey key, string site) where TKey : notnull
    {
        // Capacity growth only. Collision-triggered BCL rehash internals are not intercepted.
        if (!map.ContainsKey(key) && map.Count == map.EnsureCapacity(0)) Before(site);
    }
    internal static void BeforeListAdd<T>(List<T> list, string site) { if (list.Count == list.Capacity) Before(site); }
    internal static void BeforeListRange<T>(List<T> list, int count, string site)
    { if ((long)list.Count + count > list.Capacity) Before(site); }
    internal static T[] Array<T>(int length, string site) { Before(site); return new T[length]; }
    internal static Dictionary<TKey, TValue> Dictionary<TKey, TValue>(IEqualityComparer<TKey> comparer, string site) where TKey : notnull
    { Before(site); return new Dictionary<TKey, TValue>(comparer); }
    internal sealed class Session : IDisposable
    {
        internal readonly string[] Sites;
        internal readonly int FailAt;
        internal readonly OutOfMemoryException Failure = new("Injected managed DOM allocation-capable operation failure.");
        internal readonly InvalidOperationException Overflow = new("DOM fault trace capacity exhausted; coverage invalid.");
        internal int Count;
        internal string? TargetSite;
        internal int TargetOccurrence, MatchingSites;
        internal bool Fired, Overflowed;
        private readonly int _thread = Environment.CurrentManagedThreadId;
        private bool _ended;
        internal Session(int failAt, int capacity) { FailAt = failAt; Sites = new string[capacity]; }
        public void Dispose()
        {
            if (_ended) return;
            if (_thread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("DOM fault session disposed on another thread.");
            if (!ReferenceEquals(_current, this)) throw new InvalidOperationException("DOM fault session lost its active scope.");
            _current = null; _ended = true;
            if (Overflowed) throw Overflow;
            if ((FailAt != 0 || TargetSite is not null) && !Fired) throw new InvalidOperationException("Requested DOM fault occurrence was not reached.");
        }
    }
}
#endif
