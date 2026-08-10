using AetherUI.Core.Native;
using AetherUI.Runtime;

namespace AetherUI.Windows.Resources;

/// <summary>
/// Opt-in HTTP image loader for Windows hosts. It performs no work until an application explicitly
/// supplies it to <see cref="ImageResourceResolver"/>. Callers may further restrict URIs through
/// <paramref name="isAllowed"/>; authentication, redirects, and network policy remain host-owned
/// through the supplied <see cref="HttpClient"/>.
/// </summary>
public sealed class HttpImageResourceLoader : IImageResourceLoader, IDisposable
{
    private readonly HttpClient _client;
    private readonly Func<Uri, bool> _isAllowed;
    private readonly int _maximumDownloadBytes;
    private readonly bool _ownsClient;

    public HttpImageResourceLoader(HttpClient? client = null, Func<Uri, bool>? isAllowed = null,
        int maximumDownloadBytes = 32 * 1024 * 1024)
    {
        _client = client ?? new HttpClient();
        _ownsClient = client is null;
        _isAllowed = isAllowed ?? new Func<Uri, bool>(static uri =>
            uri.Scheme == Uri.UriSchemeHttp || uri.Scheme == Uri.UriSchemeHttps);
        _maximumDownloadBytes = maximumDownloadBytes > 0 ? maximumDownloadBytes : throw new ArgumentOutOfRangeException(nameof(maximumDownloadBytes));
    }

    public async ValueTask<ImageResourceResult> LoadAsync(string source, CancellationToken cancellationToken)
    {
        if (!Uri.TryCreate(source, UriKind.Absolute, out var uri) || !_isAllowed(uri))
        {
            throw new UnauthorizedAccessException("The image URI is rejected by the host resource policy.");
        }

        using var response = await _client.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        var contentLength = response.Content.Headers.ContentLength;
        if (contentLength.HasValue && contentLength.Value > _maximumDownloadBytes)
        {
            throw new UnauthorizedAccessException("The image exceeds the host download-size policy.");
        }

        await using var network = await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false);
        await using var buffer = new MemoryStream();
        var chunk = new byte[81920];
        int read;
        while ((read = await network.ReadAsync(chunk, cancellationToken).ConfigureAwait(false)) > 0)
        {
            if (buffer.Length + read > _maximumDownloadBytes)
            {
                throw new UnauthorizedAccessException("The image exceeds the host download-size policy.");
            }
            await buffer.WriteAsync(chunk.AsMemory(0, read), cancellationToken).ConfigureAwait(false);
        }

        if (!NativeImageDecode.TryDecode(buffer.GetBuffer().AsSpan(0, checked((int)buffer.Length)), out var width, out var height, out var pixels))
        {
            throw new InvalidDataException("The downloaded resource is not a supported image.");
        }

        return new ImageResourceResult(new ImageDimensions(width, height), pixels);
    }

    public void Dispose()
    {
        if (_ownsClient) _client.Dispose();
    }
}
