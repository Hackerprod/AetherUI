using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;
using RegistrationApp.Views;

namespace RegistrationApp;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        var options = new UiHostOptions
        {
            Title = "AetherUI — Registro",
            Width = 900,
            Height = 680,
            MinimumWidth = 520,
            MinimumHeight = 460,
            ClearColor = ColorRgba.White,
        };

        return WindowsUiApplication.Run<RegistrationView>(options);
    }
}
