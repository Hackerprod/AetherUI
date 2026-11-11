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
    private readonly double? _initialCpuMs;

    public PerfReporter(string label, ValidationReport report)
    {
        _label = label;
        _report = report;
        _initialCpuMs = ReadProcessResources().CpuTotalMs;
    }

    private sealed record ProcessResources(string Status, double? CpuTotalMs, long? WorkingSetBytes,
        long? PeakWorkingSetBytes, long? PrivateMemoryBytes, long? ManagedHeapBytes);

    private static ProcessResources ReadProcessResources()
    {
        try
        {
            using var process = Process.GetCurrentProcess();
            process.Refresh();
            return new("measured", process.TotalProcessorTime.TotalMilliseconds, process.WorkingSet64,
                process.PeakWorkingSet64, process.PrivateMemorySize64, GC.GetTotalMemory(false));
        }
        catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception
            or NotSupportedException)
        {
            return new("unavailable", null, null, null, null, null);
        }
    }

    public void Report(UiDocumentHostMetrics metrics)
    {
        lock (_gate)
        {
            if (_disposed) return;
            if (_hasMetrics && (metrics.RenderCalls < _latest.RenderCalls || metrics.LayoutBuilds < _latest.LayoutBuilds
                || metrics.StyleResolutions < _latest.StyleResolutions || metrics.RenderTimestampTicks < _latest.RenderTimestampTicks
                || metrics.LayoutInteropTimestampTicks < _latest.LayoutInteropTimestampTicks
                || metrics.LayoutProjectionTimestampTicks < _latest.LayoutProjectionTimestampTicks
                || metrics.LayoutProjections < _latest.LayoutProjections))
                _report.Write("metrics-reset", new { label = _label, reason = "Host counters decreased; observable reset only, not an exhaustive epoch detector." });
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
        var resources = ReadProcessResources();
        var elapsedMs = _clock.Elapsed.TotalMilliseconds;
        var cpuMs = resources.CpuTotalMs is double current && _initialCpuMs is double initial
            ? Math.Max(0, current - initial) : (double?)null;
        _report.Write(kind, new { label = _label, status = _hasMetrics ? "measured" : "not-run", samples = _samples,
            elapsedMs, process = resources, processCpuMsSinceReporterStart = cpuMs,
            processAverageCoreEquivalent = elapsedMs > 0 && cpuMs.HasValue ? cpuMs / elapsedMs : null,
            processScope = "validator process including native STA; excludes WebView2 child processes and GPU memory",
            timingSchema = "host-phases-v2",
            timingScope = new
            {
                accumulation = "cumulative partial wall-time regions; not an exhaustive decomposition or a sum-to-total contract",
                layoutInteropMs = "CreateNativeLayout wrapper/PInvoke/native algorithm and text/scroll callbacks; excludes prior style map, projection, reporting and drawing",
                layoutProjectionMs = "Project native reads, marshaling, conversion and managed allocation; excludes subsequent scroll index and drawing",
                styleMs = "ResolveStyles preparation, native calls, managed style conversion and map updates; not pure engine time",
                displayListMs = "RenderingInterop.Build including managed readback/decoding and AttachResolvedImages",
                renderMs = "backend TryRenderIncremental or Render after EnsureVisualTree; excludes parse/style/layout and is not presentation or GPU time",
                completion = "operations that returned without propagating; callbacks may catch errors; measured means samples, not run success",
                reset = "individual reads/resets are not atomic together; use serialized host access; only observed decrements emit metrics-reset",
                unmeasured = "DOM parse/projection, CSS loading, layout style-map preparation, scroll index, scheduling, first presentation, callback subphases, reporting and captures"
            },
            layoutInteropMs = m.LayoutInteropMilliseconds,
            layoutProjectionMs = m.LayoutProjectionMilliseconds,
            layoutProjections = m.LayoutProjections,
            renders = m.RenderCalls,
            incrementalRenders = m.IncrementalRenderCalls, styleResolutions = m.StyleResolutions,
            styleUpdatedElements = m.StyleUpdatedElements, layoutBuilds = m.LayoutBuilds,
            displayListBuilds = m.DisplayListBuilds, paintChunkBuilds = m.PaintChunkBuilds,
            scrollCompositions = m.ScrollCompositions, visualStyleMapBuilds = m.VisualStyleMapBuilds,
            visualStyleMapUpdates = m.VisualStyleMapUpdates, styleMs = m.StyleResolutionMilliseconds,
            displayListMs = m.DisplayListBuildMilliseconds, renderMs = m.RenderMilliseconds,
            averageRenderMs = m.RenderCalls > 0 ? m.RenderMilliseconds / m.RenderCalls : (double?)null,
            averageDisplayListMs = m.DisplayListBuilds > 0 ? m.DisplayListBuildMilliseconds / m.DisplayListBuilds : (double?)null,
            interpretation = "cumulative host operation counters and elapsed pipeline timings; process CPU reported separately; not FPS" });
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
