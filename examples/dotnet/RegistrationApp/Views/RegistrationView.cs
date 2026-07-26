using AetherUI.Events;
using AetherUI.Runtime;

namespace RegistrationApp.Views;

public sealed partial class RegistrationView : UiComponent
{
    private string Nombre { get; set; } = string.Empty;
    private string Mensaje { get; set; } = "Completa el formulario para continuar.";
    private string ClaseBoton { get; set; } = "submit-button";

    private void MostrarEscritura(UiEvent e)
    {
        Nombre = e.Value ?? string.Empty;
        Mensaje = string.IsNullOrWhiteSpace(Nombre)
            ? "Completa el formulario para continuar."
            : $"Escribiendo: {Nombre}";
        StateHasChanged();
    }

    private void EnviarFormulario(UiEvent e)
    {
        e.PreventDefault();
        Mensaje = $"Registro enviado correctamente para {Nombre}.";
        StateHasChanged();
    }

    private void ResaltarBoton(UiEvent e)
    {
        ClaseBoton = "submit-button highlighted";
        StateHasChanged();
    }

    private void RestaurarBoton(UiEvent e)
    {
        ClaseBoton = "submit-button";
        StateHasChanged();
    }
}
