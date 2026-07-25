# Política de seguridad

## Alcance

AetherUI 0.1 es un runtime de UI local. No contiene cliente HTTP, navegación, cookies, almacenamiento web, JavaScript, plugins ni carga remota de recursos. El principal límite de confianza está entre markup controlado por la aplicación y datos no confiables insertados en una vista.

## Reglas de uso seguro

- Inserta datos externos mediante `@Expresion`, `@(expresion)` o atributos dinámicos; `MarkupWriter` codifica esos valores como HTML.
- No pases entrada externa directamente a `WriteLiteral`.
- No trates el parser HTML inicial como sanitizador de HTML hostil.
- No expongas tokens internos `aether:*` fuera del documento generado.
- Mantén .NET, Vortice.Windows y Roslyn actualizados dentro de la línea de versiones probada.
- Distribuye aplicaciones con permisos de usuario normal; el manifiesto de la demo utiliza `asInvoker`.

## Reporte

No publiques detalles explotables en un issue público. Entrega una descripción, versión afectada, reproducción mínima e impacto mediante el canal privado del mantenedor o una advertencia privada de seguridad del repositorio donde se hospede el proyecto.

## Versiones

Durante la fase alpha solo se mantiene la versión más reciente. No se prometen parches retroactivos para builds alpha anteriores.
