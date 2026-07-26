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
    private static readonly HashSet<string> Seen = new(StringComparer.Ordinal);
    private static readonly object Gate = new();

    public static event Action<CssStyleDiagnostic>? Reported;

    public static void ReportUnknownProperty(string property, string value) => Report(
        "AUI-CSS-100",
        property,
        value,
        $"Property '{property}' is not part of AetherUI's supported CSS profile; the declaration was ignored.");

    public static void ReportUnsupportedValue(string property, string value) => Report(
        "AUI-CSS-101",
        property,
        value,
        $"Property '{property}' does not support the value '{value}'; the declaration was ignored.");

    private static void Report(string code, string property, string value, string message)
    {
        var key = string.Concat(code, "|", property, "|", value);
        lock (Gate)
        {
            if (!Seen.Add(key))
            {
                return;
            }
        }

        var diagnostic = new CssStyleDiagnostic(code, property, value, message);
        Debug.WriteLine($"[AetherUI CSS] {code}: {message}");
        Reported?.Invoke(diagnostic);
    }
}
