using System.Collections.Concurrent;

namespace AetherUI.Runtime;

public enum ImageResourceState { Pending, Ready, Failed }
public enum ImageResourceFailure { Policy, Network, Decode }

public readonly record struct ImageDimensions(int Width, int Height);
/// <summary>
/// A decoded image in the canonical binding-to-renderer representation: contiguous BGRA8 pixels
/// with premultiplied alpha, in top-to-bottom rows, using <c>Width * 4</c> bytes per row.
/// </summary>
public sealed record ImageResourceResult(ImageDimensions Dimensions, ReadOnlyMemory<byte> Pixels);
public sealed class ImageResourceChangedEventArgs(string source, ImageResourceState state, ImageResourceFailure? failure) : EventArgs
{
    public string Source { get; } = source;
    public ImageResourceState State { get; } = state;
    public ImageResourceFailure? Failure { get; } = failure;
}

/// <summary>Application-owned I/O/decode policy. The UI core never fetches a URL by itself.</summary>
public interface IImageResourceLoader
{
    /// <summary>
    /// Loads and decodes one image under the embedding application's resource policy.
    /// Throw <see cref="UnauthorizedAccessException"/> for policy rejection,
    /// <see cref="InvalidDataException"/> for an unsupported or corrupt image, and any other exception
    /// for a network/resource failure. The resolver exposes those as Policy, Decode, and Network respectively.
    /// </summary>
    ValueTask<ImageResourceResult> LoadAsync(string source, CancellationToken cancellationToken);
}

public interface IImageResourceResolver
{
    event EventHandler<ImageResourceChangedEventArgs>? Changed;
    void Request(string source);
    bool TryGetDimensions(string source, out ImageDimensions dimensions);
    bool TryGetResource(string source, out ImageResourceResult resource);
}

public sealed class ImageResourceResolver : IImageResourceResolver, IDisposable
{
    private sealed class Entry { public ImageResourceState State; public ImageResourceResult? Result; }
    private readonly IImageResourceLoader _loader;
    private readonly ConcurrentDictionary<string, Entry> _entries = new(StringComparer.Ordinal);
    private readonly CancellationTokenSource _shutdown = new();
    public event EventHandler<ImageResourceChangedEventArgs>? Changed;

    public ImageResourceResolver(IImageResourceLoader loader) => _loader = loader ?? throw new ArgumentNullException(nameof(loader));

    public void Request(string source)
    {
        if (string.IsNullOrWhiteSpace(source)) return;
        // A render can request the same failed URL every frame. Cache failure until the resolver is replaced
        // so a transient error cannot turn into an unbounded retry loop; retry policy remains host-owned.
        if (!_entries.TryAdd(source, new Entry { State = ImageResourceState.Pending })) return;
        _ = LoadAsync(source);
    }

    public bool TryGetDimensions(string source, out ImageDimensions dimensions)
    {
        if (_entries.TryGetValue(source, out var entry) && entry.State == ImageResourceState.Ready && entry.Result is { } result)
        { dimensions = result.Dimensions; return true; }
        dimensions = default; return false;
    }

    public bool TryGetResource(string source, out ImageResourceResult resource)
    {
        if (_entries.TryGetValue(source, out var entry) && entry.State == ImageResourceState.Ready && entry.Result is { } result)
        {
            resource = result;
            return true;
        }
        resource = null!;
        return false;
    }

    private async Task LoadAsync(string source)
    {
        try
        {
            var result = await _loader.LoadAsync(source, _shutdown.Token).ConfigureAwait(false);
            _entries[source] = new Entry { State = ImageResourceState.Ready, Result = result };
            Changed?.Invoke(this, new(source, ImageResourceState.Ready, null));
        }
        catch (OperationCanceledException) when (_shutdown.IsCancellationRequested) { }
        catch (UnauthorizedAccessException) { Fail(source, ImageResourceFailure.Policy); }
        catch (InvalidDataException) { Fail(source, ImageResourceFailure.Decode); }
        catch { Fail(source, ImageResourceFailure.Network); }
    }

    private void Fail(string source, ImageResourceFailure failure)
    {
        _entries[source] = new Entry { State = ImageResourceState.Failed };
        Changed?.Invoke(this, new(source, ImageResourceState.Failed, failure));
    }

    public void Dispose() => _shutdown.Cancel();
}
