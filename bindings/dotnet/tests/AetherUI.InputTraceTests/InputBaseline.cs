using System.Diagnostics;
using System.Globalization;
using AetherUI.Core.Native;
using AetherUI.Layout;
using AetherUI.Runtime;

// Synthetic fixture only. Outputs aggregate work/allocation statistics, never input positions.
internal static class InputBaseline
{
    public static void Run()
    {
        if (!NativeWorkCounters.TryEnableForCurrentThread())
            throw new InvalidOperationException("Baseline requires native work counters enabled at build time.");
        Console.WriteLine("cards,owner,samples,styleResolutions,styleUpdates,layoutBuilds,pendingProcessed,computeNodes,contextEntries,contextBuilds,mapCopies,hitBoxes,managedAllocatedBytes,gc0,gc1,gc2,wallP50Ms,wallP95Ms,cpuMs,stateEntries,revisionRecords,revisionEntries,publications,visualMapBuilds,visualMapUpdates,displayListBuilds,paintChunks");
        foreach (var count in new[] { 100, 500, 1000 })
        foreach (var owner in new[] { "none", "transform", "backdrop" })
            Measure(count, owner);
    }

    private static void Measure(int count, string owner)
    {
        const int samples = 10;
        using var renderer = new NullRenderer();
        using var host = new UiDocumentHost(new Cards(count, owner), new ApproximateTextMeasurer(), renderer,
            new UiHostOptions { Width = 200, Height = 200, EnableInputWorkCounters = true });
        host.Render();
        for (var i = 0; i < 4; i++) Move(host, i);
        host.ResetMetrics();
        var times = new double[samples];
        using var process = Process.GetCurrentProcess();
        var cpuBefore = process.TotalProcessorTime;
        var gc0 = GC.CollectionCount(0);
        var gc1 = GC.CollectionCount(1);
        var gc2 = GC.CollectionCount(2);
        var workBefore = NativeWorkCounters.ReadForCurrentThread();
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (var i = 0; i < samples; i++)
        {
            var start = Stopwatch.GetTimestamp();
            Move(host, i);
            times[i] = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        }
        var allocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
        var work = NativeWorkCounters.ReadForCurrentThread();
        gc0 = GC.CollectionCount(0) - gc0;
        gc1 = GC.CollectionCount(1) - gc1;
        gc2 = GC.CollectionCount(2) - gc2;
        var cpuMs = (process.TotalProcessorTime - cpuBefore).TotalMilliseconds;
        var metrics = host.Metrics;
        Array.Sort(times);
        if (metrics.StyleResolutions == 0 || work.ComputeNodes == workBefore.ComputeNodes ||
            work.TransitionContextEntriesVisited == workBefore.TransitionContextEntriesVisited)
            throw new InvalidOperationException("Fixture did not exercise the current full host pipeline; review before updating the baseline.");
        if (metrics.StyleUpdatedElements != 0 || metrics.LayoutBuilds != 0)
            throw new InvalidOperationException("The no-op fixture unexpectedly changed styles or rebuilt layout.");
        Console.WriteLine(string.Join(",", new object[]
        {
            count, owner, samples, metrics.StyleResolutions, metrics.StyleUpdatedElements,
            metrics.LayoutBuilds, metrics.PendingMovesProcessed,
            work.ComputeNodes - workBefore.ComputeNodes,
            work.TransitionContextEntriesVisited - workBefore.TransitionContextEntriesVisited,
            work.TransitionContextBuilds - workBefore.TransitionContextBuilds,
            work.MapEntriesCopied - workBefore.MapEntriesCopied,
            work.HitBoxesVisited - workBefore.HitBoxesVisited,
            allocated, gc0, gc1, gc2,
            ((times[4] + times[5]) / 2).ToString("F3", CultureInfo.InvariantCulture),
            times[9].ToString("F3", CultureInfo.InvariantCulture),
            cpuMs.ToString("F3", CultureInfo.InvariantCulture),
            work.StateEntriesVisited - workBefore.StateEntriesVisited,
            work.RevisionRecordsCreated - workBefore.RevisionRecordsCreated,
            work.RevisionEntriesCreated - workBefore.RevisionEntriesCreated,
            work.RevisionPublications - workBefore.RevisionPublications,
            metrics.VisualStyleMapBuilds, metrics.VisualStyleMapUpdates,
            metrics.DisplayListBuilds, metrics.PaintChunkBuilds,
        }));
    }

    private static void Move(UiDocumentHost host, int index)
    {
        host.PointerMoved(new(10, index % 2 == 0 ? 10 : 30));
        host.Render();
    }

    private sealed class Cards(int count, string owner) : UiComponent
    {
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            var declaration = owner switch
            {
                "transform" => "transform:translate(0);",
                "backdrop" => "backdrop-filter:blur(0);",
                _ => "",
            };
            writer.WriteLiteral("<style>body { margin:0 } .card { width:80px;height:20px;" + declaration + " }</style><body>");
            for (var i = 0; i < count; i++) writer.WriteLiteral("<div class='card'></div>");
            writer.WriteLiteral("</body>");
        }
    }
}
