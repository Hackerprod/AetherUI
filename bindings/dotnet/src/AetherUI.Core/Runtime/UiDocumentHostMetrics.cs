namespace AetherUI.Runtime;

/// <summary>
/// Observable host-pipeline work. Tests assert operation counts; elapsed-time samples are exposed
/// for manual performance diagnosis and should not be used as fixed-threshold assertions.
/// </summary>
public readonly record struct UiDocumentHostMetrics(
    long StyleResolutions,
    long StyleUpdatedElements,
    long LayoutBuilds,
    long DisplayListBuilds,
    long PaintChunkBuilds,
    long ScrollCompositions,
    long RenderCalls,
    long IncrementalRenderCalls,
    long VisualStyleMapBuilds,
    long VisualStyleMapUpdates,
    long StyleResolutionTimestampTicks,
    long DisplayListBuildTimestampTicks,
    long RenderTimestampTicks,
    AetherUI.Core.Native.NativeStyleImpact LastStyleImpact)
{
    public double StyleResolutionMilliseconds =>
        StyleResolutionTimestampTicks * 1000d / System.Diagnostics.Stopwatch.Frequency;

    public double DisplayListBuildMilliseconds =>
        DisplayListBuildTimestampTicks * 1000d / System.Diagnostics.Stopwatch.Frequency;

    public double RenderMilliseconds =>
        RenderTimestampTicks * 1000d / System.Diagnostics.Stopwatch.Frequency;
}
