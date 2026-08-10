using AetherUI.Core.Native;
using AetherUI.Runtime;

namespace AetherUI.Windows.Resources;

/// <summary>
/// Opt-in file image loader for Windows hosts. It performs no work until an application explicitly
/// supplies it to <see cref="ImageResourceResolver"/>. Callers may further restrict URIs through
/// <paramref name="isAllowed"/>; filesystem access policy remains host-owned, applied per file.
/// </summary>
public sealed class FileImageResourceLoader : IImageResourceLoader
{
    private readonly Func<Uri, bool> _isAllowed;

    public FileImageResourceLoader(Func<Uri, bool>? isAllowed = null)
    {
        _isAllowed = isAllowed ?? new Func<Uri, bool>(static uri => uri.Scheme == Uri.UriSchemeFile);
    }

    public async ValueTask<ImageResourceResult> LoadAsync(string source, CancellationToken cancellationToken)
    {
        if (!Uri.TryCreate(source, UriKind.Absolute, out var uri) || !_isAllowed(uri))
        {
            throw new UnauthorizedAccessException("The image URI is rejected by the host resource policy.");
        }

        var bytes = await File.ReadAllBytesAsync(uri.LocalPath, cancellationToken).ConfigureAwait(false);
        if (!NativeImageDecode.TryDecode(bytes, out var width, out var height, out var pixels))
        {
            throw new InvalidDataException("The image file is not a supported image.");
        }

        return new ImageResourceResult(new ImageDimensions(width, height), pixels);
    }
}
