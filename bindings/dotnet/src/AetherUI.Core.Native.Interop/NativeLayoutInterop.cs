using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

public readonly record struct NativeLayoutBox(
    IntPtr Handle,
    int Kind,
    IntPtr Element,
    string? Text,
    NativeRect MarginBox,
    NativeRect BorderBox,
    NativeRect PaddingBox,
    NativeRect ContentBox,
    bool IsAtomicControl,
    float ScrollableOverflowY,
    float ScrollOffsetY,
    int ChildCount);

public sealed class NativeLayoutInterop : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate NativeTextMetrics MeasureDelegate(IntPtr text, IntPtr style, float maxWidth, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate float ScrollDelegate(IntPtr element, IntPtr userData);

    private readonly Func<string, NativeComputedStyle, float, NativeTextMetrics> _measure;
    private readonly MeasureDelegate _measureDelegate;
    private readonly ScrollDelegate _scrollDelegate;
    private readonly GCHandle _self;
    private Func<IntPtr, float>? _scroll;
    private IntPtr _engine;

    public NativeLayoutInterop(Func<string, NativeComputedStyle, float, NativeTextMetrics> measure)
    {
        _measure = measure ?? throw new ArgumentNullException(nameof(measure));
        _measureDelegate = Measure;
        _scrollDelegate = Scroll;
        _self = GCHandle.Alloc(this);
        Throw(NativeMethods.LayoutCreate(Marshal.GetFunctionPointerForDelegate(_measureDelegate),
            GCHandle.ToIntPtr(_self), out _engine));
    }

    public NativeLayoutResult Layout(NativeDomDocument document, NativeStyleMap styles,
        float width, float height, Func<IntPtr, float> scroll)
    {
        ObjectDisposedException.ThrowIf(_engine == IntPtr.Zero, this);
        _scroll = scroll ?? throw new ArgumentNullException(nameof(scroll));
        Throw(NativeMethods.LayoutLayout(_engine, document.Handle, styles.Handle, width, height,
            Marshal.GetFunctionPointerForDelegate(_scrollDelegate), GCHandle.ToIntPtr(_self), out var result));
        return new NativeLayoutResult(result);
    }

    public void Dispose()
    {
        if (_engine != IntPtr.Zero) NativeMethods.LayoutDestroy(_engine);
        _engine = IntPtr.Zero;
        if (_self.IsAllocated) _self.Free();
        GC.SuppressFinalize(this);
    }

    private static NativeTextMetrics Measure(IntPtr text, IntPtr style, float maxWidth, IntPtr userData)
    {
        try
        {
            var self = (NativeLayoutInterop)GCHandle.FromIntPtr(userData).Target!;
            return self._measure(Marshal.PtrToStringUTF8(text) ?? string.Empty,
                StyleRuntimeInterop.ReadStylePointer(style), maxWidth);
        }
        catch { return default; }
    }

    private static float Scroll(IntPtr element, IntPtr userData)
    {
        try { return ((NativeLayoutInterop)GCHandle.FromIntPtr(userData).Target!)._scroll?.Invoke(element) ?? 0; }
        catch { return 0; }
    }

    private static void Throw(int status)
    {
        if (status != 0) throw new InvalidOperationException($"Native AetherUI call failed with status {status}.");
    }
}

public sealed class NativeLayoutResult : IDisposable
{
    private IntPtr _handle;

    internal NativeLayoutResult(IntPtr handle) => _handle = handle;

    public NativeLayoutBox Root => Read(NativeMethods.LayoutResultGetRoot(Handle));
    public int PaintOrderCount => NativeMethods.LayoutResultGetPaintOrderCount(Handle);
    public NativeLayoutBox GetPaintOrderBox(int index) => Read(NativeMethods.LayoutResultGetPaintOrderBox(Handle, index));
    public NativeLayoutBox GetChild(NativeLayoutBox box, int index) => Read(NativeMethods.LayoutBoxGetChild(box.Handle, index));

    public void Dispose()
    {
        var handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero) NativeMethods.LayoutResultDestroy(handle);
        GC.SuppressFinalize(this);
    }

    internal IntPtr Handle => _handle != IntPtr.Zero ? _handle : throw new ObjectDisposedException(nameof(NativeLayoutResult));

    private static NativeLayoutBox Read(IntPtr box) => new(
        box,
        NativeMethods.LayoutBoxGetKind(box),
        NativeMethods.LayoutBoxGetElement(box),
        NativeStrings.MarshalToStringAndFree(NativeMethods.LayoutBoxGetText(box)),
        NativeMethods.LayoutBoxGetMarginBox(box),
        NativeMethods.LayoutBoxGetBorderBox(box),
        NativeMethods.LayoutBoxGetPaddingBox(box),
        NativeMethods.LayoutBoxGetContentBox(box),
        NativeMethods.LayoutBoxIsAtomic(box) != 0,
        NativeMethods.LayoutBoxGetScrollableOverflowY(box),
        NativeMethods.LayoutBoxGetScrollOffsetY(box),
        NativeMethods.LayoutBoxGetChildCount(box));
}
