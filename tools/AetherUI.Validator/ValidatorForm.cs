using System.Runtime.InteropServices;
using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;
using Microsoft.Web.WebView2.WinForms;

namespace AetherUI.Validator;

/// <summary>
/// Right pane (WebView2, the reference renderer) plus the orchestration that spawns and docks the
/// left pane (a real AetherUI native window) to the other half of the screen. Both panes render the
/// same .html file; a FileSystemWatcher reloads both the instant it's saved.
/// </summary>
public sealed class ValidatorForm : Form
{
    private const string AetherWindowTitle = "AetherUI Validator — Native";

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint FindWindow(string? className, string windowTitle);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(nint hWnd, nint hWndInsertAfter, int x, int y, int cx, int cy, uint flags);

    private const uint SwpShowWindow = 0x0040;
    private const uint SwpNoZOrder = 0x0004;

    private readonly string _htmlPath;
    private readonly WebView2 _webView = new();
    private RawHtmlFileComponent? _component;
    private DateTime _lastReload = DateTime.MinValue;
    private nint _aetherWindowHandle;

    public ValidatorForm(string htmlPath)
    {
        _htmlPath = Path.GetFullPath(htmlPath);
        Text = "AetherUI Validator — Web reference (Chromium)";
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
        FormClosed += (_, _) => Environment.Exit(0);
    }

    private async void OnLoad(object? sender, EventArgs e)
    {
        DockToRightHalf();
        StartAetherWindow();

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

    private void StartAetherWindow()
    {
        var thread = new Thread(() =>
        {
            _component = new RawHtmlFileComponent(_htmlPath);
            var options = new UiHostOptions
            {
                Title = AetherWindowTitle,
                Width = 900,
                Height = 900,
                // Neutral mid-gray: content that expects a dark or light page background (which we
                // may not be able to paint yet, e.g. an unsupported gradient) stays legible either
                // way instead of white-on-white or black-on-black hiding it.
                ClearColor = new ColorRgba(0.5f, 0.5f, 0.5f, 1f),
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
            var handle = FindWindow(null, AetherWindowTitle);
            if (handle != nint.Zero)
            {
                _aetherWindowHandle = handle;
                SyncAetherWindow();
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

    /// <summary>Resizes the AetherUI native window to fill everything left of this form's current bounds.</summary>
    private void SyncAetherWindow()
    {
        if (_aetherWindowHandle == nint.Zero)
        {
            return;
        }

        var screen = Screen.FromControl(this).WorkingArea;
        var width = Math.Max(100, Left - screen.X);
        SetWindowPos(_aetherWindowHandle, nint.Zero, screen.X, Top, width, Height, SwpShowWindow | SwpNoZOrder);
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
