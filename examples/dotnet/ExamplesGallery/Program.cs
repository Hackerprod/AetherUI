using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;
using ExamplesGallery.Views;

namespace ExamplesGallery;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        var options = new UiHostOptions
        {
            Title = "AetherUI — Examples Gallery",
            Width = 760,
            Height = 640,
            MinimumWidth = 560,
            MinimumHeight = 480,
            ClearColor = ColorRgba.White,
        };

        return WindowsUiApplication.Run<LauncherView>(options);
    }
}
