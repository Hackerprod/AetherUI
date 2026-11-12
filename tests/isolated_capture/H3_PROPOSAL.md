# H3 propuesta: canvas inicial reemplazado + bloque JSON inerte

Estado: propuesta para validación independiente, NO implementada. Base inspeccionada: aether-h2-diagnostic-isolated (H2). Sin Actions, SVG y texto pendientes.

## Elección y resultado

Prefiero cerrar una ruta real DOM → intrínsecos → flex/layout → paint → PNG para canvas inicial, acompañada por la clasificación correcta del manifiesto JSON. CSS completo contiene at-rules, variables, medidas, transformaciones y muestreo de animaciones; quitar su veto nominal no sería implementación. El canvas de FLEX-016 no requiere recursos gráficos: tiene bitmap inicial transparente, atributos 100×50 y decoraciones CSS. Actualmente Aether no reconoce su tamaño reemplazado y recorre su texto fallback. H3 debe corregir esa semántica, no sustituir el elemento por un div ni borrar fallback del DOM.

Resultado verificable: fixtures independientes con canvas real producen PNG con su caja/decoraciones y dejan transparente su contenido; métricas de layout/raster reales. El documento original conserva bytes/hash y sigue REJECTED por CSS, SVG y texto. Esto NO completa la captura original ni cambia css_eligibility=Unknown.

## Evidencia Aether

- native/src/dom/html_parser.cpp:461–466 conserva script como RAWTEXT.
- native/src/style_runtime/ua_stylesheet.cpp:10 ya aplica display:none a script; autor puede sobrescribirlo.
- native/src/layout/layout_engine.cpp:415–570 crea caja/sizing normal; no lee width/height de canvas. Intrínsecos 1681/1746 recorren hijos salvo controles opacos. Colección inline 1504/1623 sólo considera atómicos controles/inline-block. measure_child_intrinsic_width_parts 1939 y resolución flex requieren el mismo dato reemplazado.
- native/src/rendering/display_list_builder.cpp:481–510 pinta caja y luego hijos o control. No se debe marcar canvas is_atomic_control: dispararía dibujo de controles.
- tests/isolated_capture/main.cpp:preflight rechaza todos script/canvas y todo texto fuera de metadata. Suprimir ese rechazo sin implementar sizing generaría falsa imagen.

## Evidencia Chromium y diferencia que debe quedar explícita

https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/html/canvas/html_canvas_element.cc
CreateLayoutObject (388–399): con scripting habilitado crea LayoutHTMLCanvas; sin él cae a layout HTML y muestra fallback. OnWidthOrHeightAssigned (819–859) actualiza dimensiones y estado limpio, con defaults 300×150 (163–165).
https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/layout/layout_html_canvas.cc
Usa LayoutReplaced, no layout normal de los hijos.
https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/script/script_loader.cc
GetScriptTypeAtPrepare (428–456) distingue JS/module/importmap/speculationrules de tipo inerte; PrepareScript retorna para tipo inválido/no ejecutable (529–531).

No equiparar «no ejecutar JS» con «scripting deshabilitado» al comparar canvas. Contrato H3: snapshot de canvas habilitado en estado inicial, sin ejecución ni creación de contexto. El documento admitido carece de scripts ejecutables y de handlers; el renderer no incorpora un runtime JS. Futura referencia Chromium debe mantener scripting habilitado pero cargar únicamente esos mismos inputs inertes; si se deshabilita scripting globalmente, es otro modo y no hay paridad de fallback. Reportar canvas_mode=ENABLED_INITIAL_EMPTY; javascript_execution=NONE. No afirmar comparación ejecutada.

## Implementación propuesta

1. Definir un registro privado por operación de canvas inicial (IDs/nodos propios y dimensiones capturadas, no estilos inyectados). En H3 lo habilita explícitamente la entrada aislada; rutas públicas existentes mantienen modo previo hasta validación específica. Centralizar predicado/lectura dimensional y propiedad de reemplazado en core layout, no en PNG ni sólo CLI. El registro pertenece a engine/resultado durante operación, nunca global; cambios posteriores del DOM no se leen al pintar.
2. Atributos HTML width/height: implementar algoritmo HTML de entero no negativo (incluyendo prefijo numérico y espacios/signo permitidos), defaults por atributo inválido/ausente; 0 válido; overflow según límite Chromium antes de default. Separar límite de producto: dimensiones válidas demasiado grandes rechazan CANVAS_DIMENSION_LIMIT, jamás se recortan o convierten a default. No reservar bitmap: inicial transparente equivale a ninguna operación de contenido.
3. Ruta reemplazada compartida para intrínsecos y caja final: tamaño intrínseco contenido, decoración sumada exactamente una vez; razón intrínseca sólo si ambos ejes no cero; width/height CSS definidos prevalecen, un eje auto puede usar razón, dos auto usan intrínsecos. Soportar los casos del gate siguiente; no extender automáticamente a img/SVG/video. No se deriva tamaño de fallback, ni se llama medición de texto para él. La caja retiene tipo propio canvas, no atomic_control. Colección inline y flex reciben un ítem atómico; en H3 capture sólo admite flex acotado abajo, inline/baseline permanece rechazado si no se prueba.
4. Pintado conserva background/border/shadow/opacity/clip existentes y no recorre fallback ni inventa bitmap blanco. Fallback DOM permanece para selectores/inspección; recursos/scripts anidados siguen analizados por preflight para no esconder ejecución/red.
5. JSON: clasificar tipo application/json ASCII-insensitive y sin src para esta primera ruta; no ejecutar, no resolver, no interpretar instrucciones del manifiesto. Conservar raw text y DOM íntegros. Su exención de TEXT_RENDERER_UNAVAILABLE es provisional hasta post-style: exigir display:none computado para el script (o ancestro probado none); si autor lo hace visible, rechazar VISIBLE_SCRIPT_DATA_UNSUPPORTED. No ocultarlo forzadamente. No hace falta parsear JSON para establecer que su tipo no ejecuta; JSON inválido sigue siendo dato inerte, reportable pero no motor JS.
6. Reorganizar preflight para acumular candidatos inertes y validar su estilo antes del layout. CSS vetos actuales permanecen. Scripts de cualquier otro tipo/módulo/handlers/onload, contenido drawable y recursos externos siguen bloqueados. Detectar handlers ASCII-insensitive tras normalización del parser. No aceptar sólo por ausencia de <script>.

## Scope/gate de sizing H3

Admitir canvas directo en flex horizontal single-line, dirección LTR, align-items/start/center (no baseline/stretch), sin auto-márgenes, ni escritura vertical, ni posicionamiento fuera de flujo. Dimensiones CSS auto o px; min/max sin restricciones especiales en primera admisión. Flex-basis auto; flex-grow 0; contenedor con espacio suficiente para que no haya shrink (probar estado final; no presumir que flex-shrink:1 implica que realmente encoge). Captura de otros casos falla explícitamente CANVAS_LAYOUT_PROFILE_UNSUPPORTED antes de PNG. No aceptarlos con geometría aproximada. Estas restricciones permiten FLEX-016 y un conjunto no específico de geometrías; no hardcodear ID/test/tamaño 100×50.

Core helper debe ser general respecto a atributos/razón/box-sizing, pero la cobertura del CLI se limita a combinaciones demostradas. Si algún caso de este scope no puede realizarse en todas las rutas intrínsecas/finales, reducirlo explícitamente en propuesta revisada, no afirmar soporte.

## Tests y gates de aceptación

G1 tests unitarios parser de dimensiones: ausencia→300×150; 100×50; 0; negativo; whitespace/+; prefijo decimal/junk conforme HTML; overflow; límites válidos que rechazan por recurso. Test exacto del algoritmo contra especificación; no from_chars estricto improvisado.
G2 tests core tamaño/intrínsecos: defaults y 100×50, CSS dos ejes definidos, un auto con razón, border/padding content-box y border-box, eje cero. Invocar tanto intrínsecos como layout final. Fallback texto y descendants no producen cajas ni mediciones; no usar un mock que devuelve ancho cero para hacer pasar.
G3 tests flex: fila 420px/gap10/center con canvas100×50 y hermano geométrico sin texto; border2→104×54; verificar sibling offsets/centrado y ausencia de hijos fallback. Otros tamaños/defaults; caso de déficit debe rechazar si shrink no soportado. Baseline/stretch/column/vertical/transform/recursos fuera del perfil: rechazo, cero PNG.
G4 PNG: fondo contrastante, interior canvas transparente muestra fondo; CSS fondo/borde canvas correctos; opacidad/clip ya soportados sin pérdida de balance; medir píxeles y hashes estables. Conservar métricas reales y etiqueta AetherResolvedOutput.
G5 JSON: application/json (variantes ASCII), texto arbitrario no ejecutado y sin mediciones; script display:block rechaza; src, classic, module, importmap, handlers, canvas drawable rechazados. Fallback contiene script/img: no esconder veto de ejecución/red.
G6 regresiones H1/H2 y raster previas; parser/style/layout APIs sin modo H3 no alteradas; registro no se filtra entre capturas. Original conserva SHA256 eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837, inventario completo, y sigue sin PNG con bloqueadores CSS/SVG/texto (nunca silencio ni éxito global).
G7 revisión independiente propuesta PASS antes de código; revisión independiente implementación PASS con build/tests/fixtures reales. Sin Actions y sin declararse Chromium parity mientras no se ejecutó referencia real. Registrar tiempos de fases iniciadas únicamente.

## Revisión H3.1: precisiones vinculantes solicitadas por validación

Esta sección reemplaza cualquier formulación ambigua anterior acerca de scope, ABI o valores admitidos.

### ABI y activación

No modificar headers, structs, enums ni exportaciones públicos, ni el significado público de is_atomic_control. Sólo helpers/structs internos y miembros privados; la llamada privada de activación pertenece al build del harness. Modo apagado por defecto por instancia, nunca estado global. Resultado conserva los facts de su operación; desactivar el modo para operaciones posteriores no cambia resultados ya construidos. La nueva ruta no promete habilitación para bindings públicos.

### Valores dimensionales exactos

H3 admite min-width/min-height ausentes o 0px, max-width/max-height ausentes o none. El registro builtin actual inicializa los mínimos a 0, no a auto (builtin_properties.cpp:830–840); por ello NO afirmar cobertura de min:auto de Flexbox. Cualquier declaración explícita de min:auto u otro mínimo/máximo en un documento con canvas H3 produce rechazo conservador antes de render. La diferencia frente al mínimo automático de Chromium no afecta el perfil sin déficit ni shrink admitido: debe demostrarse con caso test, no extrapolarse.

width/height: auto o longitud px finita no negativa. Dos auto emplean el par intrínseco. Con un eje CSS definido, primero convertirlo a dimensión de contenido según box-sizing y decorations; después derivar el otro eje de la razón intrínseca. Finalmente sumar sus propias decorations una sola vez. Dos ejes definidos ignoran razón. box-sizing sólo content-box o border-box. Si no hay razón (algún intrínseco cero), dos auto conservan el par; un eje CSS definido y otro auto se rechaza en H3, en lugar de improvisar regla. Dos CSS definidos siguen admitidos aun con bitmap dimensional cero. CSS aspect-ratio explícito, incluido auto, se rechaza en H3: no se usa un campo inexistente ni se supone que ignorarlo equivale a resolverlo.

### Gate estructural y propiedades descartadas

El canvas admitido debe tener display inline, inline-block o block y ser hijo directo de un contenedor flex horizontal sin wrap; ninguno de sus ancestros ni el canvas puede tener display:none/contents en este primer perfil (rechazo, no error de dibujo ni borrado DOM). align-self:auto hereda align-items efectivo; sólo flex-start/center efectivos. align-self:flex-start/center explícitos también admitidos. Stretch, baseline, flex-end y demás se rechazan en H3. Parent flex-direction sólo row, no row-reverse. Margins numéricos, no auto. Definite container width y no déficit de espacio disponible después de sumar outer bases y gaps; verificar también geometría final sin reducción del canvas. Si no puede probarse, rechazar.

Aether no retiene writing-mode/direction en computed style, por lo que no basta inspeccionarlo. Para documentos H3 con canvas, gate adicional de declaraciones sobre todas las hojas e inline: rechazar cualquier declaración de writing-mode/direction/aspect-ratio y aliases lógicos o de tamaño no cubiertos (inline-size/block-size, min/max variantes, all), aun cuando parezca no aplicar al canvas. HTML dir sigue rechazado. Es rechazo conservador por documento, sin inventar matching/cascada para esas propiedades. Detectar nombres en contexto de declaración con lexer y decodificación de escapes, no búsqueda de substrings (strings/comentarios no cuentan). CSS anidado/estructura que el gate no pueda clasificar dentro del perfil debe rechazar por desconocido. H1 ya rechaza at-rules/functions; no relajar esa frontera. Fuente/traversal sigue teniendo cobertura global no probada: mantener Unknown, no vender este gate como parser coverage.

Test específico: writing-mode/direction/aspect-ratio y aliases con escapes/case/comentarios; que no pasen porque el core los descarta. display:none/contents y todos los align-self fuera del perfil deben fallar explícitamente sin PNG. Cuando se implemente semántica más amplia, requerirá nueva propuesta.

### Límites/performance y aislamiento

Máximo 256 canvas por operación H3, dimensiones intrínsecas aceptadas por HTML hasta 8192 por eje antes del límite de producto; dimensiones mayores válidas para HTML rechazan sin reserva de bitmap. Registro O(n) con budget explícito, nunca W×H por canvas. Límite agregado de representación geométrica/trabajo y raster existente se mantiene; no elevarlo para hacer pasar tests. No reservar/copiar buffer de canvas transparente. No imponer comparación temporal frágil de microsegundos.

Agregar tests deterministas con 1/16/256 canvas (pequeños y 8192² transparentes, CSS tamaño pequeño en el perfil) que comprueben conteo de entries/bytes del registro, cero bytes reservados para bitmaps de canvas, cero comandos de contenido canvas y ausencia de trabajo proporcional al área intrínseca. 257 rechaza antes de layout/PNG. Medir CPU/RSS/wall de cada ejecución como evidencia sin convertir ruido en PASS de velocidad; límites OS del harness siguen vigentes. Guardar las mediciones en report.log.

Test de desactivación en mismo proceso con nuevas operaciones: modo off conserva comportamiento previo; on activa semántica reemplazada; siguiente off no hereda registro ni suppression de fallback. Dos instancias una on/otra off independientes. Resultado on ya producido permanece estable después de off y después de otra operación (facts propios); prueba de lifetime sin IDs/handles globales. Comparar los tests públicos existentes sin activación, y revisar diff que native/include y lista de símbolos públicos no cambiaron.
