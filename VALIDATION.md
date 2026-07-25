# Estado de validación — 0.1.0-alpha.1

## Resultado disponible en este paquete

La validación estática fue ejecutada correctamente el **2026-07-25 01:50 UTC** sobre Debian GNU/Linux 13 con Python 3.13.5.

Comprobaciones superadas:

- archivos obligatorios y estructura de la solución;
- UTF-8, saltos de línea, espacios finales y marcadores de conflicto;
- sintaxis estructural de XML, JSON y workflow YAML;
- rutas de la solución, `ProjectReference` y versiones centralizadas de paquetes;
- enlaces Markdown relativos;
- balance léxico de delimitadores en C#, omitiendo comentarios y literales;
- cierre de bloques `@code` en las vistas `.cshtml`;
- ausencia de referencias a Chromium, WebView2, WinForms y WPF en proyectos/código;
- SDK, versión, paquete del generador y construcciones obligatorias de la demo.

Comando reproducible:

```text
python scripts/static_validate.py
```

Resultado:

```text
PASS  Validación estática completada (8 grupos de comprobaciones).
```

## Validación que no pudo ejecutarse en el entorno de empaquetado

El entorno de creación no contiene el .NET SDK y no dispone de acceso de red desde el contenedor para restaurarlo. Por ello **no se ejecutaron localmente**:

- `dotnet restore`;
- compilación Release;
- pruebas C# ejecutables;
- creación de paquetes NuGet;
- publicación de la demo;
- smoke test de la ventana Win32/Direct2D en Windows.

No debe interpretarse la validación estática como sustituto de esas pruebas. La API de Direct2D/DirectWrite empleada fue contrastada contra documentación de Microsoft, ejemplos de Vortice.Windows y metadatos actuales de NuGet, pero la compatibilidad binaria final debe confirmarse mediante el pipeline Windows incluido.

## Verificación obligatoria en Windows

Con .NET SDK 10.0.302 instalado:

```powershell
pwsh ./scripts/verify.ps1
```

Ese script restaura, compila, ejecuta las pruebas y genera los tres paquetes NuGet. Después debe abrirse manualmente `RegistrationApp` y probarse al menos con escalas DPI de 100%, 150% y 200%.

## Criterio de entrega

Esta versión es una **alpha de ingeniería con arquitectura de producción**, no una declaración de que el motor ya sea apto como framework general. Los bloqueadores para beta están enumerados en [`docs/roadmap.md`](docs/roadmap.md), especialmente IME/TSF, accesibilidad UI Automation, scroll, selección de texto, invalidación incremental y pruebas de conformidad.
