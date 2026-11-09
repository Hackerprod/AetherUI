using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace AetherUI.Linux.Graphics;

internal static partial class CairoNative
{
    internal const string Library = "libcairo.so.2";
    [StructLayout(LayoutKind.Sequential)]
    internal struct Matrix
    {
        internal double Xx, Yx, Xy, Yy, X0, Y0;
    }
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr cairo_image_surface_create(int format, int width, int height);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int cairo_surface_status(IntPtr surface);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_surface_destroy(IntPtr surface);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_surface_flush(IntPtr surface);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_surface_mark_dirty(IntPtr surface);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr cairo_image_surface_get_data(IntPtr surface);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int cairo_image_surface_get_stride(IntPtr surface);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr cairo_create(IntPtr surface);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_destroy(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int cairo_status(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr cairo_status_to_string(int status);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_save(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_restore(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_scale(IntPtr cr, double x, double y);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_translate(IntPtr cr, double x, double y);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_transform(IntPtr cr, ref Matrix matrix);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_set_matrix(IntPtr cr, ref Matrix matrix);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_get_matrix(IntPtr cr, out Matrix matrix);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_identity_matrix(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_set_source_rgba(IntPtr cr, double r, double g, double b, double a);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_set_operator(IntPtr cr, int op);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_paint(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_paint_with_alpha(IntPtr cr, double alpha);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_new_path(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_move_to(IntPtr cr, double x, double y);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_line_to(IntPtr cr, double x, double y);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_curve_to(IntPtr cr, double x1, double y1, double x2, double y2, double x3, double y3);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_close_path(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_rectangle(IntPtr cr, double x, double y, double width, double height);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_clip(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_fill(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_set_fill_rule(IntPtr cr, int rule);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_stroke(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_set_line_width(IntPtr cr, double width);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_set_source_surface(IntPtr cr, IntPtr surface, double x, double y);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr cairo_get_target(IntPtr cr);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr cairo_pattern_create_linear(double x0, double y0, double x1, double y1);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_pattern_add_color_stop_rgba(IntPtr pattern, double offset, double r, double g, double b, double a);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_pattern_set_extend(IntPtr pattern, int extend);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_pattern_destroy(IntPtr pattern);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern int cairo_pattern_status(IntPtr pattern);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_set_source(IntPtr cr, IntPtr pattern);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr cairo_pattern_create_for_surface(IntPtr surface);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_pattern_set_matrix(IntPtr pattern, ref Matrix matrix);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern void cairo_mask(IntPtr cr, IntPtr pattern);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr cairo_get_source(IntPtr cr);
    internal static void Check(int status)
    {
        if (status != 0)
            throw new InvalidOperationException("Cairo: " + Marshal.PtrToStringUTF8(cairo_status_to_string(status)));
    }
}
internal sealed class CairoContext : SafeHandleZeroOrMinusOneIsInvalid
{
    internal CairoContext(IntPtr pointer) : base(true) { SetHandle(pointer); try { CairoNative.Check(CairoNative.cairo_status(pointer)); } catch { Dispose(); throw; } }
    internal IntPtr Pointer => handle;
    protected override bool ReleaseHandle()
    {
        CairoNative.cairo_destroy(handle);
        return true;
    }
}
internal sealed class CairoSurface : SafeHandleZeroOrMinusOneIsInvalid
{
    internal int Width
    {
        get;
    }
    internal int Height
    {
        get;
    }
    internal IntPtr Handle => handle;
    internal CairoSurface(int width, int height) : base(true)
    {
        Width = width;
        Height = height;
        SetHandle(CairoNative.cairo_image_surface_create(0, width, height));
        try
        {
            CairoNative.Check(CairoNative.cairo_surface_status(handle));
        }
        catch { Dispose(); throw; }
    }
    internal CairoContext CreateContext() => new(CairoNative.cairo_create(handle));
    internal byte[] ReadPixels()
    {
        CairoNative.cairo_surface_flush(handle);
        var bytes = new byte[checked(Width * Height * 4)];
        var stride = CairoNative.cairo_image_surface_get_stride(handle);
        var ptr = CairoNative.cairo_image_surface_get_data(handle);
        for (var y = 0; y < Height; y++)
            Marshal.Copy(ptr + y * stride, bytes, y * Width * 4, Width * 4);
        return bytes;
    }
    internal void WritePixels(byte[] bytes)
    {
        if (bytes.Length != checked(Width * Height * 4))
            throw new ArgumentException("Invalid pixel length.");
        CairoNative.cairo_surface_flush(handle);
        var stride = CairoNative.cairo_image_surface_get_stride(handle);
        var ptr = CairoNative.cairo_image_surface_get_data(handle);
        for (var y = 0; y < Height; y++)
            Marshal.Copy(bytes, y * Width * 4, ptr + y * stride, Width * 4);
        CairoNative.cairo_surface_mark_dirty(handle);
    }
    protected override bool ReleaseHandle()
    {
        CairoNative.cairo_surface_destroy(handle);
        return true;
    }
}
