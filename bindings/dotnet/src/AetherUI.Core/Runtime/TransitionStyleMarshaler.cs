using System.Runtime.InteropServices;
using AetherUI.Core.Native;

namespace AetherUI.Runtime;

/// <summary>
/// Marshals managed <see cref="AetherUI.Css.ComputedStyle"/> values into native-layout
/// <c>aether_computed_style_t</c> copies (mirrored by <see cref="ComputedStyleRaw"/>) for the CSS
/// transition runtime's <c>observe</c> call.
/// </summary>
/// <remarks>
/// The native runtime reads only a handful of fields from each style: the transition
/// property/duration/delay/timing bits, opacity, and the transform list + origin + font size used to
/// resolve the transform matrix. Only those fields are populated; the rest of the flat struct stays
/// zero-initialized, which is harmless because native observe never reads them.
/// </remarks>
internal static class TransitionStyleMarshaler
{
    /// <summary>
    /// Converts <paramref name="before"/> and <paramref name="after"/> into temporary native copies,
    /// calls <see cref="TransitionRuntimeInterop.Observe"/> and releases everything on exit. Both
    /// styles must be non-null; the host skips the call when no previous style exists.
    /// </summary>
    public static void Observe(
        TransitionRuntimeInterop runtime,
        IntPtr node,
        AetherUI.Css.ComputedStyle before,
        AetherUI.Css.ComputedStyle after,
        long nowMs)
    {
        using var prepared = Prepare(node, before, after);
        prepared.Apply(runtime, nowMs);
    }

    internal static PreparedObservation Prepare(IntPtr node, AetherUI.Css.ComputedStyle before, AetherUI.Css.ComputedStyle after)
        => new(node, before, after);

    internal sealed class PreparedObservation : IDisposable
    {
        private readonly IntPtr _node;
        private IntPtr _before, _after;
        private (IntPtr Items, int Count) _beforeTransforms, _afterTransforms;
        internal PreparedObservation(IntPtr node, AetherUI.Css.ComputedStyle before, AetherUI.Css.ComputedStyle after)
        {
            _node = node;
            try
            {
                var size = Marshal.SizeOf<StyleRuntimeInterop.ComputedStyleRaw>();
                _beforeTransforms = AllocateTransforms(before);
                _afterTransforms = AllocateTransforms(after);
                _before = Marshal.AllocHGlobal(size);
                _after = Marshal.AllocHGlobal(size);
                var beforeRaw = CreateRaw(before);
                beforeRaw.Transforms = new StyleRuntimeInterop.TransformListRaw(_beforeTransforms.Items, _beforeTransforms.Count);
                var afterRaw = CreateRaw(after);
                afterRaw.Transforms = new StyleRuntimeInterop.TransformListRaw(_afterTransforms.Items, _afterTransforms.Count);
                Marshal.StructureToPtr(beforeRaw, _before, false);
                Marshal.StructureToPtr(afterRaw, _after, false);
            }
            catch { Dispose(); throw; }
        }
        internal void Apply(TransitionRuntimeInterop runtime, long nowMs) => runtime.Observe(_node, _before, _after, nowMs);
        public void Dispose()
        {
            Marshal.FreeHGlobal(_after); Marshal.FreeHGlobal(_before);
            FreeTransforms(_afterTransforms); FreeTransforms(_beforeTransforms);
            _before = _after = IntPtr.Zero; _beforeTransforms = _afterTransforms = default;
        }
    }

    private static StyleRuntimeInterop.ComputedStyleRaw CreateRaw(AetherUI.Css.ComputedStyle style)
    {
        var raw = default(StyleRuntimeInterop.ComputedStyleRaw);
        raw.Opacity = style.Opacity;
        raw.FontSize = style.FontSize;
        raw.TransformOrigin = new StyleRuntimeInterop.TransformOriginRaw(
            ToLengthRaw(style.TransformOrigin.X), ToLengthRaw(style.TransformOrigin.Y));
        raw.TransitionProperties = style.TransitionProperties;
        raw.TransitionDuration = style.TransitionDuration;
        raw.TransitionDelay = style.TransitionDelay;
        raw.TransitionTiming = style.TransitionTiming;
        return raw;
    }

    private static (IntPtr Items, int Count) AllocateTransforms(AetherUI.Css.ComputedStyle style)
    {
        var transforms = style.Transforms;
        if (transforms.Count == 0)
        {
            return (IntPtr.Zero, 0);
        }

        var itemSize = Marshal.SizeOf<StyleRuntimeInterop.TransformOperationRaw>();
        var items = Marshal.AllocHGlobal(itemSize * transforms.Count);
        try
        {
            for (var index = 0; index < transforms.Count; index++)
            {
                var operation = transforms[index];
                var raw = new StyleRuntimeInterop.TransformOperationRaw(
                    (int)operation.Kind,
                    ToLengthRaw(operation.X), ToLengthRaw(operation.Y),
                    operation.ScalarX, operation.ScalarY, operation.AngleRadians,
                    operation.MatrixA, operation.MatrixB, operation.MatrixC,
                    operation.MatrixD, operation.MatrixE, operation.MatrixF);
                Marshal.StructureToPtr(raw, items + index * itemSize, false);
            }
        }
        catch { Marshal.FreeHGlobal(items); throw; }

        return (items, transforms.Count);
    }

    private static void FreeTransforms((IntPtr Items, int Count) transforms)
    {
        if (transforms.Items != IntPtr.Zero)
        {
            Marshal.FreeHGlobal(transforms.Items);
        }
    }

    private static StyleRuntimeInterop.LengthRaw ToLengthRaw(AetherUI.Css.CssLength length) => new(
        length.Value, (int)length.Unit, length.LinearPercent, length.LinearEm,
        length.LinearRem, length.LinearViewportWidth, length.LinearViewportHeight);
}
