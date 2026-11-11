using System.Diagnostics;
using AetherUI.Runtime;

namespace AetherUI.Validator;

/// <summary>Cumulative host work, not presented FPS or GPU timings. Includes first and final samples.</summary>
public sealed class PerfReporter : IDisposable
{
    private readonly object _gate = new();
    private readonly string _label;
    private readonly ValidationReport _report;
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private UiDocumentHostMetrics _latest;
    private bool _hasMetrics, _disposed;
    private long _samples;
    private double _lastReportMs;

    public PerfReporter(string label, ValidationReport report) { _label = label; _report = report; }

    public void Report(UiDocumentHostMetrics metrics)
    {
        lock (_gate)
        {
            if (_disposed) return;
            if (_hasMetrics && (metrics.RenderCalls < _latest.RenderCalls || metrics.LayoutBuilds < _latest.LayoutBuilds
                || metrics.StyleResolutions < _latest.StyleResolutions || metrics.RenderTimestampTicks < _latest.RenderTimestampTicks))
                _report.Write("metrics-reset", new { label = _label, reason = "Host counters decreased; totals start a new host epoch." });
            _latest = metrics;
            _hasMetrics = true;
            _samples++;
            if (_samples == 1 || _clock.Elapsed.TotalMilliseconds - _lastReportMs >= 1000)
            {
                Emit("metrics-snapshot");
                _lastReportMs = _clock.Elapsed.TotalMilliseconds;
            }
        }
    }

    private void Emit(string kind)
    {
        var m = _latest;
        _report.Write(kind, new { label = _label, status = _hasMetrics ? "measured" : "not-run", samples = _samples,
            elapsedMs = _clock.Elapsed.TotalMilliseconds, renders = m.RenderCalls,
            incrementalRenders = m.IncrementalRenderCalls, styleResolutions = m.StyleResolutions,
            styleUpdatedElements = m.StyleUpdatedElements, layoutBuilds = m.LayoutBuilds,
            displayListBuilds = m.DisplayListBuilds, paintChunkBuilds = m.PaintChunkBuilds,
            scrollCompositions = m.ScrollCompositions, visualStyleMapBuilds = m.VisualStyleMapBuilds,
            visualStyleMapUpdates = m.VisualStyleMapUpdates, styleMs = m.StyleResolutionMilliseconds,
            displayListMs = m.DisplayListBuildMilliseconds, renderMs = m.RenderMilliseconds,
            averageRenderMs = m.RenderCalls > 0 ? m.RenderMilliseconds / m.RenderCalls : (double?)null,
            averageDisplayListMs = m.DisplayListBuilds > 0 ? m.DisplayListBuildMilliseconds / m.DisplayListBuilds : (double?)null,
            interpretation = "cumulative CPU host counters; not FPS; render time includes host pipeline work" });
    }

    public void Dispose()
    {
        lock (_gate)
        {
            if (_disposed) return;
            Emit("metrics-final");
            _disposed = true;
        }
    }
}
