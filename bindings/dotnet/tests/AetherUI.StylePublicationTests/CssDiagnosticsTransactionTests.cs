using AetherUI.Css;

internal static class CssDiagnosticsTransactionTests
{
    internal static void Run()
    {
        var prefix = Guid.NewGuid().ToString("N");
        var received = new List<CssStyleDiagnostic>();
        Action<CssStyleDiagnostic> collect = diagnostic => { lock (received) received.Add(diagnostic); };
        CssDiagnostics.Reported += collect;
        try
        {
            using (CssDiagnostics.BeginTransaction())
            {
                CssDiagnostics.ReportUnsupportedValue(prefix, "aborted");
                Check(received.Count == 0, "Staging notified a listener.");
            }
            using (var committed = CssDiagnostics.BeginTransaction())
            {
                CssDiagnostics.ReportUnsupportedValue(prefix, "aborted");
                CssDiagnostics.ReportUnsupportedValue(prefix, "aborted");
                committed.Seal();
                Check(received.Count == 0, "Seal notified a listener.");
                committed.Replay();
            }
            Check(received.Count == 1, "Abort consumed Seen or duplicate staging emitted twice.");

            CssDiagnostics.Transaction first;
            using (var outer = CssDiagnostics.BeginTransaction())
            {
                CssDiagnostics.ReportUnsupportedValue(prefix, "outer-abort");
                using (var inner = CssDiagnostics.BeginTransaction())
                {
                    CssDiagnostics.ReportUnsupportedValue(prefix, "inner");
                    inner.Seal();
                    Action<CssStyleDiagnostic> reenter = d =>
                    {
                        if (d.Property == prefix && d.Value == "inner")
                            CssDiagnostics.ReportUnsupportedValue(prefix, "reentrant");
                    };
                    CssDiagnostics.Reported += reenter;
                    try { inner.Replay(); }
                    finally { CssDiagnostics.Reported -= reenter; }
                }
                CssDiagnostics.ReportUnsupportedValue(prefix, "outer-abort-2");
            }
            Check(received.Count == 3, "Nested replay captured notifications in outer staging.");
            CssDiagnostics.ReportUnsupportedValue(prefix, "outer-abort");
            CssDiagnostics.ReportUnsupportedValue(prefix, "outer-abort-2");
            Check(received.Count == 5, "Nested staging did not restore the outer transaction.");

            first = CssDiagnostics.BeginTransaction();
            CssDiagnostics.ReportUnsupportedValue(prefix, "concurrent");
            first.Seal();
            using var second = CssDiagnostics.BeginTransaction();
            CssDiagnostics.ReportUnsupportedValue(prefix, "concurrent");
            second.Seal();
            using (first)
                Task.WaitAll(Task.Run(first.Replay), Task.Run(second.Replay));
            Check(received.Count == 6, "Concurrent buffers both emitted the same key.");

            // Delimiter-containing strings are distinct triples, not string-concatenated keys.
            CssDiagnostics.ReportUnsupportedValue(prefix + "|x", "y");
            CssDiagnostics.ReportUnsupportedValue(prefix, "x|y");
            Check(received.Count == 8, "Distinct triples collided.");

            var failure = new InvalidOperationException("listener failure");
            Action<CssStyleDiagnostic> fail = d => { if (d.Value == "throw" && d.Property == prefix) throw failure; };
            CssDiagnostics.Reported += fail;
            try
            {
                using var transaction = CssDiagnostics.BeginTransaction();
                CssDiagnostics.ReportUnsupportedValue(prefix, "throw");
                transaction.Seal();
                try { transaction.Replay(); throw new Exception("Replay hid listener failure."); }
                catch (CssDiagnostics.NotificationException error)
                {
                    Check(ReferenceEquals(failure, error.InnerException), "Notification failure lost its cause.");
                }
            }
            finally { CssDiagnostics.Reported -= fail; }
            CssDiagnostics.ReportUnsupportedValue(prefix, "throw");
            Check(received.Count == 9, "Failed notification rolled back Seen.");
            CssDiagnostics.ReportUnsupportedValue(prefix, "after-failure");
            Check(received.Count == 10, "Failed replay leaked staging.");
        }
        finally { CssDiagnostics.Reported -= collect; }
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
