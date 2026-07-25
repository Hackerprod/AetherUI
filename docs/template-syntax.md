# Sintaxis de vistas `.cshtml`

AetherUI utiliza una sintaxis deliberadamente pequeña, inspirada en Razor pero compilada por un generador propio. No depende de ASP.NET Core ni de `Microsoft.AspNetCore.Razor`.

## Directivas

Las directivas deben ocupar una línea propia, opcionalmente indentada.

### `@namespace`

```cshtml
@namespace MiAplicacion.Views
```

Define el namespace de la clase generada. Si se omite, se utiliza `RootNamespace` del proyecto.

### `@class`

```cshtml
@class MainView
```

Define el nombre de la clase. Si se omite, se deriva del nombre del archivo.

### `@inherits`

```cshtml
@inherits MiComponenteBase
```

Cambia el tipo base. Debe heredar, directa o indirectamente, de `UiComponent` y proporcionar el contrato esperado por el código generado.

### `@using`

```cshtml
@using MiAplicacion.Servicios
@using Alias = MiAplicacion.Modelos
```

Se copia como directiva `using` al archivo generado. `System`, `AetherUI.Events` y `AetherUI.Runtime` se incluyen automáticamente.

### `@code`

```cshtml
@code {
    private int Contador { get; set; }

    private void Incrementar()
    {
        Contador++;
        StateHasChanged();
    }
}
```

El contenido se inserta dentro de la clase parcial generada. Se permiten campos, propiedades, métodos, tipos anidados y demás miembros válidos de C#.

Puede haber varios bloques `@code`. El scanner reconoce comentarios, strings normales, verbatim y raw strings al localizar la llave final.

## Expresiones

### Implícita

```cshtml
<p>@Nombre</p>
<p>@Modelo.Usuario.Nombre</p>
```

El perfil implícito admite identificadores, acceso por punto y operadores nullable simples `?`/`!`.

### Explícita

```cshtml
<p>@(Nombre.ToUpperInvariant())</p>
<p>@($"Total: {Precio:N2}")</p>
```

Utiliza `@(...)` para llamadas, operadores, interpolación, índices, condicionales y expresiones complejas.

### Escape de `@`

```cshtml
<p>@@usuario</p>
```

Produce `@usuario` literal. En CSS, una regla como `@media` debe escribirse `@@media` mientras esa construcción no forme parte del perfil CSS.

## Atributos dinámicos

El valor completo del atributo debe ser una expresión:

```cshtml
<input value="@Nombre">
<div class="@(Activo ? "activo" : "inactivo")"></div>
<button disabled="@EstaDeshabilitado">Guardar</button>
```

Semántica de `WriteAttribute`:

- `null` o `false`: omite el atributo;
- `true`: escribe un atributo booleano sin valor;
- otros valores: conversión invariant y HTML encoding.

La interpolación parcial dentro de un atributo no está soportada en 0.1:

```cshtml
<!-- No soportado -->
<div class="item-@Id"></div>

<!-- Equivalente soportado -->
<div class="@($"item-{Id}")"></div>
```

## Eventos

Un atributo cuyo nombre empieza por `on` y cuyo valor completo es dinámico se compila como evento:

```cshtml
<button onclick="@Guardar">Guardar</button>
<input oninput="@AlEscribir">
```

Manejador con evento:

```csharp
private void Guardar(UiEvent e)
{
    e.PreventDefault();
    StateHasChanged();
}
```

Manejador sin parámetros:

```csharp
private void Guardar()
{
    StateHasChanged();
}
```

Ambas formas se resuelven en compilación mediante sobrecargas de `WriteEventAttribute`.

## Clase parcial adicional

La vista genera una clase `partial`. Es posible separar lógica avanzada:

```csharp
namespace MiAplicacion.Views;

public sealed partial class MainView
{
    private readonly Servicio _servicio = new();
}
```

El archivo `.cshtml` y el `.cs` deben producir la misma combinación de namespace/nombre.

## Construcciones no implementadas

En 0.1 no se procesan:

- `@if`, `@for`, `@foreach`, `@switch` directamente en markup;
- bloques `@{ ... }`;
- helpers, layouts, sections, partials o tag helpers de ASP.NET Razor;
- componentes anidados por nombre de tag;
- atributos con múltiples fragmentos literales/dinámicos;
- expresiones asíncronas en el pipeline de render;
- inyección de dependencias mediante `@inject`.

Para contenido condicional puede exponerse una propiedad y usar CSS/atributos dinámicos, o construir el valor mediante una expresión. El roadmap contempla operaciones de control compiladas sin introducir un runtime Razor completo.
