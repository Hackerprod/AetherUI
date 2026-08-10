using AetherUI.Events;
using AetherUI.Runtime;

namespace AetherUI.Validator;

/// <summary>
/// Interactive modal demo built on the reusable <see cref="ModalController"/>: a button opens a
/// modal whose overlay fades in and whose card scales in through the CSS transitions runtime.
/// The page supplies only the card content; the overlay chrome (fixed, darken, blur, z-index,
/// backdrop/Escape close) comes from the controller.
/// </summary>
public sealed class ModalComponent : UiComponent
{
    public const string WindowTitle = "AetherUI Validator - Modal Interactivo (transitions)";

    private readonly ModalController _modal = new();

    public ModalComponent()
    {
        _modal.Changed += () => StateHasChanged();
    }

    protected override void BuildRenderTree(MarkupWriter writer)
    {
        WritePageStyles(writer);
        writer.WriteLiteral("""
            <body>
              <div class="circle circle-1"></div>
              <div class="circle circle-2"></div>
              <div class="container">
                <h1>Diseño Moderno</h1>
                <p>Ejemplo de diseño con efecto Glassmorphism usando solo CSS. Haz clic abajo para abrir el modal.</p>
                <button class="btn" id="openModal"
            """);
        writer.WriteEventAttribute("onclick", OpenModal);
        writer.WriteLiteral(">Abrir Modal</button></div>");

        _modal.Render(writer);
        writer.WriteLiteral("</body>");
    }

    private static void WritePageStyles(MarkupWriter writer)
    {
        writer.WriteLiteral("""
            <style>
              :root {
                --primary-color: #6366f1;
                --bg-gradient: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                --glass-bg: rgba(255, 255, 255, 0.1);
                --glass-border: rgba(255, 255, 255, 0.2);
                --text-color: #ffffff;
              }
              * { margin: 0; padding: 0; box-sizing: border-box; font-family: "Segoe UI", Tahoma, Geneva, Verdana, sans-serif; }
              body { min-height: 100vh; display: flex; justify-content: center; align-items: center;
                     background: var(--bg-gradient); overflow: hidden; position: relative; }
              .circle { position: absolute; border-radius: 50%; background: rgba(255, 255, 255, 0.1); z-index: 0; }
              .circle-1 { width: 300px; height: 300px; top: -100px; left: -100px; }
              .circle-2 { width: 200px; height: 200px; bottom: -50px; right: -50px; }
              .container { position: relative; z-index: 1; padding: 40px; background: var(--glass-bg);
                           backdrop-filter: blur(15px); border: 1px solid var(--glass-border); border-radius: 20px;
                           box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37); text-align: center; max-width: 400px; width: 90%; }
              h1 { margin-bottom: 20px; font-size: 2rem; color: var(--text-color); }
              p { margin-bottom: 30px; line-height: 1.6; color: rgba(255, 255, 255, 0.8); }
              .btn { padding: 12px 24px; background: var(--primary-color); color: white; border: none;
                     border-radius: 10px; cursor: pointer; font-size: 1rem; font-weight: 600;
                     transition: transform 0.2s ease;
                     box-shadow: 0 4px 15px rgba(99, 102, 241, 0.4); }
              .btn:hover { background: #4f46e5; transform: translateY(-2px); }
              .btn:active { transform: translateY(0); }
              /* Card content chrome (the overlay/card look comes from ModalController). */
              .modal-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
              .modal-header h2 { font-size: 1.5rem; color: #1f2937; }
              .close-btn { width: 32px; height: 32px; background: none; border: none; font-size: 1.5rem;
                           cursor: pointer; color: #6b7280; }
              .modal-body p { color: #4b5563; margin-bottom: 20px; line-height: 1.6; }
              .modal-footer { text-align: right; }
            </style>
            """);
    }

    private void OpenModal(UiEvent e)
    {
        _modal.Show(w =>
        {
            w.WriteLiteral("<div class=\"modal-header\"><h2>¡Hola!</h2><button class=\"close-btn\" id=\"closeModal\"");
            w.WriteEventAttribute("onclick", CloseModal);
            w.WriteLiteral(">&times;</button></div>");
            w.WriteLiteral("<div class=\"modal-body\"><p>Este es un modal interactivo con fondo desenfocado, " +
                "animado por el runtime de transiciones CSS de AetherUI y el ModalController reutilizable.</p></div>");
            w.WriteLiteral("<div class=\"modal-footer\"><button class=\"btn\" id=\"closeModalBtn\"");
            w.WriteEventAttribute("onclick", CloseModal);
            w.WriteLiteral(">Entendido</button></div>");
        });
    }

    private void CloseModal(UiEvent e) => _modal.Close();
}
