using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

internal static class NativeStrings
{
    internal static unsafe byte* AllocateUtf8(string? s)
    {
        if (s is null) return null;
        var bytes = System.Text.Encoding.UTF8.GetByteCount(s) + 1;
        var ptr = Marshal.AllocHGlobal(bytes);
        var span = new Span<byte>((void*)ptr, bytes);
        System.Text.Encoding.UTF8.GetBytes(s, span);
        span[^1] = 0;
        return (byte*)ptr;
    }

    internal static unsafe string? MarshalToStringAndFree(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero) return null;
        var result = Marshal.PtrToStringUTF8(ptr);
        NativeMethods.StringFree(ptr);
        return result;
    }
}
