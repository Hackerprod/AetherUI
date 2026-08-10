using System.Runtime.InteropServices;
using System.Text;

namespace AetherUI.Core.Native;

public readonly record struct NativeLength(float Value, int Unit, float Percent, float Em, float Rem, float Vw, float Vh);
public readonly record struct NativeEdges(NativeLength Left, NativeLength Top, NativeLength Right, NativeLength Bottom);
public readonly record struct NativeColor(float R, float G, float B, float A);
public readonly record struct NativeColorEdges(NativeColor Left, NativeColor Top, NativeColor Right, NativeColor Bottom);
public readonly record struct NativeBorderStyleEdges(int Left, int Top, int Right, int Bottom);
public readonly record struct NativeCornerRadius(NativeLength Horizontal, NativeLength Vertical);
public readonly record struct NativeBorderRadii(NativeCornerRadius TopLeft, NativeCornerRadius TopRight, NativeCornerRadius BottomRight, NativeCornerRadius BottomLeft);
public readonly record struct NativeGradientStop(NativeColor Color, NativeLength Position, bool HasPosition, float Offset);
public sealed record NativeGradient(float DirectionX, float DirectionY, IReadOnlyList<NativeGradientStop> Stops);
public readonly record struct NativeShadow(float X, float Y, float Blur, float Spread, NativeColor Color, bool Inset);
public sealed record NativeBackgroundLayer(
    int Kind, NativeGradient? Gradient, string? ImageSource,
    NativeLength PositionX, NativeLength PositionY, int SizeKind,
    NativeLength SizeX, NativeLength SizeY, int RepeatX, int RepeatY);
public readonly record struct NativeBackdropFilter(int Kind, NativeLength LengthAmount, float ScalarAmount);
public readonly record struct NativeFlexBasis(int Kind, NativeLength Length);
public readonly record struct NativeTransformOperation(
    int Kind, NativeLength X, NativeLength Y, float ScalarX, float ScalarY, float AngleRadians,
    float MatrixA, float MatrixB, float MatrixC, float MatrixD, float MatrixE, float MatrixF);
public readonly record struct NativeTransformOrigin(NativeLength X, NativeLength Y);

[Flags]
public enum NativeStyleImpact : uint
{
    None = 0,
    Layout = 1 << 0,
    Paint = 1 << 1,
    HitTest = 1 << 2,
    Composite = 1 << 3,
    TextLayout = 1 << 4,
    Cursor = 1 << 5,
    Inherited = 1 << 6,
}

public sealed record NativeComputedStyle(
    int Display, int FlexDirection, int JustifyContent, int AlignItems, int AlignSelf, float FlexGrow, float FlexShrink, NativeFlexBasis FlexBasis, bool FlexWrap, float Gap,
    int Position, NativeEdges Inset,
    NativeLength Width, NativeLength Height, NativeLength MinWidth, NativeLength MinHeight,
    NativeLength MaxWidth, NativeLength MaxHeight, NativeEdges Margin, NativeEdges Padding, NativeEdges BorderWidth,
    int BoxSizing, NativeColor Color, NativeColor BackgroundColor, IReadOnlyList<NativeBackgroundLayer> BackgroundLayers,
    IReadOnlyList<NativeBackdropFilter> BackdropFilters,
    NativeColor BorderColor, NativeColorEdges BorderColors, NativeBorderStyleEdges BorderStyles, NativeGradient? TextGradient,
    IReadOnlyList<NativeTransformOperation> Transforms, NativeTransformOrigin TransformOrigin,
    IReadOnlyList<NativeShadow> TextShadows, NativeBorderRadii BorderRadii, float Opacity,
    IReadOnlyList<NativeShadow> BoxShadows,
    string FontFamily, float FontSize, int FontWeight, float LineHeight, int TextAlign, int TextTransform, int WhiteSpace,
    int OverflowX, int OverflowY, int Cursor, int PointerEvents, bool Visible,
    int TransitionProperties, float TransitionDuration, float TransitionDelay, int TransitionTiming);

public sealed record NativeStyleResolution(
    IReadOnlyList<NativeComputedStyle> Styles,
    NativeStyleImpact Impact);

public sealed record NativeStyleUpdate(
    IntPtr Node,
    NativeComputedStyle Style,
    NativeStyleImpact Impact);

public sealed record NativeStyleChangeResolution(
    IReadOnlyList<NativeStyleUpdate> Updates,
    NativeStyleImpact Impact);

public sealed unsafe class StyleRuntimeInterop : IDisposable
{
    internal static int ComputedStyleSize => Marshal.SizeOf<ComputedStyleRaw>();

    private IntPtr _runtime;
    private NativeDomDocument? _ownedDocument;
    private IntPtr _document;
    private long _defaults;
    private long _author;
    private bool _disposed;
    private readonly Dictionary<IntPtr, NativeComputedStyle> _styles = [];
    private readonly Dictionary<IntPtr, int> _elementIndex = [];
    private List<IntPtr> _elements = [];

    public StyleRuntimeInterop(float width, float height, float dpiScale)
    {
        NativeAbiContract.EnsureCompatible();
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

    /// <summary>Resolves styles against a caller-owned native DOM; it never reparses markup.
    /// <paramref name="defaultsCss"/> is ignored: the engine-defaults (UA) text now comes from the
    /// native runtime via <see cref="NativeMethods.StyleRuntimeDefaultsCss"/>.</summary>
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
        _defaults = AddStyleSheet(ReadDefaultsCss(), 0, "aether-defaults.css");
        _author = AddStyleSheet(authorCss, 2, "<style>");
        _elements = document.EnumerateElementHandles().ToList();
        _elementIndex.Clear();
        for (var index = 0; index < _elements.Count; index++)
        {
            _elementIndex[_elements[index]] = index;
        }
    }

    public NativeStyleResolution Resolve(
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
            var resolution = ResolveUpdates(NativeMethods.StyleRuntimeSetEnvironment(
                _runtime, envPtr, _document, out var updates), updates);
            return new NativeStyleResolution(_elements.Select(element => _styles[element]).ToArray(), resolution.Impact);
        }
        finally { Marshal.FreeHGlobal(envPtr); }
    }

    public NativeStyleChangeResolution ResolveStateChanges(
        IReadOnlyList<IntPtr> changedElements,
        IReadOnlyList<int> changedStates)
    {
        ThrowIfDisposed();
        if (changedElements.Count != changedStates.Count)
            throw new ArgumentException("Changed element and state counts differ.", nameof(changedStates));

        if (changedElements.Count == 0)
            return new NativeStyleChangeResolution(Array.Empty<NativeStyleUpdate>(), NativeStyleImpact.None);

        var changedByElement = new Dictionary<IntPtr, int>(changedElements.Count);
        for (var i = 0; i < changedElements.Count; i++)
            changedByElement[changedElements[i]] = changedStates[i];

        var changed = changedByElement.Keys.ToArray();
        foreach (var (element, state) in changedByElement)
        {
            if (_elementIndex.ContainsKey(element))
                NativeMethods.ElementSetState(element, state);
        }

        fixed (IntPtr* changedPointer = changed)
        {
            return ResolveUpdates(NativeMethods.StyleRuntimeResolveStateChanges(
                _runtime, _document, (IntPtr)changedPointer, changed.Length, out var updates), updates);
        }
    }

    /// <summary>Creates the frame-local style-map used by native layout without another CSS pass.</summary>
    public NativeStyleMap CreateResolvedStyleMap(NativeDomDocument document)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(document);
        Throw(NativeMethods.StyleRuntimeCreateResolvedStyleMap(_runtime, document.Handle, out var styleMap));
        return new NativeStyleMap(styleMap);
    }

    public void UpdateResolvedStyleMap(NativeStyleMap map, IReadOnlyList<IntPtr> nodes)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(map);
        if (nodes.Count == 0)
        {
            return;
        }

        var distinct = nodes.Distinct().ToArray();
        fixed (IntPtr* nodePointer = distinct)
        {
            Throw(NativeMethods.StyleRuntimeUpdateResolvedStyleMap(
                _runtime, map.Handle, (IntPtr)nodePointer, distinct.Length));
        }
    }

    private long AddStyleSheet(string css, int layer, string name)
    {
        long handle = 0;
        Utf8(css, source => Utf8(name, sourceName =>
            Throw(NativeMethods.StyleRuntimeAddStyleSheet(
                _runtime, source, layer, sourceName, out handle, out _))));
        return handle;
    }

    private static string ReadDefaultsCss()
    {
        var pointer = NativeMethods.StyleRuntimeDefaultsCss();
        if (pointer == IntPtr.Zero)
            throw new InvalidOperationException("Native UA defaults stylesheet is unavailable.");
        return Marshal.PtrToStringUTF8(pointer) ?? string.Empty;
    }

    private NativeStyleChangeResolution ResolveUpdates(int count, IntPtr updates)
    {
        var aggregate = NativeStyleImpact.None;
        var records = count > 0 ? new List<NativeStyleUpdate>(count) : new List<NativeStyleUpdate>();
        try
        {
            for (var i = 0; i < count; i++)
            {
                var update = Marshal.ReadIntPtr(updates, i * IntPtr.Size);
                Throw(NativeMethods.StyleRuntimeGetUpdate(update, out var node, out var style, out var impact));
                var nativeImpact = (NativeStyleImpact)impact;
                aggregate |= nativeImpact;
                try
                {
                    var publicStyle = ReadStyle(style);
                    _styles[node] = publicStyle;
                    records.Add(new NativeStyleUpdate(node, publicStyle, nativeImpact));
                }
                finally { NativeMethods.ComputedStyleHandleDestroy(style); }
            }
        }
        finally { NativeMethods.StyleUpdatesFree(updates, count); }
        return new NativeStyleChangeResolution(records, aggregate);
    }

    internal static NativeComputedStyle ReadStyle(IntPtr handle)
    {
        var buffer = Marshal.AllocHGlobal(sizeof(ComputedStyleRaw));
        var copied = false;
        try
        {
            Throw(NativeMethods.ComputedStyleCopy(handle, buffer));
            copied = true;
            var raw = Marshal.PtrToStructure<ComputedStyleRaw>(buffer);
            return raw.ToPublic();
        }
        finally
        {
            if (copied) NativeMethods.ComputedStyleFree(buffer);
            Marshal.FreeHGlobal(buffer);
        }
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
internal struct RuntimeOptions
    {
        public int ThrowOnInvalid, EnforceThreadAffinity, MaxVariableDepth, MaxVariableExpansionLength;
    }

    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct RuntimeEnvironment(
        float Width, float Height, float DpiScale, float RootFontSize,
        int ColorScheme, int PointerAccuracy, int MotionPreference);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly record struct LengthRaw(float Value, int Unit, float Percent, float Em, float Rem, float Vw, float Vh)
    {
        public NativeLength ToPublic() => new(Value, Unit, Percent, Em, Rem, Vw, Vh);
    }

    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct EdgesRaw(LengthRaw Left, LengthRaw Top, LengthRaw Right, LengthRaw Bottom)
    {
        public NativeEdges ToPublic() => new(Left.ToPublic(), Top.ToPublic(), Right.ToPublic(), Bottom.ToPublic());
    }

    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct ColorRaw(float R, float G, float B, float A)
    {
        public NativeColor ToPublic() => new(R, G, B, A);
    }

    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct ColorEdgesRaw(ColorRaw Left, ColorRaw Top, ColorRaw Right, ColorRaw Bottom)
    {
        public NativeColorEdges ToPublic() => new(Left.ToPublic(), Top.ToPublic(), Right.ToPublic(), Bottom.ToPublic());
    }

    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct BorderStyleEdgesRaw(int Left, int Top, int Right, int Bottom)
    {
        public NativeBorderStyleEdges ToPublic() => new(Left, Top, Right, Bottom);
    }
    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct CornerRadiusRaw(LengthRaw Horizontal, LengthRaw Vertical)
    {
        public NativeCornerRadius ToPublic() => new(Horizontal.ToPublic(), Vertical.ToPublic());
    }
    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct BorderRadiiRaw(CornerRadiusRaw TopLeft, CornerRadiusRaw TopRight, CornerRadiusRaw BottomRight, CornerRadiusRaw BottomLeft)
    {
        public NativeBorderRadii ToPublic() => new(TopLeft.ToPublic(), TopRight.ToPublic(), BottomRight.ToPublic(), BottomLeft.ToPublic());
    }

    [StructLayout(LayoutKind.Sequential)]
internal struct GradientRaw { public float X, Y; public IntPtr Stops; public int Count; }
    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct GradientStopRaw(ColorRaw Color, LengthRaw Position, float Offset, int HasPosition);
    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct BackgroundLayerListRaw(IntPtr Items, int Count);
    [StructLayout(LayoutKind.Sequential)]
internal struct BackgroundLayerRaw
    {
        public int Kind;
        public GradientRaw Gradient;
        public IntPtr ImageSource;
        public LengthRaw PositionX, PositionY;
        public int SizeKind;
        public LengthRaw SizeX, SizeY;
        public int RepeatX, RepeatY;

        public readonly NativeBackgroundLayer ToPublic() => new(
            Kind,
            Kind == 0 ? ReadGradient(1, Gradient) : null,
            ImageSource == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(ImageSource),
            PositionX.ToPublic(), PositionY.ToPublic(), SizeKind,
            SizeX.ToPublic(), SizeY.ToPublic(), RepeatX, RepeatY);
    }
    [StructLayout(LayoutKind.Sequential)]
internal struct ShadowRaw
    {
        public float X, Y, Blur, Spread; public ColorRaw Color; public int Inset;
        public readonly NativeShadow ToPublic() => new(X, Y, Blur, Spread, Color.ToPublic(), Inset != 0);
    }
    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct ShadowListRaw(IntPtr Items, int Count);
    [StructLayout(LayoutKind.Sequential)]
internal struct BackdropFilterRaw
    {
        public int Kind; public LengthRaw LengthAmount; public float ScalarAmount;
        public readonly NativeBackdropFilter ToPublic() => new(Kind, LengthAmount.ToPublic(), ScalarAmount);
    }
    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct BackdropFilterListRaw(IntPtr Items, int Count);
    [StructLayout(LayoutKind.Sequential)]
internal readonly record struct FlexBasisRaw(int Kind, LengthRaw Length)
    {
        public NativeFlexBasis ToPublic() => new(Kind, Length.ToPublic());
    }
    [StructLayout(LayoutKind.Sequential)]
    internal readonly record struct TransformOperationRaw(
        int Kind, LengthRaw X, LengthRaw Y, float ScalarX, float ScalarY, float AngleRadians,
        float MatrixA, float MatrixB, float MatrixC, float MatrixD, float MatrixE, float MatrixF)
    {
        public NativeTransformOperation ToPublic() => new(
            Kind, X.ToPublic(), Y.ToPublic(), ScalarX, ScalarY, AngleRadians,
            MatrixA, MatrixB, MatrixC, MatrixD, MatrixE, MatrixF);
    }
    [StructLayout(LayoutKind.Sequential)]
    internal readonly record struct TransformListRaw(IntPtr Items, int Count);
    [StructLayout(LayoutKind.Sequential)]
    internal readonly record struct TransformOriginRaw(LengthRaw X, LengthRaw Y)
    {
        public NativeTransformOrigin ToPublic() => new(X.ToPublic(), Y.ToPublic());
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct ComputedStyleRaw
    {
        public int Display, FlexDirection, JustifyContent, AlignItems, AlignSelf;
        public float FlexGrow, FlexShrink; public FlexBasisRaw FlexBasis; public int FlexWrap; public float Gap;
        public int Position; public EdgesRaw Inset;
        public LengthRaw Width, Height, MinWidth, MinHeight, MaxWidth, MaxHeight;
        public EdgesRaw Margin, Padding, BorderWidth; public int BoxSizing;
        public ColorRaw Color, BackgroundColor; public BackgroundLayerListRaw BackgroundLayers; public BackdropFilterListRaw BackdropFilters;
        public ColorRaw BorderColor; public ColorEdgesRaw BorderColors; public BorderStyleEdgesRaw BorderStyles;
        public int HasTextGradient; public GradientRaw TextGradient;
        public TransformListRaw Transforms; public TransformOriginRaw TransformOrigin;
        public ShadowListRaw TextShadows;
        public ShadowListRaw BoxShadows;
        public BorderRadiiRaw BorderRadii; public float Opacity;
        public fixed byte FontFamily[64];
        public float FontSize; public int FontWeight; public float LineHeight; public int TextAlign, TextTransform, WhiteSpace;
        public int OverflowX, OverflowY, Cursor, PointerEvents, Visible;
        public int ZIndex, ZIndexKind;
        public int TransitionProperties;
        public float TransitionDuration;
        public float TransitionDelay;
        public int TransitionTiming;

        public NativeComputedStyle ToPublic()
        {
            string family;
            fixed (byte* p = FontFamily) family = Marshal.PtrToStringUTF8((IntPtr)p) ?? "Segoe UI";
            return new(Display, FlexDirection, JustifyContent, AlignItems, AlignSelf, FlexGrow, FlexShrink, FlexBasis.ToPublic(), FlexWrap != 0, Gap,
                Position, Inset.ToPublic(),
                Width.ToPublic(), Height.ToPublic(), MinWidth.ToPublic(), MinHeight.ToPublic(),
                MaxWidth.ToPublic(), MaxHeight.ToPublic(), Margin.ToPublic(), Padding.ToPublic(),
                BorderWidth.ToPublic(), BoxSizing, Color.ToPublic(), BackgroundColor.ToPublic(),
                ReadBackgroundLayers(BackgroundLayers), ReadBackdropFilters(BackdropFilters), BorderColor.ToPublic(), BorderColors.ToPublic(), BorderStyles.ToPublic(),
                ReadGradient(HasTextGradient, TextGradient), ReadTransforms(Transforms), TransformOrigin.ToPublic(),
                ReadShadows(TextShadows), BorderRadii.ToPublic(), Opacity,
                ReadShadows(BoxShadows),
                family, FontSize, FontWeight, LineHeight, TextAlign, TextTransform, WhiteSpace,
                OverflowX, OverflowY, Cursor, PointerEvents, Visible != 0,
                TransitionProperties, TransitionDuration, TransitionDelay, TransitionTiming);
        }
    }

    private static NativeTransformOperation[] ReadTransforms(TransformListRaw transforms)
    {
        if (transforms.Count <= 0 || transforms.Items == IntPtr.Zero) return [];
        var size = Marshal.SizeOf<TransformOperationRaw>();
        var result = new NativeTransformOperation[transforms.Count];
        for (var index = 0; index < result.Length; ++index)
            result[index] = Marshal.PtrToStructure<TransformOperationRaw>(transforms.Items + index * size).ToPublic();
        return result;
    }

    private static NativeGradient? ReadGradient(int present, GradientRaw gradient)
    {
        if (present == 0 || gradient.Count <= 0 || gradient.Stops == IntPtr.Zero) return null;
        if (gradient.Count > 4096)
            throw new InvalidOperationException($"Native gradient stop count {gradient.Count} exceeds the ABI safety limit.");
        var size = Marshal.SizeOf<GradientStopRaw>();
        var stops = new NativeGradientStop[gradient.Count];
        for (var i = 0; i < stops.Length; i++)
        {
            var raw = Marshal.PtrToStructure<GradientStopRaw>(gradient.Stops + i * size);
            stops[i] = new NativeGradientStop(raw.Color.ToPublic(), raw.Position.ToPublic(), raw.HasPosition != 0, raw.Offset);
        }
        return new NativeGradient(gradient.X, gradient.Y, stops);
    }

    private static NativeBackgroundLayer[] ReadBackgroundLayers(BackgroundLayerListRaw layers)
    {
        if (layers.Count <= 0 || layers.Items == IntPtr.Zero) return [];
        var size = Marshal.SizeOf<BackgroundLayerRaw>();
        var result = new NativeBackgroundLayer[layers.Count];
        for (var index = 0; index < result.Length; index++)
        {
            result[index] = Marshal.PtrToStructure<BackgroundLayerRaw>(layers.Items + index * size).ToPublic();
        }
        return result;
    }

    private static NativeBackdropFilter[] ReadBackdropFilters(BackdropFilterListRaw filters)
    {
        if (filters.Count <= 0 || filters.Items == IntPtr.Zero) return [];
        var size = Marshal.SizeOf<BackdropFilterRaw>();
        var result = new NativeBackdropFilter[filters.Count];
        for (var index = 0; index < result.Length; index++)
            result[index] = Marshal.PtrToStructure<BackdropFilterRaw>(filters.Items + index * size).ToPublic();
        return result;
    }

    private static NativeShadow[] ReadShadows(ShadowListRaw shadows)
    {
        if (shadows.Count <= 0 || shadows.Items == IntPtr.Zero) return [];
        var size = Marshal.SizeOf<ShadowRaw>();
        var result = new NativeShadow[shadows.Count];
        for (var i = 0; i < result.Length; i++)
        {
            result[i] = Marshal.PtrToStructure<ShadowRaw>(shadows.Items + i * size).ToPublic();
        }
        return result;
    }
}
