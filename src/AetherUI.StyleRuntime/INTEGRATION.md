# AetherUI.StyleRuntime

Runtime dinámico de estilos diseñado específicamente para el árbol visual de AetherUI.

El texto de entrada conserva la sintaxis familiar de CSS, pero termina ahí. El runtime **no implementa un DOM, CSSOM, navegador, navegación, JavaScript, elementos HTML ni un motor web embebido**. Compila las hojas a reglas internas de AetherUI, las evalúa contra el árbol vivo del motor y produce estructuras tipadas que layout, texto, pintura, composición e hit-testing pueden consumir directamente.

## 1. Límites del componente

El runtime se responsabiliza de:

- tokenizar y analizar hojas de estilo;
- convertir propiedades registradas a valores tipados;
- expandir shorthands;
- compilar selectores a planes estructurados;
- indexar reglas por tipo, clase e identificador;
- resolver selectores contra el árbol visual vivo;
- aplicar prioridad, especificidad, orden, capas e `!important`;
- heredar propiedades registradas como heredables;
- resolver custom properties y `var()`;
- evaluar condiciones `@media` soportadas;
- comparar el estilo anterior con el actual;
- devolver impactos precisos para el pipeline del motor.

El runtime no se responsabiliza de:

- crear nodos;
- interpretar HTML o `.cshtml`;
- ejecutar código C# de vistas;
- calcular geometría de block/flex;
- formar líneas de texto;
- dibujar fondos, bordes o sombras;
- crear capas GPU;
- gestionar foco, mouse, teclado o accesibilidad.

Esas capacidades siguen perteneciendo a AetherUI. El runtime solo transforma:

```text
hojas de estilo + estado actual del árbol
                    ↓
          AetherComputedStyle
                    +
              StyleImpact
```

## 2. Estructura entregada

```text
AetherUI.StyleRuntime/
├── INTEGRATION.md
└── src/
    └── AetherUI.StyleRuntime/
        ├── AetherUI.StyleRuntime.csproj
        ├── Abstractions/
        ├── Model/
        ├── Parsing/
        ├── Properties/
        ├── Runtime/
        └── Selectors/
```

No contiene paquetes externos. El proyecto apunta a `net10.0`, es compatible con trimming/Native AOT y no utiliza reflexión, `dynamic`, código `unsafe` ni callbacks hacia otro motor.

## 3. Añadirlo a AetherUI

Copiar la carpeta del proyecto al repositorio y añadir una referencia:

```xml
<ItemGroup>
  <ProjectReference Include="..\AetherUI.StyleRuntime\AetherUI.StyleRuntime.csproj" />
</ItemGroup>
```

El namespace público es:

```csharp
using AetherUI.StyleRuntime;
```

## 4. Conectar el árbol visual

Cada nodo estilizable implementa `IAetherStyleNode`. Esta interfaz no representa un nodo web. Es una vista mínima del nodo nativo que el runtime necesita para consultar estructura, clases, atributos y estados.

```csharp
public interface IAetherStyleNode
{
    long StyleNodeId { get; }
    string StyleType { get; }
    string? StyleId { get; }
    IReadOnlyList<string> StyleClasses { get; }
    StylePseudoState StyleStates { get; }
    string? InlineStyleText { get; }

    IAetherStyleNode? StyleParent { get; }
    IReadOnlyList<IAetherStyleNode> StyleChildren { get; }

    bool HasStyleClass(string className);
    bool TryGetStyleAttribute(string name, out string? value);
}
```

### Reglas del contrato

- `StyleNodeId` debe permanecer estable durante toda la vida del nodo y no reutilizarse mientras exista una entrada en caché.
- `StyleType` es el tipo nativo que AetherUI decida exponer: `window`, `panel`, `button`, `input`, `text`, etc.
- Las clases y los IDs se comparan de forma ordinal y sensible a mayúsculas.
- Los nombres de tipo se comparan sin distinguir mayúsculas.
- `StyleParent` y `StyleChildren` deben reflejar el árbol que layout y pintura consideran real.
- `TryGetStyleAttribute` solo debe exponer propiedades útiles para selectores. No hace falta mantener una colección genérica de atributos.
- `StyleStates` pertenece al sistema de entrada y controles de AetherUI.

### Implementación directa de ejemplo

```csharp
public sealed class VisualNode : IAetherStyleNode
{
    private readonly HashSet<string> _classes = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string?> _styleAttributes =
        new(StringComparer.OrdinalIgnoreCase);
    private readonly List<VisualNode> _children = [];

    public required long NodeId { get; init; }
    public required string Kind { get; init; }
    public VisualNode? Parent { get; private set; }

    public string? Id { get; set; }
    public string? InlineStyle { get; set; }
    public StylePseudoState States { get; set; }

    public long StyleNodeId => NodeId;
    public string StyleType => Kind;
    public string? StyleId => Id;
    public IReadOnlyList<string> StyleClasses => _classes.ToArray();
    public StylePseudoState StyleStates => States;
    public string? InlineStyleText => InlineStyle;
    public IAetherStyleNode? StyleParent => Parent;
    public IReadOnlyList<IAetherStyleNode> StyleChildren => _children;

    public bool HasStyleClass(string className) => _classes.Contains(className);

    public bool TryGetStyleAttribute(string name, out string? value) =>
        _styleAttributes.TryGetValue(name, out value);

    // Los métodos reales del nodo deben modificar las colecciones y notificar
    // al runtime en la misma operación; véase la sección de mutaciones.
}
```

En producción conviene que `StyleClasses` devuelva una vista almacenada y no cree un array en cada lectura. El ejemplo solo muestra el contrato.

Si el nodo actual de AetherUI es `sealed`, puede usarse un adaptador persistente por nodo. El adaptador debe conservarse; no debe crearse uno nuevo en cada consulta porque `StyleNodeId`, referencias de padre/hijos y cachés deben ser estables.

## 5. Crear el registro y el runtime

El registro define las capacidades que el motor sabe ejecutar. Debe configurarse por completo antes de crear `AetherStyleRuntime`; después queda sellado para garantizar IDs y tablas estables.

```csharp
var properties = StylePropertyRegistry.CreateDefault();

// Registrar aquí capacidades propias de AetherUI antes de crear el runtime.
// properties.RegisterExtension(...);

var runtime = new AetherStyleRuntime(
    properties,
    new StyleRuntimeOptions
    {
        EnforceThreadAffinity = true,
        ThrowOnInvalidStyleSheet = false,
        MaximumVariableDepth = 32,
        MaximumVariableExpansionLength = 64 * 1024,
    },
    new StyleEnvironment(
        ViewportWidth: clientWidth,
        ViewportHeight: clientHeight,
        DpiScale: dpiScale,
        RootFontSize: 16f,
        ColorScheme: darkTheme ? StyleColorScheme.Dark : StyleColorScheme.Light,
        PointerAccuracy: StylePointerAccuracy.Fine,
        MotionPreference: reduceMotion
            ? StyleMotionPreference.Reduce
            : StyleMotionPreference.NoPreference));
```

El runtime debe vivir en el hilo de UI. Con `EnforceThreadAffinity = true`, una llamada desde otro hilo falla inmediatamente en vez de corromper el estado.

## 6. Hojas de estilo y capas nativas

AetherUI decide qué reglas predeterminadas existen. El runtime no introduce semántica HTML ni una hoja de usuario-agente oculta.

```csharp
runtime.DiagnosticsProduced += diagnostics =>
{
    foreach (var diagnostic in diagnostics)
    {
        AetherLog.Write(
            $"{diagnostic.SourceName}({diagnostic.Line},{diagnostic.Column}) " +
            $"{diagnostic.Code}: {diagnostic.Message}");
    }
};

var defaults = runtime.AddStyleSheet(
    """
    window, panel, form { display: block; }
    button, input { display: inline-block; }
    text { display: inline; }
    """,
    StyleSheetLayer.EngineDefaults,
    "aether-defaults.css");

var theme = runtime.AddStyleSheet(
    themeCss,
    StyleSheetLayer.Theme,
    "theme.css");

var view = runtime.AddStyleSheet(
    viewCss,
    StyleSheetLayer.Author,
    "DashboardView.css");
```

Las capas se ordenan así:

```text
EngineDefaults < Theme < Author < inline
```

Dentro de una capa se aplican `!important`, especificidad, orden de regla y orden de declaración.

Para retirar una hoja:

```csharp
runtime.RemoveStyleSheet(view.Handle);
```

Al añadir o retirar hojas, los estilos almacenados quedan versionados como obsoletos. Recalcular la raíz o solicitar de nuevo el estilo de un nodo activa el programa actualizado.

## 7. Primera resolución

Después de construir la vista:

```csharp
IReadOnlyList<StyleUpdate> updates = runtime.ResolveSubtree(rootNode);
ApplyStyleUpdates(updates);
```

Cada actualización contiene:

```csharp
public readonly record struct StyleUpdate(
    IAetherStyleNode Node,
    AetherComputedStyle? Previous,
    AetherComputedStyle Current,
    StyleImpact Impact);
```

`Current` está separado en grupos que corresponden directamente a subsistemas del motor:

```csharp
style.Layout       // display, position, tamaños, edges, overflow, z-index
style.Flex         // dirección, wrap, grow/shrink, alineación, gap, order
style.Text         // fuente, tamaño, peso, line-height, color, white-space
style.Paint        // fondo, bordes, radius, opacity, box-shadow
style.Interaction  // cursor y pointer-events
style.Transform    // origen y operaciones tipadas
style.Extensions   // capacidades propias -aether-*
```

No debe copiarse de nuevo a un diccionario de strings. Layout y render deben leer estos records tipados.

## 8. Traducir impactos a la invalidación del motor

Ejemplo de puente:

```csharp
private static void ApplyStyleUpdates(IReadOnlyList<StyleUpdate> updates)
{
    foreach (var update in updates)
    {
        var node = (VisualNode)update.Node;
        node.ComputedStyle = update.Current;

        if ((update.Impact & StyleImpact.RebuildVisual) != 0)
            node.MarkVisualTreeDirty();

        if ((update.Impact & StyleImpact.Layout) != 0)
            node.MarkLayoutDirty();

        if ((update.Impact & StyleImpact.TextLayout) != 0)
            node.MarkTextLayoutDirty();

        if ((update.Impact & StyleImpact.Paint) != 0)
            node.MarkPaintDirty();

        if ((update.Impact & StyleImpact.Composite) != 0)
            node.MarkCompositeDirty();

        if ((update.Impact & StyleImpact.HitTest) != 0)
            node.MarkHitTestDirty();

        if ((update.Impact & StyleImpact.Cursor) != 0)
            node.MarkCursorDirty();
    }
}
```

`StyleImpact.Inherited` indica que el cambio puede modificar descendientes. El propio runtime ya recalcula los descendientes necesarios en `NotifyMutation`; el host no debe ejecutar una segunda propagación de estilos.

## 9. Mutaciones dinámicas

La modificación del nodo y la notificación deben formar una sola operación del motor.

### Clase

```csharp
node.SetClass("expanded", expanded);
ApplyStyleUpdates(runtime.NotifyMutation(node, StyleMutation.Class("expanded")));
```

### Estado nativo

```csharp
node.States |= StylePseudoState.Hover;
ApplyStyleUpdates(runtime.NotifyMutation(node, StyleMutation.State()));
```

### ID

```csharp
node.Id = "saveButton";
ApplyStyleUpdates(runtime.NotifyMutation(node, StyleMutation.Id()));
```

### Atributo expuesto a selectores

```csharp
node.SetStyleAttribute("variant", "danger");
ApplyStyleUpdates(runtime.NotifyMutation(node, StyleMutation.Attribute("variant")));
```

### Estilo inline generado desde `.cshtml`

```csharp
node.InlineStyle = generatedStyleText;
ApplyStyleUpdates(runtime.NotifyMutation(node, StyleMutation.InlineStyle()));
```

### Inserción, eliminación o reordenamiento

Después de actualizar padre e hijos:

```csharp
ApplyStyleUpdates(runtime.NotifyMutation(parent, StyleMutation.Children()));
```

Al destruir definitivamente un subárbol:

```csharp
runtime.DetachSubtree(removedRoot);
```

Esto elimina sus estilos almacenados.

### Cambio global no clasificable

```csharp
ApplyStyleUpdates(runtime.NotifyMutation(node, StyleMutation.Full()));
```

El runtime usa invalidación conservadora para selectores de ancestros, hermanos, posición estructural y `:has()`. La prioridad inicial es corrección; el índice puede hacerse más granular posteriormente sin cambiar la API pública.

## 10. Cambios de viewport y tema

```csharp
var environment = runtime.Environment with
{
    ViewportWidth = newWidth,
    ViewportHeight = newHeight,
    DpiScale = newDpiScale,
    ColorScheme = isDark
        ? StyleColorScheme.Dark
        : StyleColorScheme.Light,
};

ApplyStyleUpdates(runtime.SetEnvironment(environment, rootNode));
```

Las condiciones `@media` se evalúan contra `StyleEnvironment`; no consultan APIs del sistema directamente.

Condiciones incluidas:

- `min-width`, `max-width`, `width`;
- `min-height`, `max-height`, `height`;
- `prefers-color-scheme`;
- `pointer`;
- `prefers-reduced-motion`;
- listas separadas por coma, `and`, `not` y `only`.

## 11. Resolver longitudes en layout

`StyleLength` conserva componentes relativos y combinaciones lineales, incluido `calc()` compatible con el subconjunto lineal soportado.

```csharp
var width = style.Layout.Width;

if (width.Kind == StyleLengthKind.Linear)
{
    float usedWidth = width.Resolve(new StyleLengthResolveContext(
        PercentageReference: containingBlockContentWidth,
        FontSize: style.Text.FontSize.Pixels,
        RootFontSize: runtime.Environment.RootFontSize,
        ViewportWidth: runtime.Environment.ViewportWidth,
        ViewportHeight: runtime.Environment.ViewportHeight));
}
else if (width.Kind == StyleLengthKind.Auto)
{
    // Algoritmo nativo de sizing de AetherUI.
}
```

Unidades incluidas:

- `px` y cero sin unidad;
- `%`;
- `em` y `rem`;
- `vw` y `vh`;
- `auto`, `none`, `min-content`, `max-content`, `fit-content` donde corresponda;
- `calc()` lineal con suma, resta, multiplicación/división por escalares.

El runtime no decide el valor usado de porcentajes ni `auto`; esa decisión pertenece al algoritmo de layout.

## 12. Capacidades propias de AetherUI

Una propiedad nativa se registra una vez y escribe directamente en un slot tipado de `Extensions`.

```csharp
public enum WindowDragRegion : byte
{
    None,
    Drag,
    NoDrag,
}

static bool TryParseWindowDragRegion(
    string source,
    out WindowDragRegion value)
{
    switch (source.Trim().ToLowerInvariant())
    {
        case "none":
            value = WindowDragRegion.None;
            return true;
        case "drag":
            value = WindowDragRegion.Drag;
            return true;
        case "no-drag":
            value = WindowDragRegion.NoDrag;
            return true;
        default:
            value = default;
            return false;
    }
}
```

Registro, antes de crear el runtime:

```csharp
properties.RegisterExtension(
    name: "-aether-window-drag-region",
    inherited: false,
    impact: StyleImpact.HitTest,
    initialValue: WindowDragRegion.None,
    parser: TryParseWindowDragRegion,
    extensionKey: "window.drag-region");
```

Uso en la vista:

```css
.titlebar {
    -aether-window-drag-region: drag;
}

.titlebar button {
    -aether-window-drag-region: no-drag;
}
```

Consumo en hit-testing:

```csharp
var region = style.GetExtension(
    "window.drag-region",
    WindowDragRegion.None);
```

El impacto declarado en el registro participa automáticamente en `StyleDiff`. No hay que modificar el resolvedor central para añadir la propiedad.

Para una capacidad que merece un campo permanente y de acceso más rápido, se añade el campo a `AetherComputedStyle`/`AetherStyleBuilder` y se registra con `StylePropertyRegistry.Register<T>`.

## 13. Selectores dinámicos incluidos

El runtime compila cada selector una vez y lo evalúa contra el árbol vivo.

Incluye:

- universal y tipo: `*`, `button`;
- ID y clase: `#save`, `.primary`;
- atributos: `[disabled]`, `[variant="danger"]`, `~=`, `|=`, `^=`, `$=`, `*=`;
- combinadores: descendiente, hijo, hermano adyacente y hermano general;
- estados: `:hover`, `:active`, `:focus`, `:focus-within`, `:disabled`, `:enabled`, `:checked`, `:invalid`, `:read-only`, `:read-write`, `:selected`, `:expanded`;
- estructura: `:root`, `:empty`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`;
- agrupación: `:is()`, `:where()`, `:not()`;
- consulta descendente nativa: `:has()` en su forma incluida por el runtime.

`StylePseudoState` no se deduce de reglas web. El control de AetherUI activa y desactiva los bits según su propia máquina de interacción.

## 14. Propiedades incluidas

### Árbol visual y layout

- `display`, `position`, `visibility`, `box-sizing`;
- `width`, `height`, `min-*`, `max-*`;
- `margin` y longhands;
- `padding` y longhands;
- `top`, `right`, `bottom`, `left`;
- `overflow`, `overflow-x`, `overflow-y`;
- `z-index`.

### Flex

- `flex`, `flex-direction`, `flex-wrap`;
- `flex-grow`, `flex-shrink`, `flex-basis`;
- `justify-content`, `align-items`, `align-self`, `align-content`;
- `gap`, `row-gap`, `column-gap`, `order`.

### Texto

- `color`, `font-family`, `font-size`, `font-weight`, `font-style`;
- `line-height`, `text-align`, `white-space`, `letter-spacing`.

### Pintura y composición

- `background` en su forma de color y `background-color`;
- `border`, lados de width/style/color;
- `border-radius`;
- `box-shadow`;
- `opacity`;
- `transform`, `transform-origin`.

### Interacción

- `cursor`;
- `pointer-events`.

### Valores compartidos

- colores hexadecimales, `rgb()`/`rgba()`, `hsl()`/`hsla()`, nombres comunes, `transparent`, `currentColor`;
- custom properties `--nombre` y `var(--nombre, fallback)`;
- keywords globales `initial`, `inherit`, `unset`; `revert` se interpreta actualmente como reinicio al valor inicial del perfil Aether;
- `!important`.

Una propiedad que el registro no conozca produce un diagnóstico y se ignora. El runtime no finge soporte visual para una capacidad que layout o render aún no implementan.

## 15. Flujo interno

```text
texto de estilo
      ↓ una vez por hoja
RemoveComments / StyleSourceCompiler
      ↓
CompiledStyleSheet
  ├─ CompiledRule
  ├─ SelectorStep[]
  ├─ declaraciones tipadas
  ├─ shorthands diferidos con var()
  └─ MediaCondition
      ↓
RuleIndex por id/clase/tipo/universal
      ↓ en cada mutación relevante
SelectorMatcher contra IAetherStyleNode
      ↓
CustomPropertyScope + VariableResolver
      ↓
CascadeEntry[] indexado por property ID
      ↓
AetherStyleBuilder
      ↓
AetherComputedStyle
      ↓
StyleDiff usando el impacto registrado
      ↓
layout / text / paint / compositor / hit-test de AetherUI
```

No se vuelve a parsear la hoja durante hover, cambios de clases o re-render de una vista. Solo el atributo inline se recompila cuando su texto cambia.

## 16. Integración recomendada con `.cshtml`

El generador de vistas debe continuar haciendo únicamente su trabajo nativo:

```cshtml
<div class="panel @(Expanded ? "expanded" : null)">
```

La actualización generada debe terminar llamando al método del nodo que cambia la clase. Ese método notifica al runtime:

```csharp
public void SetClass(string className, bool enabled)
{
    bool changed = enabled
        ? _classes.Add(className)
        : _classes.Remove(className);

    if (!changed)
        return;

    _styleCoordinator.Process(
        _styleRuntime.NotifyMutation(
            this,
            StyleMutation.Class(className)));
}
```

El generador no resuelve selectores ni cascada. La vista puede cambiar clases, atributos y estados libremente; el runtime los evalúa dinámicamente sobre el árbol actual.

## 17. Ciclo de vida recomendado

1. Crear y configurar `StylePropertyRegistry`.
2. Crear un `AetherStyleRuntime` por árbol/ventana aislada.
3. Cargar defaults, tema y hojas de vistas.
4. Construir el árbol visual.
5. Ejecutar `ResolveSubtree(root)`.
6. En cada mutación, modificar el nodo y llamar `NotifyMutation`.
7. En resize/cambio de tema, llamar `SetEnvironment`.
8. Al eliminar un subárbol, llamar `DetachSubtree`.
9. Al cerrar la ventana, disponer el runtime en su hilo propietario.

## 18. Decisiones deliberadas

- No existe un “elemento HTML” predeterminado; los nombres son tipos del árbol AetherUI.
- No existe CSSOM mutable; las hojas se añaden o eliminan como programas completos.
- No existe `getComputedStyle()` basado en strings; se obtiene `AetherComputedStyle` tipado.
- No existen eventos web; los pseudoestados vienen de controles AetherUI.
- No existe layout externo; las estructuras resueltas alimentan los algoritmos propios.
- No existe renderer externo; los colores, bordes, sombras y transforms alimentan el display list y compositor propios.
- No se intenta aceptar silenciosamente todo CSS. El registro expresa exactamente las capacidades reales del motor.

## 19. Límites actuales

Este paquete establece el runtime y un perfil inicial utilizable; no equivale todavía a la totalidad de CSS.

No incluye aún:

- Grid;
- animaciones, transiciones y `@keyframes`;
- pseudo-elementos;
- contenido generado;
- imágenes, gradientes o múltiples capas de background;
- filtros;
- variables registradas con tipado propio;
- selectores de alcance más avanzados;
- escritura vertical y propiedades lógicas;
- serialización de estilos.

Estas capacidades deben añadirse como módulos nativos cuando los subsistemas correspondientes existan en AetherUI. La arquitectura evita tener que modificar cascada, herencia, matching e invalidación para cada propiedad de una familia ya implementada, pero no simula una capacidad que el renderer o layout todavía no poseen.

## 20. Compilación

Desde la carpeta del proyecto:

```powershell
dotnet build -c Release
```

El proyecto no requiere restaurar dependencias de terceros.
