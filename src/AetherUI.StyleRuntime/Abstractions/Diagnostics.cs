namespace AetherUI.StyleRuntime;

public enum StyleDiagnosticSeverity : byte
{
    Info,
    Warning,
    Error,
}

public readonly record struct StyleDiagnostic(
    string Code,
    StyleDiagnosticSeverity Severity,
    string Message,
    string SourceName,
    int Line,
    int Column);

public sealed class StyleCompilationException : Exception
{
    public StyleCompilationException(string message, IReadOnlyList<StyleDiagnostic> diagnostics)
        : base(message)
    {
        Diagnostics = diagnostics ?? throw new ArgumentNullException(nameof(diagnostics));
    }

    public IReadOnlyList<StyleDiagnostic> Diagnostics { get; }
}

public readonly record struct StyleSheetHandle(long Value)
{
    public bool IsValid => Value > 0;
}

public readonly record struct StyleSheetLoadResult(
    StyleSheetHandle Handle,
    IReadOnlyList<StyleDiagnostic> Diagnostics,
    int RuleCount);
