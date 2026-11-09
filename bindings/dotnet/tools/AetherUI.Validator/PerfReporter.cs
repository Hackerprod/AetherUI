using AetherUI.Runtime;
using AetherUI.Core.Native;

namespace AetherUI.Validator;

public sealed class PerfReporter
{
    private readonly object _gate = new();
    private readonly string _label;
    private DateTime _lastReport = DateTime.UtcNow;
    private UiDocumentHostMetrics _lastMetrics;
    private bool _hasLastMetrics;
    private readonly bool _workRequested = Environment.GetEnvironmentVariable("AETHERUI_WORK_COUNTERS") == "1";
    private int _workThreadId;
    private bool _workAvailable;
    private NativeWorkCountersSnapshot _lastWork;

    // Call only at the existing reporting cadence. Counters are thread-wide,
    // not host-specific, and are never reset by this consumer.
    private void ReportWork()
    {
        if (!_workRequested) return;
        var threadId = Environment.CurrentManagedThreadId;
        if (_workThreadId != threadId)
        {
            _workThreadId = threadId;
            _workAvailable = NativeWorkCounters.TryEnableForCurrentThread();
            if (_workAvailable) _lastWork = NativeWorkCounters.ReadForCurrentThread();
            return;
        }
        if (!_workAvailable) return;
        var work = NativeWorkCounters.ReadForCurrentThread();
        static ulong Delta(ulong current, ulong previous) => current >= previous ? current - previous : current;
        Console.WriteLine(
            "[perf-work-thread:{0}] compute={1} oldStates={2} records={3} manifest={4} prepares={5} publications={6} stateFullFallbacks={7} mapCopies={8} contextEntries={9} contextBuilds={10} hitQueries={11} hitBoxes={12} paintOrderClassifications={13}",
            threadId,
            Delta(work.ComputeNodes, _lastWork.ComputeNodes),
            Delta(work.StateEntriesVisited, _lastWork.StateEntriesVisited),
            Delta(work.RevisionRecordsCreated, _lastWork.RevisionRecordsCreated),
            Delta(work.RevisionEntriesCreated, _lastWork.RevisionEntriesCreated),
            Delta(work.RevisionPrepares, _lastWork.RevisionPrepares),
            Delta(work.RevisionPublications, _lastWork.RevisionPublications),
            Delta(work.StateFullFallbacks, _lastWork.StateFullFallbacks),
            Delta(work.MapEntriesCopied, _lastWork.MapEntriesCopied),
            Delta(work.TransitionContextEntriesVisited, _lastWork.TransitionContextEntriesVisited),
            Delta(work.TransitionContextBuilds, _lastWork.TransitionContextBuilds),
            Delta(work.HitQueries, _lastWork.HitQueries),
            Delta(work.HitBoxesVisited, _lastWork.HitBoxesVisited),
            Delta(work.PaintOrderChildrenClassified, _lastWork.PaintOrderChildrenClassified));
        _lastWork = work;
    }

    public PerfReporter(string label)
    {
        _label = label;
    }

    public void Report(UiDocumentHostMetrics metrics)
    {
        lock (_gate)
        {
            var now = DateTime.UtcNow;
            if (!_hasLastMetrics)
            {
                _lastMetrics = metrics;
                _lastReport = now;
                _hasLastMetrics = true;
                ReportWork();
                return;
            }

            var elapsed = now - _lastReport;
            if (elapsed.TotalMilliseconds < 1000)
            {
                return;
            }

            var renderCalls = metrics.RenderCalls - _lastMetrics.RenderCalls;
            var incrementalRenders = metrics.IncrementalRenderCalls - _lastMetrics.IncrementalRenderCalls;
            var styleMs = metrics.StyleResolutionMilliseconds - _lastMetrics.StyleResolutionMilliseconds;
            var styleUpdates = metrics.StyleUpdatedElements - _lastMetrics.StyleUpdatedElements;
            var paintChunks = metrics.PaintChunkBuilds - _lastMetrics.PaintChunkBuilds;
            var visualStyleMapBuilds = metrics.VisualStyleMapBuilds - _lastMetrics.VisualStyleMapBuilds;
            var visualStyleMapUpdates = metrics.VisualStyleMapUpdates - _lastMetrics.VisualStyleMapUpdates;
            var displayListBuilds = metrics.DisplayListBuilds - _lastMetrics.DisplayListBuilds;
            var displayListMs = metrics.DisplayListBuildMilliseconds - _lastMetrics.DisplayListBuildMilliseconds;
            var renderMs = metrics.RenderMilliseconds - _lastMetrics.RenderMilliseconds;

            Console.WriteLine(
                "[perf:{0}] {1:0.0}s renders={2} incremental={3} style={4} styleUpdates={5} styleMs={6:0.###} visualMapBuilds={7} visualMapUpdates={8} layout={9} displayList={10} paintChunks={11} scroll={12} displayListMs={13:0.###} renderMs={14:0.###} avgRenderMs={15:0.###} avgDisplayListMs={16:0.###}",
                _label,
                elapsed.TotalSeconds,
                renderCalls,
                incrementalRenders,
                metrics.StyleResolutions - _lastMetrics.StyleResolutions,
                styleUpdates,
                styleMs,
                visualStyleMapBuilds,
                visualStyleMapUpdates,
                metrics.LayoutBuilds - _lastMetrics.LayoutBuilds,
                displayListBuilds,
                paintChunks,
                metrics.ScrollCompositions - _lastMetrics.ScrollCompositions,
                displayListMs,
                renderMs,
                renderCalls > 0 ? renderMs / renderCalls : 0,
                displayListBuilds > 0 ? displayListMs / displayListBuilds : 0);

            ReportWork();
            _lastMetrics = metrics;
            _lastReport = now;
        }
    }
}
