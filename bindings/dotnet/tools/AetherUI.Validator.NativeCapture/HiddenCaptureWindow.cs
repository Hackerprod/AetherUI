using System.ComponentModel;
using System.Runtime.InteropServices;
namespace AetherUI.Validator.NativeCapture;
internal sealed class HiddenCaptureWindow : IDisposable
{
    public nint Handle { get; private set; }
    public static void ConfigureDpi()
    {
        if (!SetProcessDpiAwarenessContext(new nint(-4))) throw new Win32Exception(Marshal.GetLastWin32Error(), "Process DPI awareness could not be fixed");
    }
    public HiddenCaptureWindow(int width, int height)
    {
        Handle = CreateWindowExW(0,"STATIC","",0,0,0,width,height,0,0,0,0);
        if (Handle == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Hidden window creation failed");
        if (!GetClientRect(Handle,out var rect) || rect.Right-rect.Left != width || rect.Bottom-rect.Top != height || GetDpiForWindow(Handle) != 96)
        { Dispose(); throw new InvalidOperationException("Unexpected client dimensions or DPI; no system setting is changed"); }
    }
    public void Dispose() { if (Handle != 0) { var handle=Handle; Handle=0; if (!DestroyWindow(handle)) throw new Win32Exception(Marshal.GetLastWin32Error(),"Window cleanup failed"); } }
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left,Top,Right,Bottom; }
    [DllImport("user32.dll", SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] private static extern bool SetProcessDpiAwarenessContext(nint value);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern nint CreateWindowExW(uint ex,string cls,string title,uint style,int x,int y,int w,int h,nint parent,nint menu,nint instance,nint parameter);
    [DllImport("user32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] private static extern bool DestroyWindow(nint window);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint window);
    [DllImport("user32.dll")] [return:MarshalAs(UnmanagedType.Bool)] private static extern bool GetClientRect(nint window,out Rect rect);
}
