using System.Runtime.InteropServices;
using System.Text;

namespace AetherUI.Core.Native;

public readonly record struct NativeLength(float Value, int Unit, float Percent, float Em, float Rem, float Vw, float Vh);
public readonly record struct NativeEdges(NativeLength Left, NativeLength Top, NativeLength Right, NativeLength Bottom);
public readonly record struct NativeColor(float R, float G, float B, float A);
public readonly record struct NativeGradientStop(NativeColor Color, float Offset);
public sealed record NativeGradient(float DirectionX, float DirectionY, IReadOnlyList<NativeGradientStop> Stops);
public readonly record struct NativeShadow(float X, float Y, float Blur, float Spread, NativeColor Color, bool Inset);

public sealed record NativeComputedStyle(
    int Display, int FlexDirection, int JustifyContent, int AlignItems, float FlexGrow, bool FlexWrap, float Gap,
    int Position, NativeEdges Inset,
    NativeLength Width, NativeLength Height, NativeLength MinWidth, NativeLength MinHeight,
    NativeLength MaxWidth, NativeLength MaxHeight, NativeEdges Margin, NativeEdges Padding, NativeEdges BorderWidth,
    int BoxSizing, NativeColor Color, NativeColor BackgroundColor, NativeGradient? BackgroundGradient,
    NativeColor BorderColor, NativeGradient? TextGradient, NativeLength BorderRadius, float Opacity,
    NativeShadow? BoxShadow,
    string FontFamily, float FontSize, int FontWeight, float LineHeight, int TextAlign,
    int Overflow, int Cursor, bool Visible);

public sealed unsafe class StyleRuntimeInterop : IDisposable
{
    private IntPtr _runtime;
    private NativeDomDocument? _ownedDocument;
    private IntPtr _document;
    private long _defaults;
    private long _author;
    private bool _disposed;
    private readonly Dictionary<IntPtr, NativeComputedStyle> _styles = [];
    private List<IntPtr> _elements = [];

    public StyleRuntimeInterop(float width, float height, float dpiScale)
    {
        var options = new RuntimeOptions { MaxVariableDepth = 32, MaxVariableExpansionLength = 65536 };
        var environment = new RuntimeEnvironment(width, height, dpiScale, 16, 0, 0, 0);
        var optionsPtr = Marshal.AllocHGlobal(sizeof(RuntimeOptions));
        var environmentPtr = Marshal.AllocHGlobal(sizeof(RuntimeEnvironment));
        try
        {
            Marshal.StructureToPtr(options, optionsPtr, false);
            Marshal.StructureToPtr(environment, environmentPtr, false);
            Throw(NativeMethods.StyleRuntimeCreate(optionsPtr, environmentPtr, null, IntPtr.Zero, out _runtime));
        }
        finally
        {
            Marshal.FreeHGlobal(optionsPtr);
            Marshal.FreeHGlobal(environmentPtr);
        }
    }

    public void Load(string markup, string defaultsCss, string authorCss)
    {
        ThrowIfDisposed();
        _ownedDocument?.Dispose();
        _ownedDocument = NativeDomDocument.Parse(markup);
        Load(_ownedDocument, defaultsCss, authorCss);
    }

    /// <summary>Resolves styles against a caller-owned native DOM; it never reparses markup.</summary>
    public void Load(NativeDomDocument document, string defaultsCss, string authorCss)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(document);
        _document = document.Handle;
        NativeMethods.StyleRuntimeClearCache(_runtime);
        _styles.Clear();
        if (_defaults != 0 && NativeMethods.StyleRuntimeRemoveStyleSheet(_runtime, _defaults) == 0)
            throw new InvalidOperationException("Native defaults stylesheet could not be removed.");
        if (_author != 0 && NativeMethods.StyleRuntimeRemoveStyleSheet(_runtime, _author) == 0)
            throw new InvalidOperationException("Native author stylesheet could not be removed.");
        _defaults = AddStyleSheet(defaultsCss, 0, "aether-defaults.css");
        _author = AddStyleSheet(authorCss, 2, "<style>");
        _elements = document.EnumerateElementHandles().ToList();
    }

    public IReadOnlyList<NativeComputedStyle> Resolve(
        IReadOnlyList<int> states, float width, float height, float dpiScale)
    {
        ThrowIfDisposed();
        if (states.Count != _elements.Count)
            throw new InvalidOperationException("Managed and native DOM element counts differ.");
        for (var i = 0; i < states.Count; i++)
            NativeMethods.ElementSetState(_elements[i], states[i]);

        var environment = new RuntimeEnvironment(width, height, dpiScale, 16, 0, 0, 0);
        var envPtr = Marshal.AllocHGlobal(sizeof(RuntimeEnvironment));
        try
        {
            Marshal.StructureToPtr(environment, envPtr, false);
            ResolveUpdates(NativeMethods.StyleRuntimeSetEnvironment(
                _runtime, envPtr, _document, out var updates), updates);
        }
        finally { Marshal.FreeHGlobal(envPtr); }

        return _elements.Select(element => _styles[element]).ToArray();
    }

    /// <summary>Creates the frame-local style-map used by native layout without another CSS pass.</summary>
    public NativeStyleMap CreateResolvedStyleMap(NativeDomDocument document)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(document);
        Throw(NativeMethods.StyleRuntimeCreateResolvedStyleMap(_runtime, document.Handle, out var styleMap));
        return new NativeStyleMap(styleMap);
    }

    private long AddStyleSheet(string css, int layer, string name)
    {
        long handle = 0;
        Utf8(css, source => Utf8(name, sourceName =>
            Throw(NativeMethods.StyleRuntimeAddStyleSheet(
                _runtime, source, layer, sourceName, out handle, out _))));
        return handle;
    }

    private void ResolveUpdates(int count, IntPtr updates)
    {
        try
        {
            for (var i = 0; i < count; i++)
            {
                var update = Marshal.ReadIntPtr(updates, i * IntPtr.Size);
                Throw(NativeMethods.StyleRuntimeGetUpdate(update, out var node, out var style, out _));
                try { _styles[node] = ReadStyle(style); }
                finally { NativeMethods.ComputedStyleHandleDestroy(style); }
            }
        }
        finally { NativeMethods.StyleUpdatesFree(updates, count); }
    }

    internal static NativeComputedStyle ReadStyle(IntPtr handle)
    {
        var buffer = Marshal.AllocHGlobal(sizeof(ComputedStyleRaw));
        try
        {
            Throw(NativeMethods.ComputedStyleCopy(handle, buffer));
            var raw = Marshal.PtrToStructure<ComputedStyleRaw>(buffer);
            return raw.ToPublic();
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }

    internal static NativeComputedStyle ReadStylePointer(IntPtr pointer) =>
        Marshal.PtrToStructure<ComputedStyleRaw>(pointer).ToPublic();

    private unsafe delegate void Utf8Action(byte* pointer);

    private static void Utf8(string value, Utf8Action action)
    {
        var bytes = Encoding.UTF8.GetBytes(value + '\0');
        fixed (byte* pointer = bytes) action(pointer);
    }

    private static void Throw(int status)
    {
        if (status == 0) return;
        throw new InvalidOperationException($"Native AetherUI call failed with status {status}.");
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        if (_runtime != IntPtr.Zero) NativeMethods.StyleRuntimeDestroy(_runtime);
        _ownedDocument?.Dispose();
        _ownedDocument = null;
        _runtime = _document = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RuntimeOptions
    {
        public int ThrowOnInvalid, EnforceThreadAffinity, MaxVariableDepth, MaxVariableExpansionLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct RuntimeEnvironment(
        float Width, float Height, float DpiScale, float RootFontSize,
        int ColorScheme, int PointerAccuracy, int MotionPreference);

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct LengthRaw(float Value, int Unit, float Percent, float Em, float Rem, float Vw, float Vh)
    {
        public NativeLength ToPublic() => new(Value, Unit, Percent, Em, Rem, Vw, Vh);
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct EdgesRaw(LengthRaw Left, LengthRaw Top, LengthRaw Right, LengthRaw Bottom)
    {
        public NativeEdges ToPublic() => new(Left.ToPublic(), Top.ToPublic(), Right.ToPublic(), Bottom.ToPublic());
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct ColorRaw(float R, float G, float B, float A)
    {
        public NativeColor ToPublic() => new(R, G, B, A);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct GradientRaw { public float X, Y; public IntPtr Stops; public int Count; }
    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct GradientStopRaw(ColorRaw Color, float Offset);
    [StructLayout(LayoutKind.Sequential)]
    private struct ShadowRaw
    {
        public float X, Y, Blur, Spread; public ColorRaw Color; public int Inset;
        public readonly NativeShadow ToPublic() => new(X, Y, Blur, Spread, Color.ToPublic(), Inset != 0);
    }

    [StructLayout(LayoutKind.Sequential)]
    private unsafe struct ComputedStyleRaw
    {
        public int Display, FlexDirection, JustifyContent, AlignItems;
        public float FlexGrow; public int FlexWrap; public float Gap;
        public int Position; public EdgesRaw Inset;
        public LengthRaw Width, Height, MinWidth, MinHeight, MaxWidth, MaxHeight;
        public EdgesRaw Margin, Padding, BorderWidth; public int BoxSizing;
        public ColorRaw Color, BackgroundColor; public int HasBackgroundGradient; public GradientRaw BackgroundGradient;
        public ColorRaw BorderColor; public int HasTextGradient; public GradientRaw TextGradient;
        public int HasBoxShadow; public ShadowRaw BoxShadow;
        public LengthRaw BorderRadius; public float Opacity;
        public fixed byte FontFamily[64];
        public float FontSize; public int FontWeight; public float LineHeight; public int TextAlign;
        public int Overflow, Cursor, Visible;

        public NativeComputedStyle ToPublic()
        {
            string family;
            fixed (byte* p = FontFamily) family = Marshal.PtrToStringUTF8((IntPtr)p) ?? "Segoe UI";
            return new(Display, FlexDirection, JustifyContent, AlignItems, FlexGrow, FlexWrap != 0, Gap,
                Position, Inset.ToPublic(),
                Width.ToPublic(), Height.ToPublic(), MinWidth.ToPublic(), MinHeight.ToPublic(),
                MaxWidth.ToPublic(), MaxHeight.ToPublic(), Margin.ToPublic(), Padding.ToPublic(),
                BorderWidth.ToPublic(), BoxSizing, Color.ToPublic(), BackgroundColor.ToPublic(),
                ReadGradient(HasBackgroundGradient, BackgroundGradient), BorderColor.ToPublic(),
                ReadGradient(HasTextGradient, TextGradient), BorderRadius.ToPublic(), Opacity,
                HasBoxShadow != 0 ? BoxShadow.ToPublic() : null,
                family, FontSize, FontWeight, LineHeight, TextAlign, Overflow, Cursor, Visible != 0);
        }
    }

    private static NativeGradient? ReadGradient(int present, GradientRaw gradient)
    {
        if (present == 0 || gradient.Count <= 0 || gradient.Stops == IntPtr.Zero) return null;
        var size = Marshal.SizeOf<GradientStopRaw>();
        var stops = new NativeGradientStop[gradient.Count];
        for (var i = 0; i < stops.Length; i++)
        {
            var raw = Marshal.PtrToStructure<GradientStopRaw>(gradient.Stops + i * size);
            stops[i] = new NativeGradientStop(raw.Color.ToPublic(), raw.Offset);
        }
        return new NativeGradient(gradient.X, gradient.Y, stops);
    }
}
