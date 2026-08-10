using AetherUI.Events;

namespace AetherUI.Runtime;

/// <summary>
/// Reusable modal host for any <see cref="UiComponent"/>. The owning component calls
/// <see cref="Show"/> / <see cref="Close"/> (wire <see cref="Changed"/> to its
/// <c>StateHasChanged</c>) and renders the overlay from its BuildRenderTree via
/// <see cref="Render"/>. All the repetitive modal chrome — fixed overlay, darken,
/// backdrop blur, z-index, opacity/scale transitions, backdrop/Escape close — lives
/// here; the page supplies only the card content through a <see cref="MarkupWriter"/>
/// delegate (events inside the content use <see cref="MarkupWriter.WriteEventAttribute"/>).
/// </summary>
public sealed class ModalController
{
    private readonly int _zIndex;
    private Action<MarkupWriter>? _content;

    public ModalController(int zIndex = 100)
    {
        _zIndex = zIndex;
    }

    /// <summary>Raised by <see cref="Show"/> and <see cref="Close"/> so the owning
    /// component can re-render (e.g. subscribe with <c>_modal.Changed += () =&gt; StateHasChanged();</c>).</summary>
    public event Action? Changed;

    public bool IsOpen { get; private set; }

    /// <summary>Opens the modal with the given card content. Call <see cref="Changed"/> via the
    /// owning component's re-render; the overlay animates in through the engine's CSS transitions.</summary>
    public void Show(Action<MarkupWriter> content)
    {
        _content = content ?? throw new ArgumentNullException(nameof(content));
        IsOpen = true;
        Changed?.Invoke();
    }

    public void Close()
    {
        if (!IsOpen)
        {
            return;
        }

        IsOpen = false;
        Changed?.Invoke();
    }

    /// <summary>Writes the modal styles (once) and the overlay markup while open. Call from the
    /// owning component's BuildRenderTree — after any page content, before the closing tag.</summary>
    public void Render(MarkupWriter writer)
    {
        WriteStyles(writer);
        if (!IsOpen)
        {
            return;
        }

        writer.WriteLiteral("<div class=\"aether-modal-overlay aether-modal-open\" tabindex=\"0\"");
        writer.WriteEventAttribute("onclick", OnOverlayClick);
        writer.WriteEventAttribute("onkeydown", OnOverlayKeyDown);
        writer.WriteLiteral("><div class=\"aether-modal-card\">");
        _content?.Invoke(writer);
        writer.WriteLiteral("</div></div>");
    }

    private void WriteStyles(MarkupWriter writer)
    {
        // The style block is re-emitted on EVERY render (like any component's own
        // <style>): the host rebuilds the document per render, so a once-only write
        // would leave the reopened overlay's CSS missing from the rebuilt tree.
        // All visual tokens are CSS custom properties with defaults, so any page can
        // theme the modal by overriding --aether-modal-* in its own stylesheet.
        writer.WriteLiteral("<style>");
        writer.WriteLiteral(
            ".aether-modal-overlay { position: fixed; top: 0; left: 0; width: 100%; height: 100%; " +
            "background-color: var(--aether-modal-overlay-bg, rgba(0, 0, 0, 0.5)); " +
            "backdrop-filter: blur(var(--aether-modal-overlay-blur, 8px)); " +
            "display: flex; align-items: center; justify-content: center; " +
            "z-index: var(--aether-modal-z-index, " + _zIndex + "); opacity: 0; pointer-events: none; " +
            "transition: opacity var(--aether-modal-fade-duration, 0.3s) ease; } " +
            ".aether-modal-overlay.aether-modal-open { opacity: 1; pointer-events: auto; } " +
            ".aether-modal-card { " +
            "background-color: var(--aether-modal-card-bg, #ffffff); " +
            "color: var(--aether-modal-card-color, #1f2937); " +
            "border-radius: var(--aether-modal-card-radius, 15px); " +
            "padding: var(--aether-modal-card-padding, 30px); " +
            "max-width: var(--aether-modal-card-max-width, 500px); width: 90%; position: relative; " +
            "transform: scale(0.8); transition: transform var(--aether-modal-scale-duration, 0.3s) ease; " +
            "box-shadow: var(--aether-modal-card-shadow, 0 8px 32px rgba(0, 0, 0, 0.37)); } " +
            ".aether-modal-overlay.aether-modal-open .aether-modal-card { transform: scale(1); }");
        writer.WriteLiteral("</style>");
    }

    /// <summary>Clicking the backdrop (not the card) closes the modal.</summary>
    private void OnOverlayClick(UiEvent e)
    {
        if (ReferenceEquals(e.Target, e.CurrentTarget))
        {
            Close();
        }
    }

    /// <summary>Escape closes when the overlay has focus (the host routes keys to the focused element;
    /// a trigger button can also handle Escape itself by calling <see cref="Close"/>).</summary>
    private void OnOverlayKeyDown(UiEvent e)
    {
        if (e.Key == UiKey.Escape)
        {
            Close();
        }
    }
}
