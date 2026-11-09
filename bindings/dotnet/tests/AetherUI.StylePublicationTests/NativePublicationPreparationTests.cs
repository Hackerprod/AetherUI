using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;

internal static class NativePublicationPreparationTests
{
    private static int _retry, _busy, _concurrent, _mode, _resolutions;
    private static bool _reentryRejected;
    private static IntPtr _library;
    private static readonly OutOfMemoryException Failure = new("prelink resolver failure between imports");
    private static readonly ManualResetEventSlim Entered = new(false), Release = new(false);

    internal static void Run(bool requireCommitCounter = false)
    {
        _library = NativeLibrary.Load("aether_core", typeof(NativeDomDocument).Assembly, null);
        // This resolver belongs only to this test assembly, never the product.
        NativeLibrary.SetDllImportResolver(typeof(NativePublicationPreparationTests).Assembly, Resolve);
        using var document = NativeDomDocument.Parse("<div></div>");
        var token = NativeDomSync.ReadToken(document);
        var countersAvailable = NativeDomWorkCounters.TryEnableForCurrentThread();
        Check(!requireCommitCounter || countersAvailable, "required commit counter unavailable");
        if (countersAvailable) NativeDomWorkCounters.ResetForCurrentThread();
        try
        {
            _mode = 1; _resolutions = 0;
            var failed = false;
            try { NativePublicationImports.EnsurePrepared(ref _retry, typeof(RetryImports), 2); }
            catch (OutOfMemoryException error) when (ReferenceEquals(error, Failure)) { failed = true; }
            Check(failed && _retry == 0 && _resolutions == 2, "partial link failure did not reset for retry");
            _mode = 0;
            NativePublicationImports.EnsurePrepared(ref _retry, typeof(RetryImports), 2);
            Check(_retry == 2, "explicit retry did not become ready");

            _mode = 2;
            NativePublicationImports.EnsurePrepared(ref _busy, typeof(ReentrantImports), 1);
            Check(_reentryRejected && _busy == 2, "reentrant call changed initializer authority");

            _mode = 3; _resolutions = 0;
            Exception? workerError = null;
            var worker = new Thread(() =>
            {
                try { NativePublicationImports.EnsurePrepared(ref _concurrent, typeof(ConcurrentImports), 1); }
                catch (Exception error) { workerError = error; }
            });
            worker.IsBackground = true; worker.Start();
            try
            {
                Check(Entered.Wait(TimeSpan.FromSeconds(10)), "initializer never reached resolver");
                var rejected = false;
                try { NativePublicationImports.EnsurePrepared(ref _concurrent, typeof(ConcurrentImports), 1); }
                catch (InvalidOperationException) { rejected = true; }
                Check(rejected && _concurrent == 1, "concurrent caller waited or reset the owning initializer");
            }
            finally { Release.Set(); }
            Check(worker.Join(TimeSpan.FromSeconds(10)), "initializer deadlocked");
            if (workerError is not null) throw workerError;
            Check(_concurrent == 2 && _resolutions == 1, "concurrent preparation did not publish exactly once");
            NativePublicationImports.EnsurePrepared(ref _concurrent, typeof(ConcurrentImports), 1);
            Check(_resolutions == 1, "ready fast path relinked");

            _mode = 0;
            var mismatch = 0; var mismatched = false;
            try { NativePublicationImports.EnsurePrepared(ref mismatch, typeof(RetryImports), 3); }
            catch (InvalidOperationException) { mismatched = true; }
            Check(mismatched && mismatch == 0, "metadata mismatch reported ready");
            var missing = 0; var unavailable = false;
            try { NativePublicationImports.EnsurePrepared(ref missing, typeof(MissingExport), 1); }
            catch (EntryPointNotFoundException) { unavailable = true; }
            Check(unavailable && missing == 0, "unavailable export reported ready");
            Check(NativeDomSync.ReadToken(document) == token, "prelink modified native inputs");
            if (countersAvailable)
                Check(NativeDomWorkCounters.ReadForCurrentThread().DomEditsCommitted == 0, "prelink invoked a dummy commit");
        }
        finally { if (countersAvailable) NativeDomWorkCounters.DisableForCurrentThread(); }
        Console.WriteLine("PASS publication import preparation: partial failure/retry, reentrant/concurrent busy, single readiness, missing metadata/export; native commit counter " + (countersAvailable ? "measured zero" : "UNAVAILABLE"));
    }
    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? search)
    {
        if (name != "aether-publication-preparation-test") return IntPtr.Zero;
        var calls = Interlocked.Increment(ref _resolutions);
        if (_mode == 1 && calls == 2) throw Failure;
        if (_mode == 2)
        {
            try { NativePublicationImports.EnsurePrepared(ref _busy, typeof(ReentrantImports), 1); }
            catch (InvalidOperationException) { _reentryRejected = _busy == 1; }
        }
        if (_mode == 3)
        {
            Entered.Set();
            if (!Release.Wait(TimeSpan.FromSeconds(10))) throw new TimeoutException("test resolver was not released");
        }
        return _library;
    }
    private static class RetryImports
    {
        [DllImport("aether-publication-preparation-test", EntryPoint = "aether_dom_edit_validate_v1")]
        internal static extern int Validate(IntPtr edit);
        [DllImport("aether-publication-preparation-test", EntryPoint = "aether_dom_edit_commit_v1")]
        internal static extern int Commit(IntPtr edit, IntPtr token);
    }
    private static class ReentrantImports
    {
        [DllImport("aether-publication-preparation-test", EntryPoint = "aether_dom_edit_validate_v1")]
        internal static extern int Validate(IntPtr edit);
    }
    private static class ConcurrentImports
    {
        [DllImport("aether-publication-preparation-test", EntryPoint = "aether_dom_edit_validate_v1")]
        internal static extern int Validate(IntPtr edit);
    }
    private static class MissingExport
    {
        [DllImport("aether-publication-preparation-test", EntryPoint = "aether_test_missing_publication_export")]
        internal static extern int Validate(IntPtr edit);
    }
    private static void Check(bool condition, string message)
    { if (!condition) throw new InvalidOperationException("Publication preparation test: " + message); }
}
