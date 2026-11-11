using AetherUI.Runtime;
using System.Security.Cryptography;
using System.Text;
namespace AetherUI.Validator.NativeCapture;
internal readonly record struct ResourceSnapshot(long Epoch,int Pending,int Unapplied,int Failed,int UniqueRequests,string? Error);
internal sealed class TrackedImageResolver : IImageResourceResolver, IDisposable
{
    private sealed class Entry { public ImageResourceState State = ImageResourceState.Pending; public ImageResourceFailure? Failure; public bool Received; }
    private readonly object _gate = new();
    private readonly Dictionary<string,Entry> _entries = new(StringComparer.Ordinal);
    private readonly ImageResourceResolver _inner;
    private readonly QueuedStaContext _context;
    private long _epoch;
    private int _unapplied;
    private string? _error;
    private bool _disposed;
    public AutoResetEvent ChangedReceived { get; } = new(false);
    public event EventHandler<ImageResourceChangedEventArgs>? Changed;
    public TrackedImageResolver(IImageResourceLoader loader,QueuedStaContext context)
    { _context=context; _inner=new(loader); _inner.Changed+=Receive; }
    public void Request(string source)
    {
        _context.RequireOwner();
        if (string.IsNullOrWhiteSpace(source)) throw new InvalidOperationException("Empty image request");
        lock (_gate) { if (_entries.ContainsKey(source)) return; _entries.Add(source,new()); _epoch++; }
        _inner.Request(source);
    }
    private void Receive(object? sender,ImageResourceChangedEventArgs args)
    {
        lock (_gate)
        {
            if (_disposed) return;
            _epoch++; _unapplied++;
            if (!_entries.TryGetValue(args.Source,out var entry) || entry.Received || args.State == ImageResourceState.Pending) _error="Unknown, duplicate or nonterminal Changed";
            else entry.Received=true;
            _context.Post(_=>Apply(args),null);
            ChangedReceived.Set();
        }
    }
    private void Apply(ImageResourceChangedEventArgs args)
    {
        _context.RequireOwner();
        lock (_gate) { if (_error is not null) throw new InvalidOperationException(_error); }
        try { Changed?.Invoke(this,args); }
        catch { lock (_gate) _error="Changed handler failed before terminal application"; throw; }
        lock (_gate)
        {
            var entry=_entries[args.Source];
            if (entry.State != ImageResourceState.Pending) { _error="Terminal applied twice"; throw new InvalidOperationException(_error); }
            entry.State=args.State; entry.Failure=args.Failure; _unapplied--; _epoch++;
        }
    }
    public ResourceSnapshot Snapshot()
    {
        lock (_gate) return new(_epoch,_entries.Values.Count(e=>e.State==ImageResourceState.Pending),_unapplied,
            _entries.Values.Count(e=>e.State==ImageResourceState.Failed),_entries.Count,_error);
    }
    public ResourceSnapshot RequireStable(UiDocumentHost host,long? expectedEpoch=null)
    {
        _context.RequireOwner();
        var snapshot=Snapshot();
        if (snapshot.Pending!=0 || snapshot.Unapplied!=0 || snapshot.Error is not null || host.NeedsRender || _context.PendingCount!=0 || (expectedEpoch.HasValue && snapshot.Epoch!=expectedEpoch.Value))
            throw new InvalidOperationException("Resource/host barrier is not stable");
        return snapshot;
    }
    public object[] Resources() { lock (_gate) return _entries.Select(p=>(object)new { uriSha256=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(p.Key))).ToLowerInvariant(), state=p.Value.State.ToString(), failure=p.Value.Failure?.ToString() }).ToArray(); }
    public bool TryGetDimensions(string source,out ImageDimensions dimensions) { _context.RequireOwner(); return _inner.TryGetDimensions(source,out dimensions); }
    public bool TryGetResource(string source,out ImageResourceResult resource) { _context.RequireOwner(); return _inner.TryGetResource(source,out resource); }
    public void Dispose() { _context.RequireOwner(); lock (_gate) { if (_disposed) return; _disposed=true; _inner.Changed-=Receive; ChangedReceived.Dispose(); } _inner.Dispose(); }
}
