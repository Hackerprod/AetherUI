using System.Runtime.InteropServices;
using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;
using AetherUI.Windows.Resources;
using Microsoft.Web.WebView2.WinForms;

namespace AetherUI.Validator;

/// <summary>
/// Right pane (WebView2, the reference renderer) plus the orchestration that spawns and docks the
/// left pane (a real AetherUI native window) to the other half of the screen. Both panes render the
/// same .html file; a FileSystemWatcher reloads both the instant it's saved.
/// </summary>
public sealed class ValidatorForm : Form
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint FindWindow(string? className, string windowTitle);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(nint hWnd, nint hWndInsertAfter, int x, int y, int cx, int cy, uint flags);

    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int index);

    [DllImport("user32.dll")]
    private static extern bool PostMessage(nint hWnd, uint message, nint wParam, nint lParam);

    [DllImport("user32.dll")]
    private static extern bool IsWindow(nint hWnd);

    private const uint SwpShowWindow = 0x0040;
    private const uint SwpNoZOrder = 0x0004;
    private const uint WmClose = 0x0010;
    private const int SmCxSizeFrame = 32;
    private const int SmCxPaddedBorder = 92;

    private readonly string _htmlPath;
    private readonly string _aetherWindowTitle;
    private readonly PerfReporter? _perfReporter;
    private readonly WebView2 _webView = new();
    private RawHtmlFileComponent? _component;
    private DateTime _lastReload = DateTime.MinValue;
    private nint _aetherWindowHandle;
    private System.Windows.Forms.Timer? _nativeWindowMonitor;

    public ValidatorForm(string htmlPath, PerfReporter? perfReporter = null)
    {
        _htmlPath = Path.GetFullPath(htmlPath);
        _perfReporter = perfReporter;
        var pageName = Path.GetFileNameWithoutExtension(_htmlPath);
        _aetherWindowTitle = $"AetherUI Validator — Native — {pageName} — {Guid.NewGuid():N}";
        Text = $"AetherUI Validator — Web reference (Chromium) — {pageName}";
        StartPosition = FormStartPosition.Manual;

        _webView.Dock = DockStyle.Fill;
        Controls.Add(_webView);

        var watcher = new FileSystemWatcher(Path.GetDirectoryName(_htmlPath)!, Path.GetFileName(_htmlPath))
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size,
            EnableRaisingEvents = true,
        };
        watcher.Changed += OnFileChanged;
        Disposed += (_, _) => watcher.Dispose();

        // Keep the AetherUI pane glued to "everything left of this form" continuously, so the
        // pair behaves like one resizable/maximizable split window instead of two static ones.
        Move += (_, _) => SyncAetherWindow();
        Resize += (_, _) => SyncAetherWindow();

        Load += OnLoad;
        FormClosed += (_, _) =>
        {
            StopNativeWindowMonitor();
            CloseAetherWindow();
        };
    }

    private async void OnLoad(object? sender, EventArgs e)
    {
        DockToRightHalf();
        StartAetherWindow(GetAetherWindowBounds());

        await _webView.EnsureCoreWebView2Async();
        NavigateWebView();
    }

    private void NavigateWebView() => _webView.CoreWebView2.Navigate(new Uri(_htmlPath).AbsoluteUri);

    private void DockToRightHalf()
    {
        var bounds = Screen.PrimaryScreen!.WorkingArea;
        var half = bounds.Width / 2;
        Bounds = new Rectangle(bounds.X + half, bounds.Y, bounds.Width - half, bounds.Height);
    }

    private void StartAetherWindow(Rectangle initialBounds)
    {
        var thread = new Thread(() =>
        {
            _component = new RawHtmlFileComponent(_htmlPath);
            using var httpLoader = new HttpImageResourceLoader();
            var fileLoader = new FileImageResourceLoader();
            var imageLoader = new CompositeImageResourceLoader(httpLoader, new DataImageResourceLoader(), fileLoader);
            using var imageResolver = new ImageResourceResolver(imageLoader);
            var options = new UiHostOptions
            {
                Title = _aetherWindowTitle,
                InitialWindowBounds = new WindowBounds(
                    initialBounds.X,
                    initialBounds.Y,
                    initialBounds.Width,
                    initialBounds.Height),
                // Neutral mid-gray: content that expects a dark or light page background (which we
                // may not be able to paint yet, e.g. an unsupported gradient) stays legible either
                // way instead of white-on-white or black-on-black hiding it.
                ClearColor = new ColorRgba(0.5f, 0.5f, 0.5f, 1f),
                ImageResolver = imageResolver,
                EnableInputWorkCounters = Environment.GetEnvironmentVariable("AETHERUI_WORK_COUNTERS") == "1",
                MetricsSink = _perfReporter is null ? null : _perfReporter.Report,
            };
            WindowsUiApplication.Run(() => _component, options);
        })
        {
            IsBackground = true,
        };
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();

        var timer = new System.Windows.Forms.Timer { Interval = 150 };
        var attempts = 0;
        timer.Tick += (_, _) =>
        {
            attempts++;
            var handle = FindWindow(null, _aetherWindowTitle);
            if (handle != nint.Zero)
            {
                _aetherWindowHandle = handle;
                SyncAetherWindow();
                StartNativeWindowMonitor();
                timer.Stop();
                timer.Dispose();
            }
            else if (attempts > 60)
            {
                timer.Stop();
                timer.Dispose();
            }
        };
        timer.Start();
    }

    /// <summary>Computes the native pane's physical outer bounds from the current split geometry.</summary>
    private Rectangle GetAetherWindowBounds()
    {
        var screen = Screen.FromControl(this).WorkingArea;
        // Both this Form and the AetherUI window use a resizable (WS_THICKFRAME) frame, which on
        // Windows 10/11 carries an invisible hit-test border outside the visible edge (accounted for
        // in GetWindowRect/SetWindowPos coordinates but not actually painted). Docked edge-to-edge at
        // face value, each window's invisible border leaves a sliver of bare desktop between them.
        // Overlapping by both windows' frame padding closes that gap; the overlap only ever covers
        // dead hit-test space, never real content.
        var framePadding = GetSystemMetrics(SmCxSizeFrame) + GetSystemMetrics(SmCxPaddedBorder);
        var width = Math.Max(100, Left - screen.X) + framePadding * 2;
        return new Rectangle(screen.X, screen.Y, width, screen.Height);
    }

    /// <summary>Resizes the AetherUI native window to fill everything left of this form's current bounds.</summary>
    private void SyncAetherWindow()
    {
        if (_aetherWindowHandle == nint.Zero)
        {
            return;
        }

        var bounds = GetAetherWindowBounds();
        SetWindowPos(_aetherWindowHandle, nint.Zero, bounds.X, bounds.Y, bounds.Width, bounds.Height, SwpShowWindow | SwpNoZOrder);
    }

    private void CloseAetherWindow()
    {
        if (_aetherWindowHandle != nint.Zero)
        {
            PostMessage(_aetherWindowHandle, WmClose, nint.Zero, nint.Zero);
            _aetherWindowHandle = nint.Zero;
        }
    }

    private void StartNativeWindowMonitor()
    {
        _nativeWindowMonitor ??= new System.Windows.Forms.Timer { Interval = 250 };
        _nativeWindowMonitor.Tick -= OnNativeWindowMonitorTick;
        _nativeWindowMonitor.Tick += OnNativeWindowMonitorTick;
        _nativeWindowMonitor.Start();
    }

    private void StopNativeWindowMonitor()
    {
        if (_nativeWindowMonitor is null)
        {
            return;
        }

        _nativeWindowMonitor.Stop();
        _nativeWindowMonitor.Tick -= OnNativeWindowMonitorTick;
        _nativeWindowMonitor.Dispose();
        _nativeWindowMonitor = null;
    }

    private void OnNativeWindowMonitorTick(object? sender, EventArgs e)
    {
        if (_aetherWindowHandle != nint.Zero && !IsWindow(_aetherWindowHandle))
        {
            _aetherWindowHandle = nint.Zero;
            Close();
        }
    }

    private void OnFileChanged(object sender, FileSystemEventArgs e)
    {
        var now = DateTime.UtcNow;
        if ((now - _lastReload).TotalMilliseconds < 200)
        {
            return; // FileSystemWatcher fires more than once per save; debounce.
        }

        _lastReload = now;

        if (InvokeRequired)
        {
            BeginInvoke(new Action(() => OnFileChanged(sender, e)));
            return;
        }

        _webView.CoreWebView2?.Reload();
        _component?.Reload();
    }
}
