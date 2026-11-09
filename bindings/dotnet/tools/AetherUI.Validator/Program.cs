using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;

namespace AetherUI.Validator;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        var options = ParseArgs(args);
        var perfReporter = options.Perf ? new PerfReporter("native") : null;
        if (options.Modal)
        {
            WindowsUiApplication.Run(
                () => new ModalComponent(),
                new UiHostOptions
                {
                    Title = ModalComponent.WindowTitle,
                    Width = 800,
                    Height = 640,
                    ClearColor = new ColorRgba(0.09f, 0.10f, 0.14f, 1f),
                    EnableInputWorkCounters = Environment.GetEnvironmentVariable("AETHERUI_WORK_COUNTERS") == "1",
                    MetricsSink = perfReporter is null ? null : perfReporter.Report,
                });
            return;
        }

        if (options.HtmlPath is null)
        {
            var pagesDirectory = Path.Combine(AppContext.BaseDirectory, "Pages");
            WindowsUiApplication.Run(
                () => new PageCatalogComponent(pagesDirectory),
                new UiHostOptions
                {
                    Title = PageCatalogComponent.WindowTitle,
                    Width = 920,
                    Height = 760,
                    ClearColor = new ColorRgba(0.035f, 0.063f, 0.114f, 1f),
                    EnableInputWorkCounters = Environment.GetEnvironmentVariable("AETHERUI_WORK_COUNTERS") == "1",
                    MetricsSink = perfReporter is null ? null : perfReporter.Report,
                });
            return;
        }

        Application.Run(new ValidatorForm(options.HtmlPath, perfReporter));
    }

    private static ValidatorCommandLineOptions ParseArgs(string[] args)
    {
        var perf = false;
        var modal = false;
        string? path = null;
        for (var i = 0; i < args.Length; i++)
        {
            var arg = args[i];
            if (string.Equals(arg, "--perf", StringComparison.OrdinalIgnoreCase))
            {
                perf = true;
                continue;
            }

            if (string.Equals(arg, "--modal", StringComparison.OrdinalIgnoreCase))
            {
                modal = true;
                continue;
            }

            if (string.Equals(arg, "--page", StringComparison.OrdinalIgnoreCase))
            {
                if (++i >= args.Length)
                {
                    throw new ArgumentException("--page requires an HTML path.");
                }

                path = args[i];
                continue;
            }

            path ??= arg;
        }

        return new ValidatorCommandLineOptions(path is null ? null : Path.GetFullPath(path), perf, modal);
    }

    private sealed record ValidatorCommandLineOptions(string? HtmlPath, bool Perf, bool Modal);
}
