using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;
using DashboardApp.Views;

namespace DashboardApp;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        var options = new UiHostOptions
        {
            Title = "AetherUI — Panel",
            Width = 860,
            Height = 720,
            MinimumWidth = 560,
            MinimumHeight = 560,
            ClearColor = ColorRgba.White,
        };

        return WindowsUiApplication.Run<DashboardView>(options);
    }
}
