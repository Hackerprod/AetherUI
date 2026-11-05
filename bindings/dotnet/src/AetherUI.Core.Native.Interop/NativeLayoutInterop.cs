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
    NativeScrollMetrics ScrollMetrics,
    NativeScrollbarGeometry ScrollbarGeometry,
    int ChildCount);

public enum NativeInputHitPart
{
    None = 0, Content = 1, HorizontalThumb = 2, VerticalThumb = 3,
    HorizontalTrack = 4, VerticalTrack = 5, ScrollbarCorner = 6,
}

public readonly record struct NativeInputHit(NativeInputHitPart Part, IntPtr Owner);

public sealed class NativeLayoutInterop : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate NativeTextMetrics MeasureDelegate(IntPtr text, IntPtr style, float maxWidth, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate NativeScrollOffset ScrollDelegate(IntPtr element, IntPtr userData);

    private readonly Func<string, NativeComputedStyle, float, NativeTextMetrics> _measure;
    private readonly MeasureDelegate _measureDelegate;
    private readonly ScrollDelegate _scrollDelegate;
    private readonly GCHandle _self;
    private Func<IntPtr, NativeScrollOffset>? _scroll;
    private IntPtr _engine;

    public NativeLayoutInterop(Func<string, NativeComputedStyle, float, NativeTextMetrics> measure)
    {
        NativeAbiContract.EnsureCompatible();
        _measure = measure ?? throw new ArgumentNullException(nameof(measure));
        _measureDelegate = Measure;
        _scrollDelegate = Scroll;
        _self = GCHandle.Alloc(this);
        Throw(NativeMethods.LayoutCreate(Marshal.GetFunctionPointerForDelegate(_measureDelegate),
            GCHandle.ToIntPtr(_self), out _engine));
    }

    public NativeLayoutResult Layout(NativeDomDocument document, NativeStyleMap styles,
        float width, float height, Func<IntPtr, NativeScrollOffset> scroll)
    {
        ObjectDisposedException.ThrowIf(_engine == IntPtr.Zero, this);
        _scroll = scroll ?? throw new ArgumentNullException(nameof(scroll));
        Throw(NativeMethods.LayoutLayout(_engine, document.Handle, styles.Handle, width, height,
            Marshal.GetFunctionPointerForDelegate(_scrollDelegate), GCHandle.ToIntPtr(_self), out var result));
        return new NativeLayoutResult(result);
    }

    /// <summary>Queries logical input coordinates using the native clips and input policy.
    /// Coordinates have no implicit viewport rejection or clamping and may find an outside box.
    /// An owner's ordinary content contribution is its BorderBox, excluding margins.</summary>
    public IntPtr HitTest(NativeLayoutResult result, NativeStyleMap? currentStyles,
        float x, float y, Func<IntPtr, NativeScrollOffset> scroll)
    {
        ArgumentNullException.ThrowIfNull(result);
        ObjectDisposedException.ThrowIf(_engine == IntPtr.Zero, this);
        var previous = _scroll;
        _scroll = scroll ?? throw new ArgumentNullException(nameof(scroll));
        try
        {
            var scrollCallback = Marshal.GetFunctionPointerForDelegate(_scrollDelegate);
            var scrollData = GCHandle.ToIntPtr(_self);
            return currentStyles is null
                ? NativeMethods.LayoutResultHitTestWithScrollOffsets(result.Handle, x, y, scrollCallback, scrollData)
                : NativeMethods.LayoutResultHitTestWithStylesAndScrollOffsets(
                    result.Handle, currentStyles.Handle, x, y, scrollCallback, scrollData);
        }
        finally
        {
            _scroll = previous;
        }
    }

    /// <summary>Queries logical input coordinates using the native clips and input policy.
    /// Coordinates have no implicit viewport rejection or clamping and may find an outside box.
    /// An owner's ordinary content contribution is its BorderBox, excluding margins.</summary>
    public NativeInputHit HitTestInput(NativeLayoutResult result, NativeStyleMap? currentStyles,
        float x, float y, Func<IntPtr, NativeScrollOffset> scroll)
    {
        ArgumentNullException.ThrowIfNull(result);
        ObjectDisposedException.ThrowIf(_engine == IntPtr.Zero, this);
        var previous = _scroll;
        _scroll = scroll ?? throw new ArgumentNullException(nameof(scroll));
        try
        {
            Throw(NativeMethods.LayoutResultHitTestInput(result.Handle, currentStyles?.Handle ?? IntPtr.Zero,
                x, y, Marshal.GetFunctionPointerForDelegate(_scrollDelegate), GCHandle.ToIntPtr(_self),
                out var part, out var owner));
            return new NativeInputHit(part, owner);
        }
        finally { _scroll = previous; }
    }

    public NativeScrollbarGeometry GetScrollbarGeometryAtOffset(IntPtr box, NativeScrollOffset offset) =>
        NativeMethods.LayoutBoxGetScrollbarGeometryAtOffset(box, offset);

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

    private static NativeScrollOffset Scroll(IntPtr element, IntPtr userData)
    {
        try { return ((NativeLayoutInterop)GCHandle.FromIntPtr(userData).Target!)._scroll?.Invoke(element) ?? default; }
        catch { return default; }
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
    public int ViewportOverflowX => NativeMethods.LayoutResultGetViewportOverflowX(Handle);
    public int ViewportOverflowY => NativeMethods.LayoutResultGetViewportOverflowY(Handle);
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
        NativeMethods.LayoutBoxGetScrollMetrics(box),
        NativeMethods.LayoutBoxGetScrollbarGeometry(box),
        NativeMethods.LayoutBoxGetChildCount(box));
}
