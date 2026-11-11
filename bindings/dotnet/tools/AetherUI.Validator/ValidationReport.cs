using System.Diagnostics;
using System.Text.Json;

namespace AetherUI.Validator;

/// <summary>JSON Lines report, one private run directory per process; never a shared append file.</summary>
public sealed class ValidationReport : IDisposable
{
    private readonly object _gate = new();
    private readonly StreamWriter _writer;
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private bool _disposed;
    public string DirectoryPath { get; }

    public ValidationReport(string root)
    {
        DirectoryPath = Path.Combine(Path.GetFullPath(root),
            $"{DateTime.UtcNow:yyyyMMddTHHmmssfffZ}-{Environment.ProcessId}-{Guid.NewGuid():N}");
        Directory.CreateDirectory(DirectoryPath);
        _writer = new StreamWriter(Path.Combine(DirectoryPath, "report.log"), false) { AutoFlush = true };
        Write("session", new { os = Environment.OSVersion.ToString(), runtime = Environment.Version.ToString(),
            architecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
            visualParity = "not-run", scope = "manual Windows validator" });
    }

    public void Write(string kind, object data)
    {
        lock (_gate)
        {
            if (_disposed) return;
            _writer.WriteLine(JsonSerializer.Serialize(new { timestampUtc = DateTime.UtcNow,
                elapsedMs = _clock.Elapsed.TotalMilliseconds, kind, data }));
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            if (_disposed) return;
            Write("session-end", new { visualParity = "not-run" });
            _disposed = true;
            _writer.Dispose();
        }
    }
}
