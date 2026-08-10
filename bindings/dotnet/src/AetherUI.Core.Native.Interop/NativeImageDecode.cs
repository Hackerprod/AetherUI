using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

/// <summary>
/// Public facade over the native image decoder. Decodes arbitrary image bytes
/// (PNG, JPEG, GIF, BMP, ...) into the engine's canonical format: contiguous
/// BGRA8 pixels with premultiplied alpha, top-to-bottom rows, using
/// <c>Width * 4</c> bytes per row. The managed byte[] owns its copy; the native
/// buffer is released before this method returns.
/// </summary>
public static class NativeImageDecode
{
    public static bool TryDecode(ReadOnlySpan<byte> data, out int width, out int height, out byte[] pixels)
    {
        width = 0;
        height = 0;
        pixels = [];
        if (data.IsEmpty)
        {
            return false;
        }

        unsafe
        {
            fixed (byte* pinned = data)
            {
                byte* nativePixels = null;
                var status = NativeMethods.ImageDecode(pinned, (nuint)data.Length, out width, out height, out nativePixels);
                if (status != 0)
                {
                    return false;
                }

                try
                {
                    var byteCount = checked((long)width * height * 4);
                    if (byteCount <= 0 || byteCount > int.MaxValue)
                    {
                        return false;
                    }

                    pixels = new byte[byteCount];
                    Marshal.Copy((nint)nativePixels, pixels, 0, pixels.Length);
                    return true;
                }
                finally
                {
                    NativeMethods.ImagePixelsFree(nativePixels);
                }
            }
        }
    }
}
