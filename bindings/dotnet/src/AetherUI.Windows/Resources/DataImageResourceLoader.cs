using AetherUI.Core.Native;
using AetherUI.Runtime;

namespace AetherUI.Windows.Resources;

/// <summary>
/// Opt-in data URI image loader for Windows hosts. Accepts only the base64 data URI form
/// (<c>data:[&lt;mediatype&gt;][;base64],&lt;data&gt;</c>), which is the only form inline images use in
/// practice. Non-base64 data URIs are a form the host does not support and are rejected as a policy
/// rejection; malformed base64 and undecodable payloads are reported as decode failures.
/// </summary>
public sealed class DataImageResourceLoader : IImageResourceLoader
{
    private readonly int _maximumDecodedBytes;

    public DataImageResourceLoader(int maximumDecodedBytes = 32 * 1024 * 1024)
    {
        _maximumDecodedBytes = maximumDecodedBytes > 0
            ? maximumDecodedBytes
            : throw new ArgumentOutOfRangeException(nameof(maximumDecodedBytes));
    }

    public async ValueTask<ImageResourceResult> LoadAsync(string source, CancellationToken cancellationToken)
    {
        if (!Uri.TryCreate(source, UriKind.Absolute, out var uri) || uri.Scheme != "data")
        {
            throw new UnauthorizedAccessException("The image URI is rejected by the host resource policy.");
        }

        var commaIndex = uri.OriginalString.IndexOf(',');
        if (commaIndex < 0)
        {
            throw new UnauthorizedAccessException("The data URI is not in a supported form.");
        }

        var metadata = uri.OriginalString.AsSpan(0, commaIndex);
        if (!metadata.Contains(";base64", StringComparison.Ordinal))
        {
            // Non-base64 data URIs are a form this host does not support, so the honest mapping is
            // Policy rather than a decode failure of a payload we never attempted to read.
            throw new UnauthorizedAccessException("The data URI is not in a supported form.");
        }

        byte[] bytes;
        try
        {
            bytes = Convert.FromBase64String(uri.OriginalString[(commaIndex + 1)..]);
        }
        catch (FormatException exception)
        {
            throw new InvalidDataException("The data URI payload is not valid base64.", exception);
        }

        if (bytes.Length > _maximumDecodedBytes)
        {
            throw new UnauthorizedAccessException("The image exceeds the host decoded-size policy.");
        }

        if (!NativeImageDecode.TryDecode(bytes, out var width, out var height, out var pixels))
        {
            throw new InvalidDataException("The image data URI is not a supported image.");
        }

        return new ImageResourceResult(new ImageDimensions(width, height), pixels);
    }
}
