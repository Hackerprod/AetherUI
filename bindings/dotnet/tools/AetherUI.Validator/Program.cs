using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;

namespace AetherUI.Validator;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        ValidationReport? report = null;
        try
        {
            var options = ValidatorOptions.Parse(args);
            Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            if (options.Help)
            {
                MessageBox.Show("AetherUI.Validator [--page file.html | --modal] [--perf] [--output directory] " +
                    "[--viewport WIDTHxHEIGHT] [--capture-reference] [--settle-ms 500]\n" +
                    "Capture is Chromium reference only; no native parity verdict is implied.", "Validator help");
                return 0;
            }
            report = new ValidationReport(options.OutputRoot);
            using var perfReporter = options.Perf ? new PerfReporter("native", report) : null;
            if (options.Modal)
                return WindowsUiApplication.Run(() => new ModalComponent(), new UiHostOptions
                {
                    Title = ModalComponent.WindowTitle, Width = options.Width ?? 800, Height = options.Height ?? 640,
                    ClearColor = new ColorRgba(0.09f, 0.10f, 0.14f, 1f),
                    MetricsSink = perfReporter is null ? null : perfReporter.Report,
                });
            if (options.HtmlPath is null)
            {
                var catalog = new PageCatalogComponent(Path.Combine(AppContext.BaseDirectory, "Pages"), options, report);
                return WindowsUiApplication.Run(() => catalog, new UiHostOptions
                {
                    Title = catalog.InstanceWindowTitle, Width = 920, Height = 760,
                    ClearColor = new ColorRgba(0.035f, 0.063f, 0.114f, 1f),
                    MetricsSink = perfReporter is null ? null : perfReporter.Report,
                });
            }
            using var form = new ValidatorForm(options, report, perfReporter);
            Application.Run(form);
            return form.ExitCode;
        }
        catch (Exception ex)
        {
            report?.Write("error", new { type = ex.GetType().Name, message = ex.Message });
            MessageBox.Show(ex.Message, "Validator failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
        finally { report?.Dispose(); }
    }
}
