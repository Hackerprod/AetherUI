using AetherUI.Runtime;

namespace AetherUI.Validator;

/// <summary>Immutable markup snapshots: file IO belongs to the watcher, never the render callback.</summary>
public sealed class RawHtmlFileComponent : UiComponent
{
    private string _markup;
    public RawHtmlFileComponent(string markup) => _markup = markup;
    protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(_markup);
    // Called only on the native host thread through its message-loop dispatcher.
    public void Reload(string markup) { _markup = markup; StateHasChanged(); }
}
