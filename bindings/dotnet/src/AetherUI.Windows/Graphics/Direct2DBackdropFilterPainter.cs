using System.Numerics;
using AetherUI.Primitives;
using AetherUI.Rendering;
using SharpGen.Runtime;
using Vortice;
using Vortice.Direct2D1;
using Vortice.Mathematics;

namespace AetherUI.Windows.Graphics;

/// <summary>
/// Applies the supported CSS backdrop operations to a stable, already-painted scene snapshot.
/// Target selection, snapshot lifetime, and clipping belong to the render backend; this class
/// only owns the filter mathematics and Direct2D effect lifetime.
/// </summary>
internal static class Direct2DBackdropFilterPainter
{
    private const float MinimumSigma = .125f;

    internal static void Draw(
        ID2D1DeviceContext target,
        ID2D1Bitmap1 backdrop,
        ApplyBackdropFilterCommand command)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(backdrop);
        ArgumentNullException.ThrowIfNull(command);

        // CSS filter functions form a pipeline. Blur is the only v1 operation; successive
        // Gaussian convolutions compose to one Gaussian whose variance is the sum of theirs.
        // Retaining the operation list in the ABI keeps the dispatch extensible when the other
        // CSS filter functions gain render support.
        var variance = 0f;
        foreach (var filter in command.Filters)
        {
            if (filter.Kind != BackdropFilterKind.Blur)
            {
                throw new NotSupportedException($"Backdrop filter '{filter.Kind}' reached the Direct2D painter without renderer support.");
            }

            var sigma = ToStandardDeviation(filter.LengthAmount.Value);
            variance += sigma * sigma;
        }

        if (variance <= 0f)
        {
            return;
        }

        var standardDeviation = MathF.Sqrt(variance);
        var padding = MathF.Ceiling(3f * standardDeviation + 1f);
        var sampleBounds = Inflate(command.Rectangle, padding);
        using var effect = new ID2D1Effect(target.CreateEffect(EffectGuids.GaussianBlur));
        effect.SetInput(0, backdrop, true);
        effect.SetValue((uint)GaussianBlurProperties.StandardDeviation, standardDeviation);
        effect.SetValue((uint)GaussianBlurProperties.Optimization, GaussianBlurOptimization.Quality);
        using var image = effect.QueryInterface<ID2D1Image>();
        target.DrawImage(
            image,
            // DrawImage places an imageRectangle at targetOffset; retaining the source
            // origin avoids moving the sampled patch to (0,0) on non-zero positioned panels.
            new Vector2(sampleBounds.Left, sampleBounds.Top),
            ToRawRect(sampleBounds),
            InterpolationMode.Linear,
            CompositeMode.SourceOver);
    }

    private static float ToStandardDeviation(float cssBlurRadius) =>
        MathF.Max(MinimumSigma, MathF.Max(0f, cssBlurRadius) * .5f);

    private static RectF Inflate(RectF rectangle, float amount) => new(
        rectangle.X - amount,
        rectangle.Y - amount,
        rectangle.Width + amount * 2f,
        rectangle.Height + amount * 2f);

    private static RawRectF ToRawRect(RectF rectangle) =>
        new(rectangle.Left, rectangle.Top, rectangle.Right, rectangle.Bottom);
}

/// <summary>Per-frame counters used to prove that the direct path remains allocation-free.</summary>
internal sealed class BackdropCompositionMetrics
{
    internal int Allocations { get; private set; }
    internal int CompositionPasses { get; private set; }
    internal int SnapshotPasses { get; private set; }

    internal void Reset() => (Allocations, CompositionPasses, SnapshotPasses) = (0, 0, 0);
    internal void RecordAllocation() => Allocations++;
    internal void RecordCompositionPass() => CompositionPasses++;
    internal void RecordSnapshotPass() => SnapshotPasses++;
}
