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
    /// <summary>Calls to PointerMoved, including captured scrollbar movement. Zero unless input work counters are enabled.</summary>
    public long PointerMovesReceived { get; init; }
    /// <summary>Pending hover positions consumed by ApplyPendingPointerMove, including visibility-driven settling. Zero unless input work counters are enabled.</summary>
    public long PendingMovesProcessed { get; init; }
    /// <summary>Existing pending positions superseded or canceled by a later PointerMoved; not new coalescing behavior. Zero unless input work counters are enabled.</summary>
    public long PendingMovesCoalesced { get; init; }
    /// <summary>PointerMoved calls whose synchronous hit equals the current hovered target. Zero unless input work counters are enabled.</summary>
    public long SameTargetMoves { get; init; }
    /// <summary>Calls to PointerPressed, PointerReleased or PointerLeft. Observation only; no flush is implied. Zero unless input work counters are enabled.</summary>
    public long PointerBarriers { get; init; }
    /// <summary>Calls to PointerPressed, PointerReleased or PointerLeft. Observation only; no flush is implied. Zero unless input work counters are enabled.</summary>
    /// <summary>Those barrier calls that arrive with a pending hover position. Zero unless input work counters are enabled.</summary>
    public long PointerBarriersWithPendingMove { get; init; }
    /// <summary>Calls to CursorAt, including early returns that do not hit test. Zero unless input work counters are enabled.</summary>
    public long CursorQueries { get; init; }
    /// <summary>Calls to CursorFor, including resolutions during hover settling. Zero unless input work counters are enabled.</summary>
    public long CursorResolutions { get; init; }
    /// <summary>Host HitTest calls, including native or managed traversal; not visited boxes. Zero unless input work counters are enabled.</summary>
    public long HitQueries { get; init; }

    public double StyleResolutionMilliseconds =>
        StyleResolutionTimestampTicks * 1000d / System.Diagnostics.Stopwatch.Frequency;

    public double DisplayListBuildMilliseconds =>
        DisplayListBuildTimestampTicks * 1000d / System.Diagnostics.Stopwatch.Frequency;

    public double RenderMilliseconds =>
        RenderTimestampTicks * 1000d / System.Diagnostics.Stopwatch.Frequency;
}
