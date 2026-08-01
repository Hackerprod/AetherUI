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
                    MetricsSink = perfReporter is null ? null : perfReporter.Report,
                });
            return;
        }

        Application.Run(new ValidatorForm(options.HtmlPath, perfReporter));
    }

    private static ValidatorCommandLineOptions ParseArgs(string[] args)
    {
        var perf = false;
        string? path = null;
        for (var i = 0; i < args.Length; i++)
        {
            var arg = args[i];
            if (string.Equals(arg, "--perf", StringComparison.OrdinalIgnoreCase))
            {
                perf = true;
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

        return new ValidatorCommandLineOptions(path is null ? null : Path.GetFullPath(path), perf);
    }

    private sealed record ValidatorCommandLineOptions(string? HtmlPath, bool Perf);
}
