using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;
using AetherUI.Windows.Resources;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace AetherUI.Validator;

/// <summary>Manual native/reference workspace. A reference PNG alone is never a parity verdict.</summary>
public sealed class ValidatorForm : Form
{
    [StructLayout(LayoutKind.Sequential)] private struct NativeRect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern nint FindWindow(string? name, string title);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(nint h, nint after, int x, int y, int w, int height, uint flags);
    [DllImport("user32.dll")] private static extern bool GetClientRect(nint h, out NativeRect rect);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(nint h, out NativeRect rect);
    [DllImport("user32.dll")] private static extern bool PostMessage(nint h, uint message, nint w, nint l);
    [DllImport("user32.dll")] private static extern bool IsWindow(nint h);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint h);

    private readonly ValidatorOptions _options;
    private readonly ValidationReport _report;
    private readonly PerfReporter? _perf;
    private readonly WebView2 _webView = new() { Dock = DockStyle.Fill };
    private readonly System.Windows.Forms.Timer _reloadTimer = new() { Interval = 250 };
    private readonly System.Windows.Forms.Timer _windowTimer = new() { Interval = 150 };
    private readonly CancellationTokenSource _lifetime = new();
    private readonly SemaphoreSlim _reloadGate = new(1, 1);
    private readonly TaskCompletionSource<Control> _dispatcher = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly string _nativeTitle = $"AetherUI Validator Native {Guid.NewGuid():N}";
    private FileSystemWatcher? _watcher;
    private Thread? _nativeThread;
    private RawHtmlFileComponent? _component;
    private nint _nativeHandle;
    private int _generation, _windowAttempts;
    private ulong _navigationId;
    private volatile bool _closing;
    private string _sourceHash = "";
    // Written on the native STA after applying a snapshot; read by the WinForms STA.
    private string _nativeHash = "";
    public int ExitCode { get; private set; }

    public ValidatorForm(ValidatorOptions options, ValidationReport report, PerfReporter? perf)
    {
        _options = options;
        _report = report;
        _perf = perf;
        Text = $"AetherUI Validator - Chromium - {Path.GetFileName(options.HtmlPath)}";
        StartPosition = FormStartPosition.Manual;
        Controls.Add(_webView);
        _reloadTimer.Tick += async (_, _) => { _reloadTimer.Stop(); await ReloadAsync(); };
        _windowTimer.Tick += (_, _) => MonitorNativeWindow();
        Move += (_, _) => SyncNativeViewport();
        Resize += (_, _) => SyncNativeViewport();
        Load += OnLoad;
        FormClosed += (_, _) => Stop();
    }

    private async void OnLoad(object? sender, EventArgs e)
    {
        try
        {
            var area = Screen.FromControl(this).WorkingArea;
            Bounds = new Rectangle(area.Left + area.Width / 2, area.Top, area.Width / 2, area.Height);
            if (_options.Width is int width && _options.Height is int height) ClientSize = new Size(width, height);
            var snapshot = await ReadStableAsync(_lifetime.Token);
            _sourceHash = snapshot.Hash;
            Volatile.Write(ref _nativeHash, snapshot.Hash);
            StartNativeWindow(snapshot.Markup);
            await _webView.EnsureCoreWebView2Async();
            if (_closing) return;
            _webView.ZoomFactor = 1;
            _webView.CoreWebView2.NavigationStarting += (_, args) => _navigationId = args.NavigationId;
            _webView.CoreWebView2.NavigationCompleted += OnNavigationCompleted;
            _report.Write("browser", new { version = _webView.CoreWebView2.Environment.BrowserVersionString });
            StartWatcher();
            Navigate();
            // Reconcile edits between the initial snapshot and watcher initialization.
            ScheduleReload();
        }
        catch (OperationCanceledException) when (_closing) { }
        catch (Exception ex) { Fail("startup", ex); }
    }

    private void StartWatcher()
    {
        _watcher = new FileSystemWatcher(Path.GetDirectoryName(_options.HtmlPath!)!, "*")
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName,
        };
        _watcher.Changed += OnFileChanged;
        _watcher.Created += OnFileChanged;
        _watcher.Deleted += OnFileChanged;
        _watcher.Renamed += OnRenamed;
        _watcher.Error += (_, e) => PostToUi(() =>
        {
            _report.Write("watcher-error", new { message = e.GetException().Message });
            ScheduleReload(); // Overflow may have lost the relevant change.
        });
        _watcher.EnableRaisingEvents = true;
    }

    private bool IsTarget(string path) => string.Equals(Path.GetFullPath(path), _options.HtmlPath, StringComparison.OrdinalIgnoreCase);
    private void OnFileChanged(object sender, FileSystemEventArgs e) { if (IsTarget(e.FullPath)) PostToUi(ScheduleReload); }
    private void OnRenamed(object sender, RenamedEventArgs e)
    { if (IsTarget(e.FullPath) || IsTarget(e.OldFullPath)) PostToUi(ScheduleReload); }
    private void ScheduleReload()
    {
        if (_closing) return;
        ++_generation;
        _reloadTimer.Stop();
        _reloadTimer.Start(); // Trailing-edge debounce, only on this UI thread.
    }
    private void PostToUi(Action action)
    {
        if (_closing || !IsHandleCreated || IsDisposed) return;
        try { BeginInvoke(action); }
        catch (InvalidOperationException) when (_closing || IsDisposed || !IsHandleCreated) { }
    }

    private async Task<(string Markup, string Hash)> ReadStableAsync(CancellationToken token)
    {
        Exception? last = null;
        for (int attempt = 0; attempt < 8; attempt++)
        {
            token.ThrowIfCancellationRequested();
            try
            {
                var first = await File.ReadAllBytesAsync(_options.HtmlPath!, token);
                await Task.Delay(75, token);
                var second = await File.ReadAllBytesAsync(_options.HtmlPath!, token);
                if (first.AsSpan().SequenceEqual(second))
                {
                    using var reader = new StreamReader(new MemoryStream(second), detectEncodingFromByteOrderMarks: true);
                    return (await reader.ReadToEndAsync(token), Convert.ToHexString(SHA256.HashData(second)).ToLowerInvariant());
                }
                last = new IOException("HTML changed while the snapshot was being read.");
            }
            catch (IOException ex) { last = ex; }
            await Task.Delay(100, token);
        }
        throw new IOException("Could not read a stable HTML snapshot; last good frame was retained.", last);
    }

    private async Task ReloadAsync()
    {
        var generation = _generation;
        var entered = false;
        try
        {
            await _reloadGate.WaitAsync(_lifetime.Token);
            entered = true;
            if (_closing || generation != _generation) return;
            var snapshot = await ReadStableAsync(_lifetime.Token);
            if (_closing || generation != _generation) return;
            if (snapshot.Hash == _sourceHash && snapshot.Hash == Volatile.Read(ref _nativeHash)) return;
            var dispatcher = await _dispatcher.Task.WaitAsync(TimeSpan.FromSeconds(10), _lifetime.Token);
            if (_closing || generation != _generation) return;
            using var dispatchLifetime = CancellationTokenSource.CreateLinkedTokenSource(_lifetime.Token);
            var dispatchToken = dispatchLifetime.Token;
            var updated = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            // Unknown until the native STA confirms application. This also blocks capture while
            // a dispatched update is pending or has failed after mutating component state.
            Volatile.Write(ref _nativeHash, "");
            try
            {
                dispatcher.BeginInvoke(new Action(() =>
                {
                    if (dispatchToken.IsCancellationRequested || _closing || generation != Volatile.Read(ref _generation))
                    { updated.TrySetResult(false); return; }
                    try
                    {
                        _component!.Reload(snapshot.Markup);
                        // Publish the applied state even if a later file generation is now pending.
                        Volatile.Write(ref _nativeHash, snapshot.Hash);
                        updated.TrySetResult(true);
                    }
                    catch (Exception ex) { updated.TrySetException(ex); }
                }));
                if (!await updated.Task.WaitAsync(TimeSpan.FromSeconds(10), _lifetime.Token)) return;
            }
            finally { dispatchLifetime.Cancel(); } // Suppress a queued callback after timeout/close.
            if (_closing || generation != _generation) return;
            _sourceHash = snapshot.Hash;
            Navigate();
            _report.Write("reload", new { status = "requested", generation, sha256 = _sourceHash });
        }
        catch (OperationCanceledException) when (_closing) { }
        catch (Exception ex) { if (!_closing) _report.Write("reload-failed", new { message = ex.Message, generation }); }
        finally { if (entered) _reloadGate.Release(); }
    }

    private void Navigate()
    {
        _report.Write("source", new { file = Path.GetFileName(_options.HtmlPath), sha256 = _sourceHash,
            note = "Native snapshot; browser file navigation hash rechecked before reference capture." });
        _webView.CoreWebView2.Navigate(new Uri(_options.HtmlPath!).AbsoluteUri);
    }

    private void StartNativeWindow(string markup)
    {
        _nativeThread = new Thread(() =>
        {
            try
            {
                using var dispatcher = new Control();
                _ = dispatcher.Handle; // WinForms HWND created on native STA; DispatchMessage services BeginInvoke.
                _component = new RawHtmlFileComponent(markup);
                using var http = new HttpImageResourceLoader();
                using var images = new ImageResourceResolver(new CompositeImageResourceLoader(
                    http, new DataImageResourceLoader(), new FileImageResourceLoader()));
                _dispatcher.TrySetResult(dispatcher);
                if (_closing) { _component.Dispose(); return; }
                WindowsUiApplication.Run(() => _component, new UiHostOptions
                {
                    Title = _nativeTitle, Width = _options.Width ?? 640, Height = _options.Height ?? 640,
                    MinimumWidth = 100, MinimumHeight = 100,
                    ClearColor = new ColorRgba(0.5f, 0.5f, 0.5f, 1f), ImageResolver = images,
                    MetricsSink = _perf is null ? null : _perf.Report,
                });
            }
            catch (Exception ex)
            {
                _dispatcher.TrySetException(ex);
                PostToUi(() => Fail("native-host", ex));
            }
        }) { IsBackground = true, Name = "AetherUI native validator" };
        _nativeThread.SetApartmentState(ApartmentState.STA);
        _nativeThread.Start();
        _windowTimer.Start();
    }

    private void MonitorNativeWindow()
    {
        if (_nativeHandle == nint.Zero)
        {
            _nativeHandle = FindWindow(null, _nativeTitle);
            if (_nativeHandle != nint.Zero) { SyncNativeViewport(); return; }
            if (++_windowAttempts >= 100) Fail("native-window", new TimeoutException("Native window was not ready within 15 seconds."));
        }
        else if (!IsWindow(_nativeHandle)) Close();
    }

    private void SyncNativeViewport()
    {
        if (_nativeHandle == nint.Zero || WindowState == FormWindowState.Minimized) return;
        if (!GetWindowRect(_nativeHandle, out var outer) || !GetClientRect(_nativeHandle, out var client)) return;
        int width = _webView.ClientSize.Width + (outer.Right - outer.Left) - (client.Right - client.Left);
        int height = _webView.ClientSize.Height + (outer.Bottom - outer.Top) - (client.Bottom - client.Top);
        SetWindowPos(_nativeHandle, nint.Zero, Left - width, Top, width, height, 0x0044);
    }

    private async void OnNavigationCompleted(object? sender, CoreWebView2NavigationCompletedEventArgs e)
    {
        var generation = _generation;
        var expectedHash = _sourceHash;
        bool IsCurrent() => !_closing && generation == _generation && e.NavigationId == _navigationId
            && expectedHash == _sourceHash && expectedHash == Volatile.Read(ref _nativeHash)
            && _webView.Source is { IsFile: true } uri
            && string.Equals(uri.LocalPath, _options.HtmlPath, StringComparison.OrdinalIgnoreCase);
        if (!IsCurrent()) return;
        try
        {
            if (!e.IsSuccess) throw new IOException($"Reference navigation failed: {e.WebErrorStatus}");
            _report.Write("navigation", new { status = "loaded", generation });
            if (!_options.CaptureReference) return;
            var timeout = Stopwatch.StartNew();
            while (IsCurrent())
            {
                var ready = await _webView.CoreWebView2.ExecuteScriptAsync(
                    "document.readyState==='complete' && document.fonts.status==='loaded' && Array.from(document.images).every(i=>i.complete)");
                if (ready == "true") break;
                if (timeout.Elapsed.TotalSeconds > 30) throw new TimeoutException("Reference resources did not settle in 30 seconds.");
                await Task.Delay(100, _lifetime.Token);
            }
            await Task.Delay(_options.SettleMilliseconds, _lifetime.Token);
            if (!IsCurrent()) return;
            var source = await ReadStableAsync(_lifetime.Token);
            if (!IsCurrent()) return;
            if (source.Hash != expectedHash) { ScheduleReload(); return; }
            var viewport = await _webView.CoreWebView2.ExecuteScriptAsync(
                "JSON.stringify({width:innerWidth,height:innerHeight,dpr:devicePixelRatio,scrollX,scrollY," +
                "brokenImages:Array.from(document.images).filter(i=>!i.naturalWidth).length," +
                "activeAnimations:document.getAnimations().filter(a=>a.playState==='running').length})");
            if (!IsCurrent()) return;
            GetClientRect(_nativeHandle, out var client);
            _report.Write("viewport", new { browser = JsonSerializer.Deserialize<string>(viewport),
                browserPhysicalWidth = _webView.ClientSize.Width, browserPhysicalHeight = _webView.ClientSize.Height,
                nativePhysicalWidth = client.Right - client.Left, nativePhysicalHeight = client.Bottom - client.Top,
                nativeDpi = _nativeHandle == nint.Zero ? 0 : GetDpiForWindow(_nativeHandle), browserDpi = DeviceDpi,
                parity = "not-run", scope = "current viewport only; document below viewport is not captured" });
            var name = $"reference-{generation:D4}-{e.NavigationId}.png";
            var finalPath = Path.Combine(_report.DirectoryPath, name);
            var pendingPath = finalPath + ".pending";
            try
            {
                await using (var png = File.Create(pendingPath))
                    await _webView.CoreWebView2.CapturePreviewAsync(CoreWebView2CapturePreviewImageFormat.Png, png);
                if (!IsCurrent()) return;
                var after = await ReadStableAsync(_lifetime.Token);
                if (!IsCurrent()) return;
                if (after.Hash != expectedHash) { ScheduleReload(); return; }
                File.Move(pendingPath, finalPath, overwrite: true);
                _report.Write("reference-capture", new { file = name, sha256 = expectedHash,
                    status = "captured-reference-only", nativeCapture = "not-implemented", visualParity = "not-run" });
            }
            finally
            {
                if (File.Exists(pendingPath)) File.Delete(pendingPath);
            }
        }
        catch (OperationCanceledException) when (_closing) { }
        catch (Exception ex) { if (IsCurrent()) Fail("reference-capture", ex); }
    }

    private void Fail(string stage, Exception ex)
    {
        ExitCode = 1;
        _report.Write("error", new { stage, type = ex.GetType().Name, message = ex.Message });
        if (!_closing) MessageBox.Show(this, ex.Message, "Validator failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
        Close();
    }

    private void Stop()
    {
        if (_closing) return;
        _closing = true;
        _lifetime.Cancel();
        _watcher?.Dispose();
        _reloadTimer.Dispose();
        _windowTimer.Dispose();
        var wait = Stopwatch.StartNew();
        while (_nativeThread?.IsAlive == true && wait.ElapsedMilliseconds < 3000)
        {
            var handle = _nativeHandle != nint.Zero ? _nativeHandle : FindWindow(null, _nativeTitle);
            if (handle != nint.Zero) PostMessage(handle, 0x0010, nint.Zero, nint.Zero);
            _nativeThread.Join(50);
        }
        if (_nativeThread?.IsAlive == true)
        {
            ExitCode = 1;
            _report.Write("shutdown-timeout", new { message = "Native thread did not stop; final metrics may be incomplete." });
        }
        _perf?.Dispose(); // Flush final snapshot after native thread termination.
    }
}
