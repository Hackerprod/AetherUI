namespace AetherUI.Validator;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        var htmlPath = args.Length > 0
            ? args[0]
            : Path.Combine(AppContext.BaseDirectory, "sample.html");

        Application.Run(new ValidatorForm(htmlPath));
    }
}
