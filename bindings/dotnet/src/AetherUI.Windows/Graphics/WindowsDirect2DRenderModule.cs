using AetherUI.Rendering;

namespace AetherUI.Windows.Graphics;

/// <summary>A borrowed Win32 window handle. The window must outlive its renderer.</summary>
public readonly record struct WindowsRenderTarget(nint WindowHandle);

/// <summary>
/// Creates the existing Direct2D/DirectWrite backend without an adapter or alternate pipeline.
/// Preserves legacy sizing: creation clamps dimensions to one pixel, nonpositive resize is
/// ignored, and invalid DPI falls back to one. Owner-thread use is required; this module does
/// not add cross-thread guards to the existing Windows implementation.
/// </summary>
public sealed class WindowsDirect2DRenderModule : IRenderModule<WindowsRenderTarget>
{
    public string Id => "windows.direct2d";

    public IRenderBackendWithText Create(WindowsRenderTarget target, RenderViewport viewport)
    {
        // Direct2DRenderBackend validates the HWND and owns all partial-allocation cleanup.
        return new Direct2DRenderBackend(target.WindowHandle,
            viewport.PixelWidth, viewport.PixelHeight, viewport.DpiScale);
    }
}
