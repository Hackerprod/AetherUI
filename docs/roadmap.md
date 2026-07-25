# Limitaciones y roadmap

## Clasificación actual

`0.1.0-alpha.1` es una **base funcional y extensible**, no un motor web completo ni un framework general de producción. La demo permite validar el flujo end-to-end, pero una aplicación comercial accesible y multilingüe requiere completar varias capas.

## Bloqueadores antes de una beta de producción

### Texto y entrada

- caret con posición arbitraria;
- selección, clipboard, undo/redo;
- Unicode grapheme clusters;
- IME/TSF y composition events;
- password, textarea y edición multilínea;
- bidi y line breaking con tests exhaustivos.

### Accesibilidad

- árbol semántico separado del layout;
- bridge UI Automation en Windows;
- nombres, roles, estados y acciones;
- navegación por teclado consistente;
- contrast/focus tests.

### Layout y viewport

- scroll containers y wheel;
- clipping/scroll anidado;
- `position`, z-order y stacking contexts;
- Flexbox con grow/shrink/basis/wrap;
- Grid;
- imágenes y reemplazados;
- invalidación incremental de style/layout/paint/composite.

### Render y composición

- swap chain/device context moderno en lugar de depender únicamente de `ID2D1HwndRenderTarget`;
- dirty regions;
- caché de geometrías, glyph runs e imágenes;
- capas, opacity groups, transforms y animaciones;
- telemetría de frames y presupuesto de memoria;
- golden screenshots en distintas GPU/DPI.

### Plataforma

- clipboard, drag/drop, dialogs y cursores personalizados;
- ciclo de vida de múltiples ventanas;
- dispatcher y trabajo desde otros hilos;
- macOS/Linux;
- empaquetado y firma de aplicaciones.

### Compatibilidad

- parser HTML más cercano a WHATWG;
- expansión correcta de shorthands en la cascada;
- custom properties y `calc()`;
- media queries;
- suite seleccionada de Web Platform Tests;
- perfil versionado con métricas públicas de conformidad.

## Roadmap propuesto

### 0.2 — Interacción de escritorio

- scroll vertical;
- wheel;
- clipboard;
- selección/caret básico;
- controles checkbox y textarea;
- invalidación separada de markup/layout/paint.

### 0.3 — Layout moderno

- Flexbox completo del perfil elegido;
- positioning;
- stacking contexts;
- transforms y opacity layers;
- imágenes locales.

### 0.4 — Accesibilidad y texto internacional

- UI Automation;
- IME/TSF;
- bidi y graphemes;
- navegación de teclado y focus order formal.

### 0.5 — Tooling

- inspector DOM/CSS/layout;
- hot reload de vistas;
- diagnósticos CSS;
- golden tests;
- profiler de frames.

### 1.0 — Perfil estable

Requiere API pública congelada, suite de conformidad, compatibilidad de paquetes, documentación de migración, revisión de seguridad y aplicaciones reales funcionando sin forks del motor.

## Decisiones que no forman parte del roadmap

- incorporar Chromium o WebView como fallback;
- ejecutar C# arbitrario recibido durante runtime;
- convertir AetherUI en navegador general con navegación remota;
- depender de WinForms/WPF para controles visuales;
- afirmar compatibilidad HTML/CSS total sin resultados de conformidad.
