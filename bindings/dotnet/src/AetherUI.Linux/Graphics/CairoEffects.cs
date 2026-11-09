using System.Runtime.InteropServices;
using AetherUI.Primitives;
using AetherUI.Rendering;

namespace AetherUI.Linux.Graphics;

public sealed partial class CairoRenderBackend
{
    // Three image buffers plus the native surface fit within this per-operation budget.
    private const long EffectMemoryBudget = 256L * 1024 * 1024;
    private const int MaximumKernelRadius = 256;
    private const long MaximumConvolutionSamples = 250_000_000;

    private void DrawBoxShadow(IntPtr cr, DrawBoxShadowCommand command)
    {
        var shadow = command.Shadow;
        ValidateEffectShadow(shadow);
        if (shadow.Color.A <= 0 || command.BorderBoxRectangle.IsEmpty)
            return;
        var sigma = shadow.BlurRadius * .5f;
        var padding = MathF.Ceiling(3 * sigma + 2 / _dpi);
        var shape = command.BorderBoxRectangle.Inflate(shadow.Inset ? -shadow.SpreadRadius : shadow.SpreadRadius)
            .Offset(shadow.OffsetX, shadow.OffsetY);
        var bounds = shadow.Inset ? command.BorderBoxRectangle.Inflate(padding)
            : shape.Inflate(padding);
        if (bounds.IsEmpty)
            return;
        var (width, height) = EffectSize(bounds);
        using var mask = CreateSurface(width, height);
        using (var context = mask.CreateContext())
        {
            var mc = context.Pointer;
            FxScale(mc, _dpi, _dpi);
            FxTranslate(mc, -bounds.X, -bounds.Y);
            Color(mc, shadow.Color);
            if (shadow.Inset)
            {
                FxPaint(mc);
                FxSetOperator(mc, 0); // CLEAR the inner opening, leaving the inset shadow mask.
            }
            if (!shape.IsEmpty)
            {
                RoundPath(mc, shape, InflateShadowRadii(command.Radii,
                    shadow.Inset ? -shadow.SpreadRadius : shadow.SpreadRadius));
                FxFill(mc);
            }
            CairoNative.Check(CairoNative.cairo_status(mc));
        }
        var pixels = mask.ReadPixels();
        Gaussian(pixels, width, height, sigma * _dpi, 1, 0, 0, 1);
        mask.WritePixels(pixels);
        FxSave(cr);
        try
        {
            if (shadow.Inset)
            {
                RoundPath(cr, command.BorderBoxRectangle, command.Radii);
                FxClip(cr);
            }
            else
            {
                // An outset shadow belongs outside the border box, not under transparent content.
                FxNewPath(cr);
                FxRectangle(cr, bounds.X, bounds.Y, bounds.Width, bounds.Height);
                RoundPathAppend(cr, command.BorderBoxRectangle, command.Radii);
                FxSetFillRule(cr, 1); // EVEN_ODD
                FxClip(cr);
                FxSetFillRule(cr, 0);
            }
            CompositeLocalMask(cr, mask, bounds);
        }
        finally { FxRestore(cr); }
    }

    private void DrawTextShadow(IntPtr cr, DrawTextShadowCommand command)
    {
        ValidateEffectShadow(command.Shadow);
        if (command.Shadow.Color.A <= 0 || string.IsNullOrEmpty(command.Text))
            return;
        var shadow = command.Shadow;
        var sigma = shadow.BlurRadius * .5f;
        // Include room for the convolution halo. As on Windows, glyph ink is clipped to
        // the text rectangle before blur; the resulting halo may extend beyond it.
        var padding = MathF.Ceiling(3 * sigma + command.FontSize * 2 + 2 / _dpi);
        var rect = command.Rectangle.Offset(shadow.OffsetX, shadow.OffsetY);
        var bounds = rect.Inflate(padding);
        var (width, height) = EffectSize(bounds);
        using var mask = CreateSurface(width, height);
        using (var context = mask.CreateContext())
        {
            var mc = context.Pointer;
            FxScale(mc, _dpi, _dpi);
            FxTranslate(mc, -bounds.X, -bounds.Y);
            // Color glyph fonts can ignore the Cairo source RGB. Render full glyph coverage,
            // then use only alpha to construct a uniformly tinted premultiplied shadow.
            Color(mc, ColorRgba.White);
            _text.Draw(mc, command.Text, rect, command.FontFamily, command.FontSize,
                command.FontWeight, command.Alignment, command.Wrap);
            CairoNative.Check(CairoNative.cairo_status(mc));
        }
        var pixels = mask.ReadPixels();
        for (var index = 0; index < pixels.Length; index += 4)
        {
            var alpha = pixels[index + 3] * Math.Clamp(shadow.Color.A, 0, 1);
            pixels[index] = (byte)MathF.Round(alpha * Math.Clamp(shadow.Color.B, 0, 1));
            pixels[index + 1] = (byte)MathF.Round(alpha * Math.Clamp(shadow.Color.G, 0, 1));
            pixels[index + 2] = (byte)MathF.Round(alpha * Math.Clamp(shadow.Color.R, 0, 1));
            pixels[index + 3] = (byte)MathF.Round(alpha);
        }
        Gaussian(pixels, width, height, sigma * _dpi, 1, 0, 0, 1);
        mask.WritePixels(pixels);
        FxSave(cr);
        try
        {
            CompositeLocalMask(cr, mask, bounds);
        }
        finally { FxRestore(cr); }
    }

    private void CompositeLocalMask(IntPtr cr, CairoSurface mask, RectF bounds)
    {
        FxTranslate(cr, bounds.X, bounds.Y);
        FxScale(cr, 1 / _dpi, 1 / _dpi);
        FxSetSourceSurface(cr, mask.Handle, 0, 0);
        FxPaint(cr);
    }

    private void ApplyBackdrop(IntPtr cr, ApplyBackdropFilterCommand command)
    {
        ArgumentNullException.ThrowIfNull(command.Filters);
        if (command.Filters.Count == 0 || command.Rectangle.IsEmpty)
            return;
        CheckEffectBudget(_width, _height);
        var target = FxGetTarget(cr);
        FxSurfaceFlush(target);
        var stride = FxGetStride(target);
        if (stride != checked(_width * 4))
            throw new InvalidOperationException("Unexpected Cairo image stride.");
        var pixels = new byte[checked(stride * _height)];
        Marshal.Copy(FxGetData(target), pixels, 0, pixels.Length);
        FxGetMatrix(cr, out var matrix);
        foreach (var filter in command.Filters)
        {
            switch (filter.Kind)
            {
                case BackdropFilterKind.Blur:
                    var amount = filter.LengthAmount.Resolve(0, 16, 16, _width / _dpi, _height / _dpi);
                    if (!float.IsFinite(amount) || amount < 0)
                        throw new ArgumentException("Invalid backdrop blur.");
                    // Sample along the transformed local axes: the backdrop stays in device space,
                    // while an affine transform rotates/scales the two separable blur directions.
                    Gaussian(pixels, _width, _height, amount * .5,
                        matrix.Xx, matrix.Yx, matrix.Xy, matrix.Yy);
                    break;
                case BackdropFilterKind.Brightness:
                case BackdropFilterKind.Contrast:
                case BackdropFilterKind.Saturate:
                    if (!float.IsFinite(filter.ScalarAmount) || filter.ScalarAmount < 0)
                        throw new ArgumentException("Invalid backdrop filter amount.");
                    FilterColors(pixels, filter.Kind, filter.ScalarAmount);
                    break;
                default:
                    throw new NotSupportedException($"Unknown backdrop filter {filter.Kind}.");
            }
        }
        FxSave(cr);
        try
        {
            // Capture the clip in the original local transform BEFORE device-space painting.
            RoundPath(cr, command.Rectangle, command.Radii);
            FxClip(cr);
            FxSetOperator(cr, 1); // SOURCE replaces the snapshot, including its alpha.
            PaintPixels(cr, pixels, _width, _height);
        }
        finally { FxRestore(cr); }
    }

    private (int Width, int Height) EffectSize(RectF bounds)
    {
        var width = Math.Ceiling(bounds.Width * _dpi);
        var height = Math.Ceiling(bounds.Height * _dpi);
        if (!double.IsFinite(width) || !double.IsFinite(height) || width <= 0 || height <= 0 || width > 32767 || height > 32767)
            throw new ArgumentOutOfRangeException(nameof(bounds), "Shadow dimensions exceed the software effect limit.");
        CheckEffectBudget((int)width, (int)height);
        return ((int)width, (int)height);
    }

    private static void CheckEffectBudget(int width, int height)
    {
        if (checked((long)width * height * 4 * 4) > EffectMemoryBudget)
            throw new InvalidOperationException("Software effect exceeds the 256 MiB working-memory budget.");
    }

    private static void ValidateEffectShadow(BoxShadow shadow)
    {
        if (!float.IsFinite(shadow.BlurRadius) || shadow.BlurRadius < 0 ||
            !float.IsFinite(shadow.SpreadRadius) || !float.IsFinite(shadow.OffsetX) || !float.IsFinite(shadow.OffsetY))
            throw new ArgumentException("Shadow values must be finite and blur must be nonnegative.");
    }

    private static CornerRadii InflateShadowRadii(CornerRadii r, float v) => new(
        MathF.Max(0, r.TopLeftX + v), MathF.Max(0, r.TopLeftY + v),
        MathF.Max(0, r.TopRightX + v), MathF.Max(0, r.TopRightY + v),
        MathF.Max(0, r.BottomRightX + v), MathF.Max(0, r.BottomRightY + v),
        MathF.Max(0, r.BottomLeftX + v), MathF.Max(0, r.BottomLeftY + v));

    // RoundPath starts a new path. Appending a copied path preserves the exterior rectangle.
    private static void RoundPathAppend(IntPtr cr, RectF rect, CornerRadii radii)
    {
        var preceding = FxCopyPath(cr);
        try
        {
            RoundPath(cr, rect, radii);
            FxAppendPath(cr, preceding);
        }
        finally { FxDestroyPath(preceding); }
    }

    private static void Gaussian(byte[] pixels, int width, int height, double sigma,
        double xx, double yx, double xy, double yy)
    {
        if (sigma <= 0)
            return;
        var lengthX = Math.Sqrt(xx * xx + yx * yx);
        var lengthY = Math.Sqrt(xy * xy + yy * yy);
        var sigmaX = sigma * lengthX;
        var sigmaY = sigma * lengthY;
        static (double[] Weights, int Radius) Kernel(double deviation)
        {
            if (!double.IsFinite(deviation) || deviation * 3 > MaximumKernelRadius)
                throw new ArgumentOutOfRangeException(nameof(deviation), "Software blur kernel exceeds the 256 pixel-radius limit.");
            if (deviation <= .0001)
                return (new[] { 1d }, 0);
            var radius = Math.Max(1, (int)Math.Ceiling(deviation * 3));
            var weights = new double[radius * 2 + 1];
            double sum = 0;
            for (var k = -radius; k <= radius; k++)
                sum += weights[k + radius] = Math.Exp(-k * k / (2 * deviation * deviation));
            for (var k = 0; k < weights.Length; k++)
                weights[k] /= sum;
            return (weights, radius);
        }
        var xKernel = Kernel(sigmaX);
        var yKernel = Kernel(sigmaY);
        if (checked((long)width * height * (xKernel.Weights.Length + yKernel.Weights.Length)) > MaximumConvolutionSamples)
            throw new InvalidOperationException("Software blur exceeds the 250 million sample work budget.");
        var temporary = new byte[pixels.Length];
        // Normalize the direction and grow sigma in device pixels, rather than leaving gaps
        // between samples when an element is magnified or rendered at high DPI.
        Convolve(pixels, temporary, width, height, xKernel.Weights, xKernel.Radius,
            lengthX > 0 ? xx / lengthX : 0, lengthX > 0 ? yx / lengthX : 0);
        Convolve(temporary, pixels, width, height, yKernel.Weights, yKernel.Radius,
            lengthY > 0 ? xy / lengthY : 0, lengthY > 0 ? yy / lengthY : 0);
    }

    private static void Convolve(byte[] input, byte[] output, int width, int height,
        double[] kernel, int radius, double dx, double dy)
    {
        for (var y = 0; y < height; y++)
            for (var x = 0; x < width; x++)
                for (var channel = 0; channel < 4; channel++)
                {
                    double value = 0;
                    for (var k = -radius; k <= radius; k++)
                        value += Sample(input, width, height, x + k * dx, y + k * dy, channel) * kernel[k + radius];
                    output[(y * width + x) * 4 + channel] = (byte)Math.Clamp(Math.Round(value), 0, 255);
                }
    }

    private static double Sample(byte[] pixels, int width, int height, double x, double y, int channel)
    {
        if (x <= -1 || y <= -1 || x >= width || y >= height)
            return 0;
        var ix = (int)Math.Floor(x);
        var iy = (int)Math.Floor(y);
        var fx = x - ix;
        var fy = y - iy;
        double At(int px, int py) => px < 0 || py < 0 || px >= width || py >= height ? 0 : pixels[(py * width + px) * 4 + channel];
        return At(ix, iy) * (1 - fx) * (1 - fy) + At(ix + 1, iy) * fx * (1 - fy)
            + At(ix, iy + 1) * (1 - fx) * fy + At(ix + 1, iy + 1) * fx * fy;
    }

    private static void FilterColors(byte[] pixels, BackdropFilterKind kind, float amount)
    {
        for (var i = 0; i < pixels.Length; i += 4)
        {
            var alpha = pixels[i + 3];
            if (alpha == 0)
            {
                pixels[i] = pixels[i + 1] = pixels[i + 2] = 0;
                continue;
            }
            var r = pixels[i + 2] / (double)alpha;
            var g = pixels[i + 1] / (double)alpha;
            var b = pixels[i] / (double)alpha;
            switch (kind)
            {
                case BackdropFilterKind.Brightness:
                    r *= amount;
                    g *= amount;
                    b *= amount;
                    break;
                case BackdropFilterKind.Contrast:
                    r = (r - .5) * amount + .5;
                    g = (g - .5) * amount + .5;
                    b = (b - .5) * amount + .5;
                    break;
                case BackdropFilterKind.Saturate:
                    var l = .213 * r + .715 * g + .072 * b;
                    r = l + amount * (r - l);
                    g = l + amount * (g - l);
                    b = l + amount * (b - l);
                    break;
            }
            pixels[i] = (byte)Math.Round(Math.Clamp(b, 0, 1) * alpha);
            pixels[i + 1] = (byte)Math.Round(Math.Clamp(g, 0, 1) * alpha);
            pixels[i + 2] = (byte)Math.Round(Math.Clamp(r, 0, 1) * alpha);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct FxMatrix
    {
        public double Xx, Yx, Xy, Yy, X0, Y0;
    }
    private const string FxCairo = "libcairo.so.2";
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_save")] private static extern void FxSave(IntPtr cr);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_restore")] private static extern void FxRestore(IntPtr cr);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_scale")] private static extern void FxScale(IntPtr cr, double x, double y);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_translate")] private static extern void FxTranslate(IntPtr cr, double x, double y);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_paint")] private static extern void FxPaint(IntPtr cr);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_fill")] private static extern void FxFill(IntPtr cr);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_clip")] private static extern void FxClip(IntPtr cr);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_new_path")] private static extern void FxNewPath(IntPtr cr);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_rectangle")] private static extern void FxRectangle(IntPtr cr, double x, double y, double w, double h);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_set_operator")] private static extern void FxSetOperator(IntPtr cr, int value);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_set_fill_rule")] private static extern void FxSetFillRule(IntPtr cr, int value);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_set_source_surface")] private static extern void FxSetSourceSurface(IntPtr cr, IntPtr surface, double x, double y);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_get_target")] private static extern IntPtr FxGetTarget(IntPtr cr);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_surface_flush")] private static extern void FxSurfaceFlush(IntPtr surface);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_image_surface_get_data")] private static extern IntPtr FxGetData(IntPtr surface);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_image_surface_get_stride")] private static extern int FxGetStride(IntPtr surface);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_get_matrix")] private static extern void FxGetMatrix(IntPtr cr, out FxMatrix matrix);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_copy_path")] private static extern IntPtr FxCopyPath(IntPtr cr);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_append_path")] private static extern void FxAppendPath(IntPtr cr, IntPtr path);
    [DllImport(FxCairo, CallingConvention = CallingConvention.Cdecl, EntryPoint = "cairo_path_destroy")] private static extern void FxDestroyPath(IntPtr path);
}
