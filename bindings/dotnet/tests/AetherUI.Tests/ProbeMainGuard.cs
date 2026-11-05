using System;
using System.Threading.Tasks;

internal static class ProbeMainGuard
{
    public static int Run(Func<int> action)
    {
        try
        {
            return action();
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"PROBE_FAILURE_HANDLED: {error}");
            return 1;
        }
    }

    public static async Task<int> RunAsync(Func<Task<int>> action)
    {
        try
        {
            return await action().ConfigureAwait(false);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"PROBE_FAILURE_HANDLED: {error}");
            return 1;
        }
    }
}
