# AetherUI JavaScript/TypeScript binding

Binding Node.js ESM de AetherUI para dos escenarios:

- `@aetherui/javascript`: acceso headless al ABI C instalado (DOM, CSS, layout, hit-testing y display list).
- `@aetherui/javascript/desktop`: host de escritorio Win32 con ciclo de pintura, entrada, foco, formularios,
  scroll y renderer GDI. El renderer es reemplazable por Direct2D, Skia u otro backend.

La capa JavaScript no toca el core C/C++: solo carga la biblioteca instalada con `koffi`, valida ABI/capacidades,
usa el tamaño runtime del display list y aplica ownership determinista (`dispose()`/`Symbol.dispose`).

## Instalación y uso headless

```sh
cd bindings/javascript
npm install
```

```js
import { createAether } from "@aetherui/javascript";

const aether = createAether({
  installPrefix: process.env.AETHERUI_INSTALL_PREFIX,
});

using document = aether.document.parse("<main id='app'>Hello</main>");
using styles = aether.styleRuntime.create({
  environment: { viewportWidth: 800, viewportHeight: 600 },
});
styles.addStylesheet("#app { display: block; color: #202124; }");
using styleMap = styles.createResolvedStyleMap(document);
using layout = aether.layout.create((text) => ({
  width: text.length * 8,
  height: 16,
  baseline: 12,
}));
using result = layout.layout(document, styleMap, 800, 600);
console.log(result.root?.borderBox);
```

La biblioteca nativa se busca en `AETHERUI_INSTALL_PREFIX/bin/aether_core.dll` en Windows,
`lib/libaether_core.so` en Linux y `lib/libaether_core.dylib` en macOS. También se puede pasar
`libraryPath` explícitamente.

## Aplicación de escritorio Win32

El entrypoint de una aplicación puede ser tan pequeño como este:

```js
import { runDesktop } from "@aetherui/javascript/desktop";

runDesktop({
  render: (host) => `
    <main id="window">
      <h1>AetherUI</h1>
      <input id="name" value="" placeholder="Nombre" />
      <button id="save" onclick="save">Guardar</button>
    </main>
  `,
  handlers: {
    save(event) {
      console.log("click", event.target?.getAttribute("id"));
    },
  },
}, {
  windowOptions: { title: "AetherUI", width: 960, height: 640 },
  aetherOptions: { installPrefix: process.env.AETHERUI_INSTALL_PREFIX },
});
```

Para componentes con estado, `createDesktopComponent()` ofrece `stateHasChanged()`, que invalida
el frame de forma segura. Los handlers se conectan desde atributos `onclick`, `oninput`, `onsubmit`,
etc.; los eventos burbujean por los ancestros y soportan `preventDefault()` y `stopPropagation()`.

```js
import { createDesktopComponent, runDesktop } from "@aetherui/javascript/desktop";

let count = 0;
const component = createDesktopComponent({
  render: (host) => `<button onclick="increment">Clicks: ${count}</button>`,
  handlers: {
    increment() {
      count += 1;
      component.stateHasChanged();
    },
  },
});
runDesktop(component);
```

La ventana es una capa opcional. El core sigue sin conocer HWND, mensajes Win32, rasterización o
text measurement; eso permite sustituir `Win32Window` y `GdiRenderBackend` sin modificar el ABI.

## Ejecutable portable de Windows

El empaquetador usa Node SEA para generar un `.exe` y copia junto a él el runtime Node, el paquete JS,
`koffi` y `aether_core.dll`. El resultado es una carpeta portable: se distribuye completa, sin instalar
Node ni AetherUI globalmente.

Requisitos de build: Windows, Node >= 20, `npm install`, una DLL nativa compilada/instalada y `postject`.

```sh
npm install
npm run package:win -- --entry C:\\src\\my-app\\main.mjs --app-dir C:\\src\\my-app --out C:\\dist\\my-app --name MyApp --native C:\\path\\to\\aether_core.dll
```

El ejecutable queda en `C:\\dist\\my-app\\MyApp.exe`; `resources/app` contiene el código y dependencias
de la aplicación y `resources/native/bin/aether_core.dll` contiene el core. `--entry` debe estar dentro de
`--app-dir`. El empaquetador rechaza rutas que causarían borrar el directorio fuente.

## Verificación

```sh
npm run check
npm test
npm pack --dry-run
```

`npm test` cubre negociación ABI y descubrimiento de la biblioteca. La prueba de ventana/renderer debe
ejecutarse en Windows con una DLL `aether_core` compatible, porque el backend requiere Win32/GDI real.
