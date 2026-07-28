namespace AetherUI.Core.Native;

/// <summary>
/// Versioned contract for the native tagged display-list ABI. The managed
/// decoder uses byte offsets, so pre-1.0 bindings require an exact ABI match.
/// </summary>
public static class NativeAbiContract
{
    public const uint ExpectedVersion = (0u << 24) | (18u << 16) | 0u;
    public const ulong RequiredCapabilities = (1UL << 0) | (1UL << 1) | (1UL << 2) | (1UL << 3);

    /// <summary>Validates the loaded runtime before native resources are created.</summary>
    public static int EnsureCompatible()
    {
        if (IntPtr.Size != 8)
            throw new PlatformNotSupportedException(
                "AetherUI .NET display-list decoding for ABI 0.18.0 currently requires a 64-bit process.");
        var commandSize = Validate(NativeMethods.AbiVersion(), NativeMethods.AbiCapabilities(), NativeMethods.PaintCommandSize());
        ValidateComputedStyleSize(NativeMethods.ComputedStyleSize(), StyleRuntimeInterop.ComputedStyleSize);
        return commandSize;
    }

    public static int Validate(uint version, ulong capabilities, nuint commandSize)
    {
        if (version != ExpectedVersion)
            throw new InvalidOperationException(
                $"AetherUI native ABI mismatch: managed binding expects 0.18.0 (0x{ExpectedVersion:X8}), " +
                $"but the loaded runtime reports 0x{version:X8}.");
        if ((capabilities & RequiredCapabilities) != RequiredCapabilities)
            throw new InvalidOperationException(
                $"AetherUI native runtime is missing required capabilities: expected mask 0x{RequiredCapabilities:X}, " +
                $"actual mask 0x{capabilities:X}.");
        if (commandSize < NativePaintCommandLayout.MinimumBufferSize || commandSize > int.MaxValue)
            throw new InvalidOperationException(
                $"AetherUI native paint-command size {commandSize} is incompatible with ABI 0.18.0 " +
                $"(minimum readable size {NativePaintCommandLayout.MinimumBufferSize}).");
        return checked((int)commandSize);
    }

    /// <summary>Rejects a native style layout that cannot fit in the managed interop buffer.</summary>
    public static void ValidateComputedStyleSize(nuint nativeSize, int managedSize)
    {
        if (nativeSize != checked((nuint)managedSize))
            throw new InvalidOperationException(
                $"AetherUI native computed-style size {nativeSize} does not match the managed ABI 0.18.0 size {managedSize}.");
    }
}

/// <summary>Byte layout of aether_paint_command_t for ABI 0.18.0 on supported 64-bit ABIs.</summary>
public static class NativePaintCommandLayout
{
    public const int Tag = 0;
    public const int FillRect = 8, FillRadii = 24, FillColor = 56;
    public const int ShadowRect = 8, ShadowRadii = 24, ShadowOffsetX = 56, ShadowColor = 72, ShadowInset = 88;
    public const int GradientRect = 8, GradientRadii = 24, GradientData = 56, GradientOpacity = 80;
    public const int StrokeRect = 8, StrokeRadii = 24, StrokeWidth = 56, StrokeColor = 60;
    public const int TextPointer = 8, TextRect = 16, TextFamily = 32, TextSize = 96,
        TextWeight = 100, TextAlignment = 104, TextColor = 108, TextWrap = 124;
    public const int TextGradientData = 112, TextGradientOpacity = 136;
    public const int TextShadowOffsetX = 108, TextShadowColor = 124, TextShadowInset = 140, TextShadowWrap = 144;
    public const int ClipRect = 8, ClipRadii = 24;
    public const int BorderOuterRect = 8, BorderOuterRadii = 24, BorderInnerRect = 56,
        BorderInnerRadii = 72, BorderSide = 104, BorderStyle = 108, BorderColor = 112;
    public const int BackgroundImageSource = 8, BackgroundImageRect = 16, BackgroundImagePositionX = 32,
        BackgroundImagePositionY = 60, BackgroundImageSizeKind = 88, BackgroundImageSizeX = 92,
        BackgroundImageSizeY = 120, BackgroundImageRepeatX = 148, BackgroundImageRepeatY = 152,
        BackgroundImageOpacity = 156, BackgroundImageSourceRect = 160,
        BackgroundImageDestinationRect = 176, BackgroundImageHasResolvedGeometry = 192;
    public const int BackdropRect = 8, BackdropRadii = 24, BackdropFilters = 56, BackdropFilterCount = 64;
    public const int TransformMatrix = 8;

    public const int MinimumBufferSize = BackgroundImageHasResolvedGeometry + sizeof(int);
}
