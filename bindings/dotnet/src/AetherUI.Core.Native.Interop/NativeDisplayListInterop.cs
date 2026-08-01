using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

public sealed class NativeDisplayListInterop : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate NativeTextMetrics MeasureDelegate(IntPtr text, IntPtr style, float maxWidth, IntPtr userData);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int ImageDimensionsDelegate(IntPtr source, out NativeImageSize size, IntPtr userData);
    [StructLayout(LayoutKind.Sequential)] private readonly record struct NativeImageSize(float Width, float Height);

    private readonly Func<string, NativeComputedStyle, float, NativeTextMetrics> _measure;
    private readonly MeasureDelegate _measureDelegate;
    private readonly Func<string, (float Width, float Height)?>? _imageDimensions;
    private readonly ImageDimensionsDelegate? _imageDimensionsDelegate;
    private readonly GCHandle _self;
    private readonly int _commandSize;
    private IntPtr _builder;

    public NativeDisplayListInterop(Func<string, NativeComputedStyle, float, NativeTextMetrics> measure,
        Func<string, (float Width, float Height)?>? imageDimensions = null)
    {
        _commandSize = NativeAbiContract.EnsureCompatible();
        _measure = measure ?? throw new ArgumentNullException(nameof(measure));
        _measureDelegate = Measure;
        _imageDimensions = imageDimensions;
        _imageDimensionsDelegate = imageDimensions is null ? null : ImageDimensions;
        _self = GCHandle.Alloc(this);
        Throw(NativeMethods.DisplayListBuilderCreate(Marshal.GetFunctionPointerForDelegate(_measureDelegate),
            GCHandle.ToIntPtr(_self), out _builder));
        if (_imageDimensionsDelegate is not null)
            NativeMethods.DisplayListBuilderSetImageDimensionsCallback(_builder,
                Marshal.GetFunctionPointerForDelegate(_imageDimensionsDelegate), GCHandle.ToIntPtr(_self));
    }

    public IReadOnlyList<NativePaintCommand> Build(NativeLayoutResult layout, NativeStyleMap currentStyles, IntPtr focusedElement)
    {
        ObjectDisposedException.ThrowIf(_builder == IntPtr.Zero, this);
        ArgumentNullException.ThrowIfNull(currentStyles);
        Throw(NativeMethods.DisplayListBuildWithStyles(_builder, layout.Handle, currentStyles.Handle, focusedElement, out var list));
        return ReadList(list);
    }

    public IReadOnlyList<NativePaintCommand> BuildNode(
        NativeLayoutResult layout,
        NativeStyleMap currentStyles,
        IntPtr element,
        IntPtr focusedElement)
    {
        ObjectDisposedException.ThrowIf(_builder == IntPtr.Zero, this);
        ArgumentNullException.ThrowIfNull(currentStyles);
        Throw(NativeMethods.DisplayListBuildNodeWithStyles(_builder, layout.Handle, currentStyles.Handle,
            element, focusedElement, out var list));
        return ReadList(list);
    }

    private IReadOnlyList<NativePaintCommand> ReadList(IntPtr list)
    {
        try
        {
            var commands = new List<NativePaintCommand>(NativeMethods.DisplayListGetCount(list));
            for (var index = 0; index < NativeMethods.DisplayListGetCount(list); index++)
            {
                commands.Add(ReadCommand(list, index, _commandSize));
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

    private static int ImageDimensions(IntPtr source, out NativeImageSize size, IntPtr userData)
    {
        size = default;
        try
        {
            var self = (NativeDisplayListInterop)GCHandle.FromIntPtr(userData).Target!;
            var dimensions = self._imageDimensions?.Invoke(Marshal.PtrToStringUTF8(source) ?? string.Empty);
            if (dimensions is not { } value || value.Width <= 0 || value.Height <= 0) return 0;
            size = new(value.Width, value.Height);
            return 1;
        }
        catch { return 0; }
    }

    private static NativePaintCommand ReadCommand(IntPtr list, int index, int commandSize)
    {
        var memory = Marshal.AllocHGlobal(commandSize);
        try
        {
            Marshal.Copy(new byte[commandSize], 0, memory, commandSize);
            Throw(NativeMethods.DisplayListGetCommand(list, index, memory));
            var bytes = new byte[commandSize];
            Marshal.Copy(memory, bytes, 0, commandSize);
            var tag = BitConverter.ToInt32(bytes, NativePaintCommandLayout.Tag);
            var text = tag is 4 or 5 or 9 ? Marshal.PtrToStringUTF8(Pointer(bytes, NativePaintCommandLayout.TextPointer)) : null;
            var imageSource = tag == 10
                ? Marshal.PtrToStringUTF8(Pointer(bytes, NativePaintCommandLayout.BackgroundImageSource))
                : null;
            var gradient = tag switch
            {
                2 => CopyGradient(bytes, NativePaintCommandLayout.GradientData),
                5 => CopyGradient(bytes, NativePaintCommandLayout.TextGradientData),
                _ => null,
            };
            var backdropFilters = tag == 11 ? CopyBackdropFilters(bytes) : null;
            return new NativePaintCommand(tag, bytes, text, gradient, imageSource, backdropFilters);
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
        if (count < 0 || count > 4096)
            throw new InvalidOperationException($"Native paint gradient stop count {count} exceeds the ABI safety limit.");
        var stops = new NativeGradientStop[Math.Max(0, count)];
        for (var index = 0; index < stops.Length; index++)
        {
            var raw = new byte[52];
            Marshal.Copy(stopsPointer + index * 52, raw, 0, raw.Length);
            stops[index] = new NativeGradientStop(
                new NativeColor(BitConverter.ToSingle(raw, 0), BitConverter.ToSingle(raw, 4),
                    BitConverter.ToSingle(raw, 8), BitConverter.ToSingle(raw, 12)),
                default, false, BitConverter.ToSingle(raw, 44));
        }
        return new NativeGradient(directionX, directionY, stops);
    }

    private static NativeBackdropFilter[] CopyBackdropFilters(byte[] bytes)
    {
        var items = Pointer(bytes, NativePaintCommandLayout.BackdropFilters);
        var count = Math.Max(0, BitConverter.ToInt32(bytes, NativePaintCommandLayout.BackdropFilterCount));
        if (items == IntPtr.Zero || count == 0) return [];
        const int itemSize = 36; // int kind + aether_css_length (28 bytes) + float scalar
        var result = new NativeBackdropFilter[count];
        for (var index = 0; index < count; index++)
        {
            var raw = new byte[itemSize];
            Marshal.Copy(items + index * itemSize, raw, 0, raw.Length);
            result[index] = new(
                BitConverter.ToInt32(raw, 0),
                new NativeLength(BitConverter.ToSingle(raw, 4), BitConverter.ToInt32(raw, 8),
                    BitConverter.ToSingle(raw, 12), BitConverter.ToSingle(raw, 16), BitConverter.ToSingle(raw, 20),
                    BitConverter.ToSingle(raw, 24), BitConverter.ToSingle(raw, 28)),
                BitConverter.ToSingle(raw, 32));
        }
        return result;
    }
}

/// <summary>Raw ABI command copy. RenderingInterop decodes it into the existing managed paint records.</summary>
public sealed record NativePaintCommand(int Tag, byte[] Bytes, string? Text, NativeGradient? Gradient, string? ImageSource,
    IReadOnlyList<NativeBackdropFilter>? BackdropFilters);
