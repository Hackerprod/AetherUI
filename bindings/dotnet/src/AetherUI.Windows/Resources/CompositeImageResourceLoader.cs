using AetherUI.Runtime;

namespace AetherUI.Windows.Resources;

/// <summary>
/// Tries a sequence of <see cref="IImageResourceLoader"/> instances in order and returns the first
/// result. Dispatch contract: a loader signals it does not support a source by throwing
/// <see cref="UnauthorizedAccessException"/> (the policy signal), and the composite then tries the
/// next loader; the first loader that succeeds wins. A loader that accepts a scheme but then fails
/// to load or decode it throws a non-policy exception, which propagates as a real resource/decode
/// failure. If every loader rejects the source, the last policy rejection is rethrown.
/// </summary>
public sealed class CompositeImageResourceLoader : IImageResourceLoader
{
    private readonly IReadOnlyList<IImageResourceLoader> _loaders;

    public CompositeImageResourceLoader(params IImageResourceLoader[] loaders)
    {
        ArgumentNullException.ThrowIfNull(loaders);
        if (loaders.Length == 0)
        {
            throw new ArgumentException("At least one image resource loader is required.", nameof(loaders));
        }

        _loaders = loaders.ToArray();
    }

    public async ValueTask<ImageResourceResult> LoadAsync(string source, CancellationToken cancellationToken)
    {
        UnauthorizedAccessException? lastPolicyRejection = null;
        foreach (var loader in _loaders)
        {
            try
            {
                return await loader.LoadAsync(source, cancellationToken).ConfigureAwait(false);
            }
            catch (UnauthorizedAccessException exception)
            {
                lastPolicyRejection = exception;
            }
        }

        if (lastPolicyRejection is not null)
        {
            throw lastPolicyRejection;
        }

        throw new UnauthorizedAccessException("No image resource loader accepted the URI.");
    }
}
