# Arquitectura de AetherUI

## Objetivo

AetherUI separa el lenguaje declarativo, el modelo semántico, el layout y el backend nativo. HTML y CSS son entradas del motor; no se delegan a un navegador. C# es el lenguaje de estado y eventos de la aplicación.

## Componentes

### `AetherUI.Generator`

Generador incremental Roslyn que recibe archivos `.cshtml` como `AdditionalFiles` y emite una clase parcial derivada de `UiComponent`.

Responsabilidades:

- procesar `@namespace`, `@class`, `@inherits`, `@using` y `@code`;
- convertir markup literal en llamadas `MarkupWriter.WriteLiteral`;
- convertir expresiones en `MarkupWriter.Write`;
- convertir atributos dinámicos en `MarkupWriter.WriteAttribute`;
- convertir atributos `on*` dinámicos en `MarkupWriter.WriteEventAttribute`;
- emitir diagnósticos asociados a posiciones del archivo original;
- generar `#line` para que los errores del bloque C# apunten a la vista.

No ejecuta C# ni genera assemblies durante la ejecución de la aplicación.

### `AetherUI.Core`

Contiene las capas independientes de plataforma.

```text
UiComponent
    │ RenderMarkup()
    ▼
MarkupWriter
    │ string confiable + valores codificados
    ▼
HtmlParser ─────────────► DocumentNode / ElementNode / TextNode
    │
    ▼
StyleResolver ──────────► StyleMap / ComputedStyle
    │
    ▼
LayoutEngine ───────────► LayoutResult / LayoutBox
    │
    ▼
DisplayListBuilder ─────► DisplayList / PaintCommand
    │
    ▼
IRenderBackend
```

`UiDocumentHost` coordina estas capas, mantiene foco/hover/active, enruta eventos y solicita nuevos frames.

### `AetherUI.Windows`

Contiene el host Win32 y el backend gráfico actual.

- creación de HWND sin WinForms/WPF;
- bucle de mensajes;
- DPI Per-Monitor V2;
- traducción de mouse/teclado a eventos del core;
- selección de cursor;
- render target Direct2D asociado al HWND;
- medición y dibujo de texto con DirectWrite;
- recreación del render target ante fallo de dispositivo.

## Modelo de render

AetherUI utiliza una **display list retenida**. El layout produce cajas en DIPs; `DisplayListBuilder` emite comandos inmutables de alto nivel:

- rellenar rectángulo redondeado;
- dibujar borde redondeado;
- dibujar texto;
- abrir/cerrar clip.

El backend traduce esos comandos a la API gráfica disponible. El core no conoce Direct2D, HWND ni detalles COM.

## Coordenadas y DPI

- Win32 entrega dimensiones y punteros en píxeles físicos.
- `UiDocumentHost` divide por `dpiScale` y trabaja en DIPs.
- Direct2D recibe el DPI del monitor y una superficie con tamaño físico.
- Layout, hit testing y CSS usan el mismo sistema lógico.

## Estado y rerender

1. Un evento C# modifica campos de la clase de vista.
2. `StateHasChanged()` marca el markup como sucio.
3. El host vuelve a invocar `RenderMarkup()`.
4. Se reconstruyen DOM, estilos, layout y display list.
5. Los valores no controlados de `<input>` se conservan por `id`.
6. Foco, hover y active se restauran por `id` cuando es posible.

Este primer perfil reconstruye el árbol completo después de un cambio de estado. La frontera entre capas ya permite evolucionar a diff DOM, invalidación por propiedad y recomposición parcial sin cambiar la sintaxis de las vistas.

## Eventos

Los manejadores no se escriben como nombres de función ejecutados por JavaScript. En cada render:

```csharp
writer.WriteEventAttribute("onclick", AlClick);
```

registra el delegado y escribe un token interno:

```html
<button onclick="aether:0">
```

El DOM conserva el token. Cuando ocurre un evento, `UiDocumentHost` busca el delegado en el componente actual y propaga desde target hacia ancestros. `PreventDefault()` y `StopPropagation()` están implementados.

## Modelo de seguridad

- No existe motor JavaScript.
- No existe navegación ni networking.
- Las expresiones se compilan con el proyecto.
- Los valores dinámicos se codifican con HTML encoding.
- Los nombres de atributos emitidos por `MarkupWriter` se validan.
- Los eventos aceptan solamente atributos `on*` y delegados registrados.

`WriteLiteral` permanece deliberadamente como API privilegiada para markup controlado por el desarrollador.

## Hilos

La implementación 0.1 presupone un único hilo de UI:

- el HWND, el componente y `UiDocumentHost` viven en el mismo hilo;
- los eventos se ejecutan sincrónicamente;
- no existe dispatcher público todavía;
- un cambio de estado desde otro hilo no está soportado.

## Extensión a otras plataformas

Un backend nuevo debe implementar:

```csharp
public interface IRenderBackend : IDisposable
{
    void Resize(int pixelWidth, int pixelHeight, float dpiScale);
    void Render(DisplayList displayList, ColorRgba clearColor);
}

public interface ITextMeasurer
{
    TextMetrics MeasureText(
        string text,
        ComputedStyle style,
        float maxWidth = float.PositiveInfinity);
}
```

La plataforma también debe traducir entrada, DPI, clipboard, IME, accesibilidad y ciclo de ventana. El DOM/CSS/layout no requiere cambios para añadir Metal, Vulkan, Skia u otro backend.
