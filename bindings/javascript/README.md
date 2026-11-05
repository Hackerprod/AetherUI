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
styles.resolveSubtree(document.documentElement);
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

## ABI y cadenas UTF-8

El binding conoce exactamente el perfil **0.34.0**. Comprueba versión, capacidades y
el tamaño exacto de `ComputedStyle` antes de crear recursos. `expectedAbi` sólo puede
restringir ese perfil, no habilitar otro major, minor o patch sin adaptar el espejo.
El espejo incluye `whiteSpace`, `zIndex`/`zIndexKind` y los cuatro valores de transición.
Las duraciones y demoras se transportan en segundos; son datos de estilo, no una API
JavaScript de animaciones. El parser nativo actual rechaza demoras CSS negativas, aunque
el binding sí puede transportar ese float desde la ABI.

Los getters de texto, tag y atributos retornan copias JavaScript `string | null`; el
consumidor no debe liberarlas. Internamente se conserva el puntero raw que asignó Aether,
se copian sus bytes UTF-8 y se llama exactamente una vez a `aether_string_free`, incluso
si el decoder lanza. No se pasa un string JavaScript al destructor nativo.

Los strings en paint, estilos y callbacks son prestados durante la llamada nativa: el
binding los convierte y copia antes de liberar el comando/estilo o retornar del callback.
Los snapshots JavaScript conservan sus strings y arrays después de destruir los owners.
`PaintCommand.raw` es sólo un snapshot de bytes para diagnóstico: puede contener direcciones
nativas ya caducadas. **Nunca vuelvas a decodificar sus punteros ni lo envíes como comando nativo.**
La compatibilidad del transporte de estilos no acredita que el renderer JS implemente todos
los tags de composición de la ABI 0.34.0; los tags no reconocidos siguen siendo `unknown`.

## Contrato de mutación DOM y vistas prestadas

`getElementById(id)` devuelve la primera coincidencia conectada en preorden del árbol
actual, también con IDs duplicados y después de mover, desconectar o renombrar nodos.
La comparación de IDs es exacta (sin normalizar case, espacios ni Unicode) y no depende
de visibilidad. Las vistas son prestadas: dos consultas pueden crear wrappers JS distintos
para el mismo nodo; para comparar identidad nativa usa `ffi.address(view.handle)`.
Un ID vacío o no string produce `TypeError`; un documento disposed o una vista de una
generación anterior al reparseo falla antes de acceder a su handle nativo.

`appendChild` y `removeChild` delegan la integridad estructural a la ABI nativa.
Con handles vivos, un bosque DOM inicialmente coherente y acceso serializado:

- `appendChild` rechaza self-append y colocar un ancestro bajo su descendiente,
  también en subárboles desconectados.
- Ambas operaciones rechazan endpoints sintéticos de layout: wrappers anónimos,
  pseudoelementos y descendientes de nodos generados. No redirigen la operación al host.
- Los rechazos de integridad producen `NativeStatusError` con `status === -2` y
  `operation === "aether_node_append_child"` o `"aether_node_remove_child"`.
  Conservan padres, hijos y orden, contenido, IDs y versión del documento.
- Mover nodos authored hacia arriba o entre ramas sigue permitido. Reinsertar en el
  mismo padre mueve al final sin duplicar. Los hosts con pseudoelementos y los textos
  authored referenciados por wrappers siguen siendo mutables.

JavaScript comprueba por separado mismo documento, documento no disposed y generación
vigente tras `document.parse()`. Esos errores no equivalen a un estado nativo `-2`.
Esta garantía de rechazo no es rollback universal ante OOM, no repara árboles previamente
corruptos y no resuelve propiedad de nodos desconectados ni adopción nativa entre documentos.

`LayoutBox.element` puede devolver un nodo sintético prestado, no necesariamente un nodo
DOM authored. Mantén vivos documento, runtime/mapa de estilos y resultado de layout mientras
uses sus vistas. En particular, un wrapper pertenece al resultado de layout: **no hay garantía
de vida ni comprobación JS completa después de destruir ese resultado**, aunque su documento
siga vivo. La protección estructural no hace `NodeView` read-only ni protege sus otros setters.
Los resultados previos son snapshots, no vistas actualizadas automáticamente tras mutaciones.
Destruye primero display lists/resultados, después motores/mapas/runtime y finalmente documento.

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

`npm test` cubre negociación ABI, descubrimiento de biblioteca y contratos de escritorio sin ventana.
La regresión DOM nativa es opt-in explícita y aparece como omitida sin `AETHERUI_TEST_NATIVE=1`:

```sh
AETHERUI_TEST_NATIVE=1 AETHERUI_TEST_LIBRARY=/ruta/libaether_core.so npm test
# Alternativa: AETHERUI_TEST_NATIVE=1 AETHERUI_INSTALL_PREFIX=/ruta/instalada npm test
```

El caso aislado `id-order` usa la biblioteca real y compara direcciones con expectativas
obtenidas de navegación de hijos. Cubre duplicados, movimientos, IDs modificados,
desconexión/reconexión, reparseo, búsquedas exactas y errores de contrato. Una vez activado
el opt-in, falta de Koffi, biblioteca o ABI compatible es un fallo, no una omisión.

La suite opt-in incluye contraste de los 55 offsets, tamaño y alineación de ComputedStyle
contra un probe compilado del header C, copias profundas repetidas, y el callback real de
medición. Comprueba que métricas no cero cambian el ancho del fragmento de texto público,
además del fallback de excepciones. Los siete campos nuevos se verifican con valores distintos.
También prueba los seis getters owned y la emisión nativa de los tags paint 4, 5, 9 y 10,
con Unicode, URL, callbacks y snapshots conservados tras destruir sus propietarios.

Las pruebas de contrato/ownership usan Koffi real y un shim C con contadores de allocations,
frees y liberaciones inválidas. Pueden ejecutarse sin cargar Aether:

```sh
AETHERUI_TEST_FFI=1 node --test test/ffi-strings.test.mjs
```

Estos probes requieren el checkout con `native/include`, un compilador C/C++ (`CC`/`CXX`,
por defecto `cc`/`c++`) y Koffi instalado. No son una prueba de una DLL Windows ni del renderer.
La validación disponible es Linux x64, Node 24.22.1 y Koffi 2.16.3, con ASan+UBSan sobre
la biblioteca nativa para DOM, estilos, callbacks y strings. LeakSanitizer no se ejecutó en
esas pruebas; los contadores del shim no demuestran ausencia de fugas en todo el producto.

Con opt-in, una biblioteca ausente/incompatible o `koffi` ausente **falla**, no se sustituye por
mocks ni se omite silenciosamente. La regresión ejecuta procesos aislados y acotados para detectar
sin colgar el runner un core antiguo que acepte ciclos. Verifica estado y operación exactos,
snapshots estructurales, IDs, versión, layout y display list, usando wrappers/pseudoelementos
obtenidos por layout real y propietarios vivos. Incluye pruebas independientes de cross-document,
vistas obsoletas y documento disposed. Un caso fallido sale sin destruir un grafo posiblemente
corrupto; los casos correctos destruyen sus propietarios en orden. Esta prueba no demuestra ausencia
de fugas ni seguridad de handles tras liberar su propietario.

La prueba de ventana/renderer debe ejecutarse en Windows con una DLL `aether_core` compatible,
porque el backend requiere Win32/GDI real.
