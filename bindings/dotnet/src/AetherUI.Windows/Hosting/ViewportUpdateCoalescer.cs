namespace AetherUI.Windows.Hosting;

/// <summary>
/// Retains only the newest native viewport notification until the host reaches its next paint.
/// Win32 emits a burst of WM_SIZE messages while a sizing drag is in progress; recreating the
/// swap-chain for every intermediate size needlessly blocks the UI thread.
/// </summary>
internal sealed class ViewportUpdateCoalescer
{
    private ViewportUpdate? _pending;

    public void Queue(int pixelWidth, int pixelHeight, float dpiScale)
    {
        if (pixelWidth <= 0 || pixelHeight <= 0 || !float.IsFinite(dpiScale) || dpiScale <= 0)
        {
            return;
        }

        _pending = new ViewportUpdate(pixelWidth, pixelHeight, dpiScale);
    }

    public bool TryTake(out ViewportUpdate update)
    {
        if (_pending is not { } pending)
        {
            update = default;
            return false;
        }

        _pending = null;
        update = pending;
        return true;
    }
}

internal readonly record struct ViewportUpdate(int PixelWidth, int PixelHeight, float DpiScale);
