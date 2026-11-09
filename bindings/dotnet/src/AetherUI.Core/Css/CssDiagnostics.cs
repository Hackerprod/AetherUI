using System.Diagnostics;

namespace AetherUI.Css;

/// <summary>A non-fatal CSS authoring issue found while resolving styles at runtime.</summary>
public readonly record struct CssStyleDiagnostic(string Code, string Property, string Value, string Message);

/// <summary>
/// Reports CSS declarations AetherUI recognized but could not apply, instead of silently
/// dropping them. Each unique (code, property, value) triple is reported once per process.
/// </summary>
public static class CssDiagnostics
{
    private static readonly HashSet<(string Code, string Property, string Value)> Seen = new();
    private static readonly object Gate = new();
    [ThreadStatic] private static Transaction? _staging;

    public static event Action<CssStyleDiagnostic>? Reported;

    // Style conversion is synchronous and owner-thread confined. Do not flow staging into
    // unrelated tasks: concurrent hosts must have independent buffers.
    internal static Transaction BeginTransaction()
    {
        var transaction = new Transaction(_staging);
        _staging = transaction;
        return transaction;
    }

    public static void ReportUnknownProperty(string property, string value) => Report(
        "AUI-CSS-100", property, value,
        $"Property '{property}' is not part of AetherUI's supported CSS profile; the declaration was ignored.");

    public static void ReportUnsupportedValue(string property, string value) => Report(
        "AUI-CSS-101", property, value,
        $"Property '{property}' does not support the value '{value}'; the declaration was ignored.");

    private static void Report(string code, string property, string value, string message)
    {
        var diagnostic = new CssStyleDiagnostic(code, property, value, message);
        if (_staging is { } transaction)
        {
            // All buffer growth is before publication. Aborting leaves Seen untouched.
            transaction.Add(diagnostic);
            return;
        }
        Emit(diagnostic);
    }

    private static void Emit(CssStyleDiagnostic diagnostic)
    {
        lock (Gate)
        {
            if (!Seen.Add((diagnostic.Code, diagnostic.Property, diagnostic.Value)))
                return;
        }
        // Never run user code under Gate. Once claimed, a key remains consumed even if
        // notification fails: external effects cannot be rolled back safely.
        Debug.WriteLine($"[AetherUI CSS] {diagnostic.Code}: {diagnostic.Message}");
        Reported?.Invoke(diagnostic);
    }

    /// <summary>A notification failed after the host published; this is not a staging failure.</summary>
    internal sealed class NotificationException : Exception
    {
        internal NotificationException(Exception innerException)
            : base("CSS diagnostic notification failed after publication.", innerException) { }
    }

    /// <summary>
    /// Capture synchronously, Seal before publication, then Replay only after publication.
    /// Dispose without Replay aborts. A sealed buffer may be transferred to another thread,
    /// but the caller must serialize its use and retain its host reentrancy guard in Replay.
    /// </summary>
    internal sealed class Transaction : IDisposable
    {
        private readonly Transaction? _previous;
        private readonly List<CssStyleDiagnostic> _diagnostics = new();
        private readonly int _ownerThread = Environment.CurrentManagedThreadId;
        private bool _sealed;
        private bool _finished;

        internal Transaction(Transaction? previous) => _previous = previous;

        internal void Add(CssStyleDiagnostic diagnostic) => _diagnostics.Add(diagnostic);

        internal void Seal()
        {
            if (_finished)
                throw new ObjectDisposedException(nameof(Transaction));
            if (_sealed)
                return;
            RequireOwner();
            _staging = _previous;
            _sealed = true;
        }

        internal void Replay()
        {
            if (_finished)
                throw new InvalidOperationException("The CSS diagnostic transaction is already finished.");
            if (!_sealed)
                throw new InvalidOperationException("End CSS diagnostic staging before replaying notifications.");
            _finished = true;
            var previousStaging = _staging;
            _staging = null; // Reentrant reports belong to notifications, not another host's staging.
            try
            {
                foreach (var diagnostic in _diagnostics)
                    Emit(diagnostic);
            }
            catch (Exception error)
            {
                throw new NotificationException(error);
            }
            finally
            {
                _staging = previousStaging;
                _diagnostics.Clear();
            }
        }

        public void Dispose()
        {
            if (_finished)
                return;
            if (!_sealed)
            {
                RequireOwner();
                _staging = _previous;
            }
            _finished = true;
            _diagnostics.Clear();
        }

        private void RequireOwner()
        {
            if (_ownerThread != Environment.CurrentManagedThreadId || !ReferenceEquals(_staging, this))
                throw new InvalidOperationException("CSS diagnostic staging must end on its owning thread.");
        }
    }
}
