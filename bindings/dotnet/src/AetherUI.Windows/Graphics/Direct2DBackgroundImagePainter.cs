using AetherUI.Rendering;
using Vortice;
using Vortice.Direct2D1;
using Vortice.Mathematics;

namespace AetherUI.Windows.Graphics;

// Shared by the HWND backend and the WARP regression target. The core has already
// resolved CSS sizing, position, repetition and source cropping; this layer only maps
// that geometry onto a decoded bitmap.
internal static class Direct2DBackgroundImagePainter
{
    internal static void Draw(ID2D1DeviceContext target, ID2D1Bitmap1 bitmap,
                              DrawBackgroundImageCommand command)
    {
        var destination = command.DestinationRectangle;
        var source = command.SourceRectangle;
        if (!command.HasResolvedGeometry || command.Opacity <= 0 ||
            destination.IsEmpty || source.IsEmpty)
        {
            return;
        }

        target.PushAxisAlignedClip(ToRawRect(destination), AntialiasMode.PerPrimitive);
        try
        {
            // Both rectangles are explicit. Direct2D performs the source-to-destination
            // mapping itself, so no overload-specific placement convention or manual target
            // transform can leave part of a CSS background tile uncovered.
            target.DrawBitmap(bitmap, ToRawRect(destination), Math.Clamp(command.Opacity, 0f, 1f),
                InterpolationMode.Linear, ToRawRect(source), System.Numerics.Matrix4x4.Identity);
        }
        finally
        {
            target.PopAxisAlignedClip();
        }
    }

    private static RawRectF ToRawRect(AetherUI.Primitives.RectF rect) =>
        new(rect.Left, rect.Top, rect.Right, rect.Bottom);
}
