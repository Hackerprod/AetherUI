namespace AetherUI.Core.Native;

/// <summary>Owns a frame-local style-map view produced by <see cref="StyleRuntimeInterop"/>.</summary>
public sealed class NativeStyleMap : IDisposable
{
    private IntPtr _handle;

    internal NativeStyleMap(IntPtr handle) => _handle = handle;

    internal IntPtr Handle => _handle != IntPtr.Zero
        ? _handle
        : throw new ObjectDisposedException(nameof(NativeStyleMap));

    public void Dispose()
    {
        var handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero) NativeMethods.StyleMapDestroy(handle);
        GC.SuppressFinalize(this);
    }
}
