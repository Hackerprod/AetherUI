using AetherUI.Runtime;

namespace AetherUI.Validator;

public sealed class PerfReporter
{
    private readonly object _gate = new();
    private readonly string _label;
    private DateTime _lastReport = DateTime.UtcNow;
    private UiDocumentHostMetrics _lastMetrics;
    private bool _hasLastMetrics;

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

            _lastMetrics = metrics;
            _lastReport = now;
        }
    }
}
