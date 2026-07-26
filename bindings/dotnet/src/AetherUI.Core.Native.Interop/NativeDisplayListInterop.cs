using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

public sealed class NativeDisplayListInterop : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate NativeTextMetrics MeasureDelegate(IntPtr text, IntPtr style, float maxWidth, IntPtr userData);

    private readonly Func<string, NativeComputedStyle, float, NativeTextMetrics> _measure;
    private readonly MeasureDelegate _measureDelegate;
    private readonly GCHandle _self;
    private IntPtr _builder;

    public NativeDisplayListInterop(Func<string, NativeComputedStyle, float, NativeTextMetrics> measure)
    {
        _measure = measure ?? throw new ArgumentNullException(nameof(measure));
        _measureDelegate = Measure;
        _self = GCHandle.Alloc(this);
        Throw(NativeMethods.DisplayListBuilderCreate(Marshal.GetFunctionPointerForDelegate(_measureDelegate),
            GCHandle.ToIntPtr(_self), out _builder));
    }

    public IReadOnlyList<NativePaintCommand> Build(NativeLayoutResult layout, IntPtr focusedElement)
    {
        ObjectDisposedException.ThrowIf(_builder == IntPtr.Zero, this);
        Throw(NativeMethods.DisplayListBuild(_builder, layout.Handle, focusedElement, out var list));
        try
        {
            var commands = new List<NativePaintCommand>(NativeMethods.DisplayListGetCount(list));
            for (var index = 0; index < NativeMethods.DisplayListGetCount(list); index++)
            {
                commands.Add(ReadCommand(list, index));
            }
            return commands;
        }
        finally { NativeMethods.DisplayListDestroy(list); }
    }

    public void Dispose()
    {
        if (_builder != IntPtr.Zero) NativeMethods.DisplayListBuilderDestroy(_builder);
        _builder = IntPtr.Zero;
        if (_self.IsAllocated) _self.Free();
        GC.SuppressFinalize(this);
    }

    private static NativeTextMetrics Measure(IntPtr text, IntPtr style, float maxWidth, IntPtr userData)
    {
        try
        {
            var self = (NativeDisplayListInterop)GCHandle.FromIntPtr(userData).Target!;
            return self._measure(Marshal.PtrToStringUTF8(text) ?? string.Empty,
                StyleRuntimeInterop.ReadStylePointer(style), maxWidth);
        }
        catch { return default; }
    }

    private static NativePaintCommand ReadCommand(IntPtr list, int index)
    {
        const int commandSize = 160;
        var memory = Marshal.AllocHGlobal(commandSize);
        try
        {
            Marshal.Copy(new byte[commandSize], 0, memory, commandSize);
            Throw(NativeMethods.DisplayListGetCommand(list, index, memory));
            var bytes = new byte[commandSize];
            Marshal.Copy(memory, bytes, 0, commandSize);
            var tag = BitConverter.ToInt32(bytes, 0);
            var text = tag is 4 or 5 ? Marshal.PtrToStringUTF8(Pointer(bytes, 8)) : null;
            var gradient = tag switch
            {
                2 => CopyGradient(bytes, 32),
                5 => CopyGradient(bytes, 112),
                _ => null,
            };
            return new NativePaintCommand(tag, bytes, text, gradient);
        }
        finally
        {
            NativeMethods.PaintCommandFree(memory);
            Marshal.FreeHGlobal(memory);
        }
    }

    private static void Throw(int status)
    {
        if (status != 0) throw new InvalidOperationException($"Native AetherUI call failed with status {status}.");
    }

    private static IntPtr Pointer(byte[] bytes, int offset) => IntPtr.Size == 8
        ? new IntPtr(BitConverter.ToInt64(bytes, offset))
        : new IntPtr(BitConverter.ToInt32(bytes, offset));

    private static NativeGradient CopyGradient(byte[] bytes, int offset)
    {
        var directionX = BitConverter.ToSingle(bytes, offset);
        var directionY = BitConverter.ToSingle(bytes, offset + 4);
        var stopsPointer = Pointer(bytes, offset + 8);
        var count = BitConverter.ToInt32(bytes, offset + 16);
        var stops = new NativeGradientStop[Math.Max(0, count)];
        for (var index = 0; index < stops.Length; index++)
        {
            var raw = new byte[20];
            Marshal.Copy(stopsPointer + index * 20, raw, 0, raw.Length);
            stops[index] = new NativeGradientStop(
                new NativeColor(BitConverter.ToSingle(raw, 0), BitConverter.ToSingle(raw, 4),
                    BitConverter.ToSingle(raw, 8), BitConverter.ToSingle(raw, 12)),
                BitConverter.ToSingle(raw, 16));
        }
        return new NativeGradient(directionX, directionY, stops);
    }
}

/// <summary>Raw ABI command copy. RenderingInterop decodes it into the existing managed paint records.</summary>
public sealed record NativePaintCommand(int Tag, byte[] Bytes, string? Text, NativeGradient? Gradient);
