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

        if (args.Length == 0)
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
                });
            return;
        }

        Application.Run(new ValidatorForm(ResolveHtmlPath(args)));
    }

    private static string ResolveHtmlPath(string[] args)
    {
        var path = args.Length == 2 && string.Equals(args[0], "--page", StringComparison.OrdinalIgnoreCase)
            ? args[1]
            : args[0];

        return Path.GetFullPath(path);
    }
}
