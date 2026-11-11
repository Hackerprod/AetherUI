using AetherUI.Runtime;
namespace AetherUI.Validator.NativeCapture;
internal sealed class TrackedLoader(IImageResourceLoader inner, Func<Task>? afterTerminal = null) : IImageResourceLoader
{
    private long _terminals;
    public long TerminalCount => Interlocked.Read(ref _terminals);
    public async ValueTask<ImageResourceResult> LoadAsync(string source, CancellationToken cancellationToken)
    {
        try
        {
            if (!source.StartsWith("data:",StringComparison.OrdinalIgnoreCase)) throw new UnauthorizedAccessException("Diagnostic fixture permits data resources only");
            return await inner.LoadAsync(source,cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            Interlocked.Increment(ref _terminals); // Telemetry only, never closes resolver Pending.
            if (afterTerminal is not null) await afterTerminal().ConfigureAwait(false);
        }
    }
}
