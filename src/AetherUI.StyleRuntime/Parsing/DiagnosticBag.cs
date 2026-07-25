namespace AetherUI.StyleRuntime;

internal sealed class DiagnosticBag
{
    private readonly string _source;
    private readonly string _sourceName;
    private readonly List<StyleDiagnostic> _items = [];

    public DiagnosticBag(string source, string sourceName)
    {
        _source = source ?? string.Empty;
        _sourceName = string.IsNullOrWhiteSpace(sourceName) ? "<style>" : sourceName;
    }

    public IReadOnlyList<StyleDiagnostic> Items => _items;

    public void Error(string code, string message, int offset) =>
        Add(code, StyleDiagnosticSeverity.Error, message, offset);

    public void Warning(string code, string message, int offset) =>
        Add(code, StyleDiagnosticSeverity.Warning, message, offset);

    public void Info(string code, string message, int offset) =>
        Add(code, StyleDiagnosticSeverity.Info, message, offset);

    private void Add(string code, StyleDiagnosticSeverity severity, string message, int offset)
    {
        offset = Math.Clamp(offset, 0, _source.Length);
        var line = 1;
        var column = 1;
        for (var index = 0; index < offset; index++)
        {
            if (_source[index] == '\n')
            {
                line++;
                column = 1;
            }
            else
            {
                column++;
            }
        }

        _items.Add(new StyleDiagnostic(code, severity, message, _sourceName, line, column));
    }
}
