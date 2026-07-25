# Changelog

Los cambios relevantes se documentan en este archivo.

## 0.1.0-alpha.1 — 2026-07-24

### Incluido

- Motor DOM/HTML propio para el perfil inicial de AetherUI.
- Parser CSS, selectores, cascada, herencia y estilos computados.
- Layout block, inline, controles atómicos y Flexbox básico en fila/columna.
- Display list retenida, clipping, bordes redondeados, texto y caret básico.
- Host Win32 con DPI por monitor, mouse, teclado, foco y cursores.
- Backend Direct2D/DirectWrite con recreación del render target después de fallo de dispositivo.
- Generador incremental de vistas `.cshtml` con `@code`, expresiones, atributos dinámicos y eventos C#.
- Formularios básicos con `required`, `maxlength`, submit y bubbling de eventos.
- Demo `RegistrationApp`, pruebas y scripts de build/pack.

### Restricciones conocidas

- No existe compatibilidad HTML/CSS completa.
- No hay scroll, Grid, posicionamiento, imágenes, SVG, animaciones, IME, accesibilidad ni selección avanzada de texto.
- El parser de vistas no implementa todos los constructos de ASP.NET Razor.
- El backend incluido es únicamente Windows.
