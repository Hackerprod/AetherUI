using System.Collections.Concurrent;
namespace AetherUI.Validator.NativeCapture;
internal sealed class QueuedStaContext : SynchronizationContext, IDisposable
{
    private readonly int _owner = Environment.CurrentManagedThreadId;
    private readonly SynchronizationContext? _previous = Current;
    private readonly ConcurrentQueue<(SendOrPostCallback Callback, object? State)> _queue = new();
    private readonly object _lifetime = new();
    private bool _disposed;
    private readonly AutoResetEvent _work = new(false);
    public QueuedStaContext() => SetSynchronizationContext(this);
    public int PendingCount => _queue.Count;
    public void RequireOwner() { if (Environment.CurrentManagedThreadId != _owner || Current != this) throw new InvalidOperationException("STA owner/context required"); }
    public override void Post(SendOrPostCallback d, object? state) { lock (_lifetime) { if (_disposed) return; _queue.Enqueue((d,state)); _work.Set(); } }
    public override void Send(SendOrPostCallback d, object? state) { RequireOwner(); d(state); }
    public void Drain() { RequireOwner(); while (_queue.TryDequeue(out var item)) item.Callback(item.State); }
    public void WaitForWork(TimeSpan timeout) { RequireOwner(); _work.WaitOne(timeout); }
    public void Dispose() { RequireOwner(); lock (_lifetime) { _disposed=true; _work.Dispose(); } SetSynchronizationContext(_previous); }
}
