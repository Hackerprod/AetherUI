# Contribuir a AetherUI

## Principios

1. El motor no debe depender de Chromium, WebView, WPF, WinForms ni de un navegador embebido.
2. HTML, CSS, DOM, layout, eventos y display list deben permanecer desacoplados del backend gráfico.
3. Los cambios de compatibilidad deben incluir pruebas y actualizar `docs/supported-profile.md`.
4. La lógica de vistas se compila durante el build; no se añadirá evaluación arbitraria de C# en runtime.
5. Las API públicas deben conservar nullable annotations y validación de argumentos.

## Flujo

```powershell
pwsh ./scripts/verify.ps1
```

Antes de proponer un cambio:

- añade una prueba de regresión;
- compila en Release;
- ejecuta las pruebas;
- verifica la demo en Windows con al menos dos escalas DPI;
- actualiza documentación y changelog cuando corresponda.

## Áreas prioritarias

- shaping, caret, selección e IME;
- scrolling y clipping anidado;
- invalidación incremental por propiedad;
- accesibilidad UI Automation;
- suite de conformidad y golden screenshots;
- backend de compositor basado en swap chain/device context;
- perfiles Flexbox y Grid más completos.
