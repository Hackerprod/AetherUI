using AetherUI.Runtime;

namespace AetherUI.Validator;

/// <summary>
/// Renders a plain .html file's contents verbatim through AetherUI's real pipeline (HTML parser,
/// AetherUI.StyleRuntime cascade, layout, Direct2D paint) with no Razor compilation step. The same
/// file is loaded into a WebView2 control side by side, so both panes render from one source of
/// truth — no risk of the two copies drifting apart.
/// </summary>
public sealed class RawHtmlFileComponent : UiComponent
{
    private readonly string _path;

    public RawHtmlFileComponent(string path)
    {
        _path = path;
    }

    protected override void BuildRenderTree(MarkupWriter writer)
    {
        var markup = File.Exists(_path)
            ? File.ReadAllText(_path)
            : $"<body><p>File not found: {_path}</p></body>";
        writer.WriteLiteral(markup);
    }

    public void Reload() => StateHasChanged();
}
