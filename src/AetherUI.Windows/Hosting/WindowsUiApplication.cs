using AetherUI.Runtime;

namespace AetherUI.Windows.Hosting;

/// <summary>Entry point for running an AetherUI component in a native Win32 window.</summary>
public static class WindowsUiApplication
{
    public static int Run<TComponent>(UiHostOptions? options = null)
        where TComponent : UiComponent, new() =>
        Run(static () => new TComponent(), options);

    public static int Run(Func<UiComponent> componentFactory, UiHostOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(componentFactory);
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("AetherUI.Windows requiere Windows 10 o posterior.");
        }

        var component = componentFactory() ?? throw new InvalidOperationException("La fábrica devolvió un componente nulo.");
        using var window = new NativeWindow(options ?? new UiHostOptions());
        var ownershipTransferred = false;
        try
        {
            window.Create();
            ownershipTransferred = true;
            window.Attach(component);
            window.Show();
            return window.RunMessageLoop();
        }
        catch
        {
            // Attach takes ownership at entry and disposes the component if initialization fails.
            if (!ownershipTransferred)
            {
                component.Dispose();
            }

            throw;
        }
    }
}
