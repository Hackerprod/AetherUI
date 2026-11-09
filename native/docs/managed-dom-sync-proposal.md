# D1: sincronización DOM in-place y recuperación vigente

**Estado: DRAFT v2, decisiones propuestas para revisión independiente; NO PASS y NO implementación.**
Fecha: 2026-11-09. Base inspeccionada: `b317fc8b1d9b7b9d666a4cf0a37a8d18defbc957`, checkout C3b. No se ha editado producto. Este documento sustituye la dirección de serializer/rebind rechazada; no afirma que los contratos nuevos ya existan.

## 1. Decisión central propuesta

Usar un puente vivo con escrituras **linealizadas**, no reconstruir markup ni mantener dos árboles con merge retrospectivo ambiguo.

- Una mutación pública de un nodo enlazado empieza comprobando el epoch nativo. Si native avanzó, importa primero sus entradas actuales mediante un snapshot exacto con ownership. Después aplica solamente la operación pedida, no una copia completa de los valores managed.
- Preparar cambios nativos y gestionados antes de tocar ninguno. Validar sus bases; commit nativo in-place sin callbacks ni asignaciones; publicación managed por swaps/escrituras no fallibles antes de permitir usuario. La ventana entre ambos commits es inaccesible por owner-thread, no por esperanza de que no haya callbacks.
- Los getters públicos de entradas enlazadas comprueban coherencia antes de entregar datos. La comprobación limpia es O(1); sólo un avance externo nativo exige importar. Así `element.State |= Hover` lee el State de R2 antes del setter. El host utiliza accesores internos puros durante su tramo de publicación: `Document.Version`, `InputVersion`, tokens e identidades nunca importan ni reservan memoria.
- No resolver estilo, pintar, llamar RenderRequested o ejecutar handlers durante sincronización DOM. La mutación sólo produce invalidación/Bnext. El mismo runtime nativo, los mismos nodos supervivientes y el mismo runtime de transiciones permanecen vivos.
- B/Bnext son pendientes de **resolución**, no autoridad para reescribir entradas. La recuperación adquiere el DOM vivo; jamás reproduce el array State capturado en B.

Esta dirección fue consultada con el worker D1 y el revisor. Ambos indicaron que no hay impedimento fundamental si se especifican staging bilateral, lifetime y callbacks. No constituye su aprobación.

## 2. Evidencia concreta y defectos de la base

- `bindings/dotnet/src/AetherUI.Core/Dom/Node.cs`: Append/Insert hacen Remove previo y cambian Parent antes de reservar List; TextContent hace Clear y Append. Una notificación síncrona externa observaría una mutación incompleta. El tipo público admite textos, comentarios y formas que un parser HTML normalizaría.
- `DocumentNode.cs`: Version cubre árbol/atributos/texto. La base C3b no tiene notificación al host. El corte D1 acaba de añadir `InputVersion` puro + notificación interna, incluyendo State/RuntimeValue; el puente propuesto debe conservar ese contrato y no adoptar un watermark global después de callbacks.
- `ElementNode.cs`: State y RuntimeValue eran auto-properties; Attributes es Dictionary OrdinalIgnoreCase y nombres usan ToLowerInvariant. Esto no coincide exactamente con la identidad ASCII de atributos nativos.
- `DomInterop.cs`: sólo mapea elementos; CopyChildren no registra identidad de Text/Comment; SyncRuntimeValues escribe `value` sólo si RuntimeValue no es null. No conserva atributo base separado.
- `NativeDomDocument.cs`: lectura de null se convierte en atributo ausente/vacío, tag div o texto vacío. No sirve para certificar un snapshot en presencia de malloc OOM.
- `native/src/dom/node.cpp`: append/remove tienen validación de ciclos/endpoints sintéticos, pero no ownership cross-document ni rollback OOM completo. El journal identifica tokens auténticos, no retiene storage de nodos.
- `native/src/dom/document_node.cpp`, `html_parser.cpp`, `element_node.cpp`: create, parse y set_attribute pueden dejar escapar bad_alloc. Parse destruye árbol antes de terminar; set_attribute toca epoch antes de escrituras que pueden fallar parcialmente.
- `UiDocumentHost.Pointer.cs`: UpdateHoverTarget retorna cuando target es el mismo aunque hayan cambiado sus ancestros. Focus usa estilo/layout potencialmente viejos y ejecuta blur/reentry.
- `UiDocumentHost.Scroll.cs`: CancelScrollbarDrag sólo borra drag. `_scrollbarPointerCaptured` debe sobrevivir hasta release. Scroll offsets por wrapper pueden conservarse; índices de boxes deben rehacerse después del layout válido.
- `UiComponent.RenderMarkup` borra `_events` antes de BuildRenderTree/parse. No hay rollback de registry por conservar el DOM antiguo.
- `StyleRuntimeInterop.Load` cambia sheets/cache/element index en varias operaciones; no sirve para un commit transaccional.
- `transition_runtime.cpp`: active/finished se indexan por raw node; Observe reserva y modifica vectores. Preparar marshaling .NET no prepara una transición nativa atómica.

## 3. Contrato mínimo de mutación gestionada

Todas las operaciones built-in pasan por una sola entrada interna. No se implementa Append mediante un Remove público seguido por otro Append, ni TextContent mediante dos mutaciones visibles.

1. Verificar owner-thread y tipo built-in sin invocar TextContent virtual de una subclase desconocida.
2. Reconciliar las entradas nativas actuales, una sola vez al comienzo de la operación compuesta. No hay getters con import dentro de staging/commit.
3. Validar la operación completa contra esa base: parentesco, ciclos, índice después de extracción, documentos afectados, attachment y tokens.
4. Reservar colecciones/strings managed, payloads/child vectors nativos, nuevas identidades, mapas y bookkeeping de invalidación. No cambiar Parent/Children, Document.Version, next_style_node_id o registry aún.
5. Revalidar tokens de todos los documentos y bindings implicados. Si cambiaron, abortar sin commit; no aplicar un diff sobre una base distinta.
6. Publicar por swaps y escrituras no fallibles sin código externo. Version/InputVersion y HostInputSerial avanzan por la mutación confirmada exacta. Bnext obtiene su invalidación. No borrar dirty preexistente.
7. Retirar staging/recursos antiguos después de salir del tramo no fallible. Entregar notificaciones externas únicamente en punto seguro.

Operaciones: Set/RemoveAttribute, State, RuntimeValue, TextNode.TextContent, Append/Insert/Remove/ClearChildren y Node.TextContent como reemplazo atómico. CommentNode.Text actual es inmutable; su contenido se captura mediante Text, no TextContent heredado. Los cambios detached permanecen legales; al insertar se transfiere el árbol typed directamente, nunca mediante HTML.

No-op de State/RuntimeValue conserva el comportamiento de InputVersion del corte D1. Mutaciones tree/attribute mantienen la política Version existente salvo correcciones de atomicidad que deben probarse explícitamente.

## 4. Nuevo contrato nativo necesario

Nombres orientativos, ABI aditiva versionada:

- `read_dom_token`: identidad/lifetime, input epoch, structure epoch y propietario/owner-thread. Puro, no asigna, no callback.
- `acquire_dom_snapshot`: snapshot immutable exacto de nodos/orden, atributos, texto, estado, runtime override y attachment; incluye nodos detached todavía conocidos por el binding. Datos con longitud explícita y status, nunca null usado como vacío. Retiene tanto identidad como storage/datos necesarios.
- `prepare_dom_edit`: operación typed o lote compuesto contra token(s), devuelve transacción dueña del staging y de los handles nuevos aún no publicados. Prepara payloads, child vectors, ownership, id index y resultados para el mapping managed.
- `commit_dom_edit`: revalida base y publica sólo swaps/escrituras no fallibles. Sin callback, logging, comparación de strings, asignaciones o liberación que ejecute usuario. Devuelve token exacto producido.
- `destroy_dom_edit/snapshot`: liberación segura de staging; los pins prestados sobreviven al transaction.

Modificar también los mutadores legacy usados desde callbacks para entrar en la misma implementación fuerte. No basta que sólo el setter managed sea seguro mientras R2 utiliza parse/set_attribute que corrompen parcialmente o lanzan a través de P/Invoke. Conservan firmas y conteos de epochs de éxito; ante OOM dejan inputs sin cambios y devuelven error. Los getters antiguos pueden mantener su contrato ambiguo, pero el puente no los utiliza para certificar datos.

El journal de C1/C3b sigue siendo observación de invalidación, no ledger de sincronización ni certificado. Overflow obliga a importar snapshot completo, no a rechazar una mutación soportada. No se añade clear público que oculte historia. Generated nodes y wrappers de layout siguen fuera del DOM editable.

### Ownership, detached y continuidad de attachment

Decisión de implementación propuesta: `EditableNodeStorage` referencia-contado por nodo, con token auténtico de identidad separado. El storage posee el objeto; el objeto/token sólo tienen referencia débil al storage, nunca un ciclo fuerte. El registro del documento posee los storages conectados y detached que siguen bajo su propiedad. Children/Parent siguen siendo enlaces prestados. Mover un nodo no lo recrea. El destructor de storage no destruye recursivamente hijos que pertenecen al registro; todos los caminos parser/destructor deben adoptar la fábrica/retirement controlados. Los nodos sintéticos no editables conservan su ownership ya existente.

Un snapshot de DOM posee datos immutable y leases de todos sus nodos. Los `FrozenNodePayload`/retired payloads adquieren lease editable además del token actual; así sus mapas/revisiones retienen raw pointers necesarios. El layout conserva un pin del snapshot de DOM que usó, incluyendo textos/comentarios no enumerados como estilos. Las entradas de transición poseen lease+token del nodo. No se da por protegido un raw pointer sólo porque su token sigue vivo. Destrucción lógica y liberación física son distintas: retire/reparse/doc_destroy marca alive=false de inmediato y suelta propiedad live, pero sólo el último lease destruye storage. Nunca volver alive=true de un token retirado. Insertar después un wrapper de nodo realmente retirado exige materializar una identidad nativa nueva; no resucitar la vieja.

Remove separa el subtree sin retirarlo: alive=true, attached=false. Su propiedad documental permanece mientras el documento o handles/pins explícitos lo requieran. Eso conserva el contrato de handles legacy vivos hasta reparse/destroy; no confundir detached retenido con retired fugado. Nuevos handles retained pueden prolongar esa vida; snapshots liberados permiten liberar retired aun si otros nodos del documento sobreviven. La tabla managed usa referencias débiles para wrappers detached sin lectores, sin perder la identidad de wrappers todavía referenciados. El árbol managed mantiene fuertes a sus hijos actuales.

Agregar al propietario `dom_observation_serial` que avanza por TODA mutación de sus nodos, incluidos roots detached. Los epochs originales del root/journal conservan su significado; el nuevo serial no los sustituye. El token del bridge contiene owner identity/lifetime+observation serial y el pin del root observado. Después de detach, `OwnerDocument` público puede ser null, pero el binding interno permanece; no inferir propiedad de almacenamiento de Parent. Tras adopt, token de nodo indica nuevo propietario y ambos domains avanzan. Un getter de nodo detached consulta ese propietario, no sólo el epoch del antiguo root document.

Añadir `disconnect_serial` persistente por identidad. Remove/Clear/reemplazo y salida de un documento lo incrementan para cada descendiente afectado, durante commit de la lista ya preparada. Reattach no lo borra. Append/Insert de un nodo conectado dentro del mismo documento es un movimiento atómico que no lo incrementa. Remove y Append separados sí son desconexión observable, aunque el snapshot final sea idéntico y el journal haya desbordado. Targets de foco/press/hover y entradas de transición guardan el serial visto; una diferencia invalida su continuidad. No depender del historial bounded para reconstruir este hecho.

Cross-document se admite sólo bajo el mismo owner-thread y coordinación de ambos bindings: preparar snapshots/mappings/registros, transferir ownership y publicar ambos sin callbacks. Una tabla débil por owner-thread mantiene wrapper canónico por token, evitando duplicarlo cuando el cambio nativo se descubre desde el destino primero. Import de un move reconcilia también el origen antes de publicar Parent/Children. Si los owners difieren, rechazo previo sin mutación; nunca dos commits supuestamente atómicos entre hilos. Native token/raw node sobreviven la adopción; `style_node_id` es local al documento y puede necesitar un nuevo id para evitar colisión. Los índices dependientes se reconstruyen; no prometer que ese id numérico local sobreviva cross-document. Foco/press/transiciones del host origen pierden continuidad y no se trasladan al otro host. Mismo documento mantiene todos los ids.

## 5. Orden de writes y R2

Con owner-thread no hay concurrencia oculta entre el commit nativo y managed.

- Managed attr A → native State R2: A ya fue confirmado nativamente; importar sólo actualiza State y conserva A.
- Native State R2 → managed `State |= Hover`: getter importa R2 y setter opera sobre ese valor. No sobre el State de B.
- Managed State S1 → native State S2: S2 es actual; recovery lo importa y no reproduce S1.
- Native attr/structure → managed otra operación: import previo conserva los cambios nativos y valida la operación managed sobre la forma actual.
- R2 sin nuevos inputs: no importar DOM innecesariamente; adquirir revisión vigente y rehacer proyección mediante token/preview D1.
- Abort/OOM de la operación nueva: conserva entradas previas nativas y managed ya confirmadas, no restaura una revisión vieja.

Publicación R1 mantiene §8.1: guard durante commit/replay, token postcallback, descartar R1 si fue superada, merge de pendientes por serial y una sola recuperación inmediata. OOM no activa retry automático. Unknown/internal writes que no incrementan epoch siguen fuera del contrato público y deben eliminarse/auditarse, no certificarse mediante hash.

## 6. RuntimeValue y dominio de strings

Separar atributo `value` base de override runtime nullable. Tres casos: ausencia de override, string vacío y string no vacío. Native conserva su mapa efectivo actual para selectores/layout/paint y agrega base-presence/base-value/runtime-override. La ecuación certificada es effective = override si no-null, de otro modo base presente o ausencia. Snapshot devuelve ambos; managed Attributes recibe la base. RuntimeValue en elementos no-input se conserva para APIs/eventos, pero no superpone atributos, conforme al comportamiento actual de SyncRuntimeValues.

Dos operaciones explícitamente distintas, no confundir sus contratos:

- Nueva operación typed managed `SetAuthoredAttribute(value,x)`/remove modifica base y conserva override. RuntimeValue=null restaura base o elimina effective si no había base. Empty no equivale a null. Esto corrige la incoherencia del puente actual sin convertir Attributes managed en un espejo del valor superpuesto.
- Legacy native `aether_element_set_attribute(value,x)` conserva su resultado observable actual: effective=x. En un nodo con bridge, además normaliza base=x y override=null. Import posterior trae esa entrada nativa vigente a managed; no reaplica un override anterior. Los getters nativos legacy siguen leyendo effective. El cambio del sidecar managed tras escritura native se declara como nueva reconciliación, no como contrato antiguo que ya existía.

Cambios a value efectivo invalidan **estilo como atributo**, además de layout/text/paint; `[value]`, :has y herencia pueden cambiar. No clasificarlos sólo como edición de texto. `_uncontrolledInputValues` conserva sus entradas históricas: una edición de valor/id actualiza la entrada del id actual según el valor efectivo; no borra por inferencia ids antiguos ni reejecuta inicialización controlled sobre un wrapper superviviente. RuntimeValue=null nunca consulta el cache para reinstalar el override durante sync. El cache se usa como antes sólo en reemplazo real del componente.

**Dominio deliberadamente acotado:** no ampliar este ciclo a Unicode/CSS rendering. El binding admite strings sin U+0000 y UTF-16 bien formado, transmitidas como UTF-8 estricto; no se reemplazan surrogates silenciosamente. Mutador bound o attach con string fuera de dominio falla antes de cualquier cambio. Detached puramente managed conserva su capacidad de almacenar esas strings y attach se rechaza íntegro. Import de bytes native no representables sin pérdida falla sin cambiar R2 ni avanzar watermark. Es un límite explícito de cobertura y un cambio observable del setter bound frente a almacenamiento managed antes no sincronizado; **no** se anuncia recuperación completa para esos valores. Native legacy sin binding no obtiene una promesa nueva para bytes arbitrarios. Mutadores legacy de documentos bound validan su entrada antes del commit.

Snapshots usan longitud+status para lectura exacta y distinción vacío/ausente/OOM, no un codec nuevo ni permiso para insertar NUL en consumidores que usan c_str/strtof. Árboles normales no-roundtrip sí se transfieren typed: textos contiguos/vacíos, comentarios, hijos de leaf/void ya almacenados, raw style/script que contenga delimitadores, RCDATA y nombres sin garantía de reparse. No se serializa ni se normaliza por parser.

**Cambio público que requiere aprobación de este diseño:** alinear nombres HTML managed a folding ASCII A–Z y comparación ordinal de claves canónicas. Native ya usa ese contrato. Cambiar constructor de ElementNode, Set/Get/Has/RemoveAttribute, GetElementsByTagName/QuerySelector y Closest; auditar comparaciones de nombres relacionados. É y é permanecen distintos; CLASS/class son iguales. Values/id/class tokens no se pliegan. Esto corrige una incompatibilidad preexistente donde managed no podía representar dos claves nativas distintas; no es semántica completamente idéntica a OrdinalIgnoreCase/ToLowerInvariant. No añadir namespaces ni tocar reglas Unicode del motor CSS.

## 7. Hojas style y handlers

Cambiar contenido/orden/adjunción de un `<style>` cambia la hoja autor del host **en la misma transacción de inputs DOM**, sin `Load`. El binding registra un slot autor reservado y explícito en su runtime. Prepare calcula el source del árbol typed candidato con la política actual (style.TextContent concatenado en orden), compila sólo ese slot, prepara índice y almacena diagnósticos, sin callbacks. Commit publica DOM+slot+sheet epoch juntos. Una edición que no afecta ese source no recompila ni modifica sheet epoch. Native legacy parse/append/remove/attribute/text del documento bound atraviesan el mismo adaptador: no se difiere la compilación al import managed. De otro modo un import tardío podría pisar una hoja R2 más reciente.

Autoridad: el host es propietario del **binding de fuente** DOM→slot. El API native existente puede añadir/quitar sheets explícitas, incluido retirar el slot conocido por su handle; no se eliminan sheets ajenas. Retirar el slot es una entrada native vigente, y una mutación posterior relevante de style DOM reinstala su contenido como nueva operación cronológicamente posterior. Si no hay nueva mutación DOM relevante, importar o resolver NO lo reinstala. No existe un setter deferred que recomponga todas las hojas sobre R2. Los handles/counters del slot se reservan en staging y se consumen sólo en commit; base de la transacción incluye runtime identity, sheet epoch, compiled/registry owner y binding identity. Una llamada cross-document prepara el cambio de slot de ambos hosts afectados.

El diagnóstico de esa compilación pertenece al nuevo input y se conserva en una cola preparada. No se reproduce desde el mutador DOM, destructor ni commit bilateral. Lo consume un punto seguro del pipeline de estilo después de publicar una revisión que incluya ese input y según el guard D1. Si otra mutación supersede el slot antes de resolver, los diagnósticos del source abandonado no se emiten. Mantener orden determinista del source vigente y deduplicación managed existente. Los APIs explícitos `add_stylesheet` conservan su replay propio; después de un callback suyo no se vuelve a escribir runtime. Error sintáctico CSS conserva la política actual de compilador; OOM aborta la mutación DOM completa sin perder inputs anteriores.

La sincronización directa no llama RenderMarkup. Para invalidación real del componente, añadir RenderCandidate privado con markup y registry de handlers aislado; MarkupWriter escribe en el candidato, nunca en `_events` activo. Publicar registry junto con su DOM y token. `Dispatch` fija el registry correspondiente al DOM, no el último RenderMarkup global. BuildRenderTree y ToString son código de usuario en fase fallible y pueden invalidar el candidato. No se deshacen sus efectos externos. Parse/OOM/fallo antes del swap deja A+handlers A íntegros aunque B reutilice token aether:0.

## 8. Interacción, continuidad y publicación de transiciones

### 8.1 Estado retenido y safe points

Supervivientes: conservar `_focused`, `_active`, `_hovered`, wrappers, node tokens, scroll offsets y transition runtime. Layout/estilos viejos no certifican elegibilidad tras mutación. Host entry/NeedsRender compara token nativo puro para detectar mutaciones directas; freshness import nunca depende de que se llame por casualidad un getter de usuario.

Preparar en import/mutación el cambio de seriales/dirty y registros internos de lifecycle. No llamar InputsChanged delegado que agregue a HashSet o reserve después del commit nativo: preparar capacidad/lotes y publicar sus referencias. `Version`/`InputVersion`/HostInputSerial y next ids se prevalidan para exhaustion; la operación falla con status/exception de límite antes de mutar. No wrap de un contador usado como certificado. Los contadores de métricas pueden saturar, no se usan como autoridad. En legacy void SetState, un límite no representable produce no-write y estado sticky explícito consultable por read_dom_token; no false success del bridge.

Las notificaciones internas no ejecutan RenderRequested. La operación pública compuesta sale primero de su scope transaccional y todos sus datos están confirmados. Un trampoline de fin de operación, fuera de ese scope y sólo con depth cero, puede pedir **una** notificación de render; si el host ya está en guard/replay, únicamente deja pending-notification. Los handlers de esa notificación pueden reentrar sobre el estado completo, no sobre Remove+Append parcial. Un fallo del listener después de commit es fallo de notificación, no rollback; revalidar antes de usar snapshots. No usar SynchronizationContext.Post como prueba de asincronía: un contexto personalizado puede ejecutar inline. Los legacy native mutators no llaman .NET/usuario; el host detecta sus seriales a la salida del callback y en todas sus entradas. Esta diferencia de scheduling no concede permiso a un frame stale.

Los getters públicos que devuelven Children/Attributes entregan vistas read-only estables de la **colección actual** y no el List/Dictionary mutable casteable de hoy. Obtener vista ejecuta freshness; cada acceso o inicio de enumeración captura y fija una versión immutable vigente. Un enumerador ya empezado recorre ese snapshot, aunque una mutación posterior cambie el árbol; no importa a mitad ni mezcla versiones. Una vista retenida crea un nuevo snapshot al comenzar una nueva enumeración/lectura, por lo que no queda permanently stale. Este contrato de enumeración snapshot es cambio observable declarado. OwnerDocument/Parent/TextContent/State y queries atraviesan freshness una sola vez y luego usan accesores internos puros. No invocar overrides de Node desconocidos durante import/certificación: sólo built-ins sincronizables; subclases nunca fueron producto del parser/bridge y no reciben promesa nueva de native rendering.

### 8.2 Reconciliación de interacción

Los registros interactivos guardan `(token, disconnect_serial, host/document identity)`. Antes de cualquier acción discreta, validar esos valores, no sólo attached actual. Pérdida de continuidad queda pendiente hasta procesarse aunque se haya reinsertado el nodo.

1. Tras commit de inputs/import, invalidar hit/pending target y marcar focus/press/hover elegibles sólo si los tokens siguen vigentes. No ejecutar Focus/Dispatch desde el import o commit.
2. En el safe point de host, consumir un lote privado de lifecycle, dejando nuevos cambios en el siguiente. Una retirada de pressed target invalida press y click incluso reattach. Cancelar live scrollbar drag si perdió continuidad; nunca limpiar capture hasta release.
3. Limpiar autoridad de foco anterior antes de blur. Entregar como máximo un blur de esa pérdida, conservando el registry/target que corresponden; si el target sigue detached, sólo su handler propio, sin inventar bubbling por ancestros retirados. Un callback que enfoca otro target prevalece y no se sobrescribe después. Revalidar tras callback y no escoger foco nuevo usando layout antiguo. La edición de State público sigue siendo flags CSS, no una API para enfocar el host.
4. Reconciliar el Hover de la antigua cadena y la cadena actual, aun si target es idéntico. Reparent dentro del mismo documento conserva target y no genera out/over sólo por cambio de padres. Actualizar exclusivamente bits Hover mediante entradas nuevas sobre State actual, no máscaras guardadas en B; preservar Invalid/Active/Focus salvo sus propias decisiones de lifecycle.
5. Para retirada, invalidar target y volver a hit desde última posición después de obtener proyección certificada. `mouseout` sólo si el outgoing target todavía está conectado y conserva continuidad al entregarlo; no enviarlo a un retirado sólo para vaciar una variable. `mouseover` normal sólo a candidato todavía vigente después de callbacks. No añadir mouseenter/mouseleave. Capture de scrollbar sigue impidiendo delivery DOM del gesto.
6. Deshabilitación/ocultación sin detach usa la reevaluación de elegibilidad existente con estilos/layout actuales. El host conserva el límite de estabilización hover existente, separado del máximo de una recuperación §8.1. Cambios producidos por blur/out entran en Bnext y no se borran al terminar el lote anterior.

Los cambios de flags debidos a lifecycle son entradas nuevas justificadas por la topología/continuidad actual, no replay de writes antiguos. Deben dejar pruebas observables de ese origen. Se declara la supresión de out/click sobre retirados y pérdida de foco por disconnect como corrección explícita de rutas hoy incompletas, no equivalencia universal con Blink.

Scroll offsets sobreviven por wrapper sin id; clamp tras layout válido. Retirar índices usando ids/tokens capturados en staging, sin pedir GetNativeHandle de un wrapper eliminado. En resize/retiro puede cancelarse drag, pero press de scrollbar→mutación→release nunca se convierte en click de botón.

### 8.3 Transiciones: orden elegido y contrato completo

Preparar las observaciones y retiros sobre candidato native que conserva **active y finished**, sus observed_ms, delay, duration, timing, from/to/current matrices/opacity y entradas no afectadas. Añadir owner-thread, instance/lifetime y `TransitionMutationSerial` para Observe/Tick/retire; incluye tick final y cambios de samples aunque membership sea igual. Mantener intacta aritmética legacy de tiempos negativos/backward/extremos. No sustituirlo por contextual_transitions: sus clocks, events y ausencia de finished no son equivalentes.

Capturar un único nowMs para cada batch antes de preparar las observaciones. Retarget muestra el estado vigente en ese instante; retry es otro intento con nueva base/timestamp. Un cambio de atributo ajeno no inicia/cancela/reinicia transiciones iguales. No-op sin observaciones, retiros ni cambios de lifecycle comparte el estado y no clona. La copia fallible inicial de los vectores para una operación real es O(A+F), declarada; no se presume O(K). Se puede optimizar después sin cambiar contrato.

Tick también necesita strong guarantee: hoy puede reservar finished tras modificar active. Preparar samples/migraciones en candidato y swap no-throw, o reservar absolutamente todo antes de tocar live y probarlo. Ninguna excepción atraviesa C ABI. Un error de Tick/Observe no altera parcialmente los vectores ni serial. Las entradas retienen node lease+token+disconnect_serial: overrides y HasActive excluyen una entrada que perdió continuidad, aunque siga almacenada y el nodo ya esté reattached; prepare próximo la retira. Los queries no hacen allocation/callback. Eso impide resucitar animaciones con remove+reattach y evita UAF. No declarar que leer token retiene por sí solo el nodo.

**Orden obligatorio de publish D1:** después de commit nativo de estilo y de comprobar token vigente R1, comprobar además instancia/lifetime/serial base de transiciones. Publicar candidate de transiciones EN EL MISMO segmento no fallible que maps/styles, antes del replay gestionado. El runtime handle y el préstamo del builder no cambian: swap de storage interno. Listener HasActive/Tick ve R1 completa; es cambio observable frente al corte c577 donde Observe iba después del replay. No queda ningún Apply de T1 después de replay. Si listener tickea/observa, su estado más nuevo prevalece, invalida certificados y se recupera desde lo vigente; nunca restaurar T1. Error del listener no deshace R1 ya válida.

Coordinar con D2: `ActiveTransformMembershipSerial` es distinto de `TransitionMutationSerial`, incluye delay activo y excluye finished. Candidato comparte pin/índice persistente de owners D2; sólo membership delta real lo cambia. No copia N ni rebuild owners por clone/no-op. DOM detach/adopt invalida la elegibilidad por liveness/attachment/disconnect; no esconder ese cambio bajo serial de sample.

## 9. Chromium consultado, no prueba inventada

Fuente pinned `2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9`, archivos locales de investigación. Sólo inspección, no ejecución ni afirmación de que Blink tenga este puente/rollback.

- `third_party/blink/renderer/core/dom/element.cc`: Element::RemovedFrom (4691–4800) diferencia StatePreservingAtomicMove de retirada y condiciona cancelación de animación; DetachLayoutTree (5107–5186) distingue reattach, limpia hover/active sólo en detach real. Referencia contra reset por edición auxiliar.
- Element::ChildrenChanged (8306+) registra efectos de :empty, siblings y :has; la marca estructural no se reemplaza por state-only.
- Element::SetHovered (13552+) cambia estado y declara invalidación; no reconstruye todo el documento.
- `core/input/mouse_event_manager.cc`: RecomputeMouseHoverStateIfNeeded (381–434) consume dirty antes de callbacks y reconsulta última posición; HandleRemoveSubtree (501+) invalida click target retirado.
- `core/input/boundary_event_dispatcher.cc`: SendBoundaryEventsSameOrigin (145+) comprueba pertenencia al documento antes de out/over y construye cadenas. No se importan mouseenter/mouseleave ni nuevos eventos a Aether.

URL permanente base: https://github.com/chromium/chromium/blob/2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9/third_party/blink/renderer/

## 10. Checkpoints y gates pendientes

1. Aprobar contratos: linearización, strings/value, storage/ownership, slot style y orden interacción. No implementar mientras estas decisiones estén abiertas.
2. Native DOM snapshot/edit + fronteras legacy fuertes + fault injection. Comprobar exactitud y no callbacks/asignaciones en commit.
3. Managed operaciones compuestas + mapping de todos los tipos + barreras públicas/internos puros + tests mixed R2. Mantener fast-path de State sin snapshot global.
4. Slot CSS y handlers staged. Fault injection sin pérdida de sheets R2/handlers A.
5. Transition prepare/swap + serial. Fallos tras N observaciones, tick reentrante y no-op sin clone.
6. Integración host D1 y validación independiente del conjunto. D1 parcial no equivale a esta gate.

Matriz mínima observable: cada mutador; Remove+Append atomicidad; texto vacío/contiguo/raw; attr remove/empty/NUL/Unicode; value base presente/ausente + null/empty/override; native R2 antes/después de managed attr y State |=; native estructura + managed state; next FULL después de recuperación; A-B-A; overflow journal; reparent hover mismo target; focus/active removals y callbacks reentrantes; scrollbar press→mutación→release sobre botón sin click; scroll sin id; transiciones delay/active/finished/retarget/cancel; sheets ajenas R2; handler A/B + fallo parse; disposal y leases; OOM en cada reserva nativa/managed y malloc/string read; wrong-thread; dos supersessions con Bnext conservado; no nueva pintura/hit obsoleto.

## 11. Estado de aprobación y cambios observables sometidos a revisión

Esta revisión cierra decisiones del borrador, no los da por implementados ni aprobados. Pedir PASS de diseño exige aprobar expresamente: folding ASCII managed; dominio de strings bound; vistas con enumeración snapshot; legacy effective-value write normalizando sidecar; sincronización inmediata DOM+slot y timing diferido de diagnósticos de ese source; continuidad por disconnect; callbacks de lifecycle definidos arriba; transición publicada antes de replay con timestamp de batch único. No presentar esos puntos como preservación exacta de todas las inconsistencias antiguas.

Limitaciones legítimas: wrong-thread/cross-owner, objetos custom no representables por bridge, strings fuera del dominio, resource/counter exhaustion y destrucción explícita del documento sin replacement. Rechazo preserva inputs/dirty y reporta causa. No son mecanismo para declarar soportadas esas entradas. Las operaciones ordinarias built-in dentro del dominio deben completar recuperación: no aceptar un FAIL cerrado permanente para attrs/text/tree/value/R2 que el contrato sí cubre.

No se implementa dibujo, renderer Linux, nuevos tipos de eventos, namespace/Unicode CSS completo, incrementalidad structural ni sistema de merge CRDT. La expansión mínima necesaria es native editable lifetime+snapshot/edit, slots CSS vinculados, staging de handlers y transition state. Son contratos que faltan en D1, no arreglos de serial que puedan simularse.

## 12. Matriz ampliada de pruebas y oráculos

Además de §10, exigir estas secuencias exactas y observar datos finales, siguiente FULL y callbacks, no sólo generation:

- Native cambia detached attr/State después de Remove; getter/import y reattach lo conservan. Owner domain serial debe detectar aunque doc root epoch no cambie.
- Native Remove→Append del mismo node antes de getter, más de 128 mutations entre medias: press no click, focus pierde continuidad, drag cancelado/capture retenido, transición vieja no retorna. Append same-doc atómico sí conserva continuidad.
- Cross-doc con ids locales iguales: tokens auténticos siguen, ids destino únicos, source/dest Parent/Children recíprocos, wrapper único; wrong-owner falla antes de ambos commits.
- Vista Children/Attributes retenida y enumerador abierto: enumerador anterior estable, nueva lectura refleja árbol vigente, no cast a List/Dictionary mutable.
- Native R2 legacy SetAttribute(value) durante override: getter/CSS/layout ven x, managed importa base x/null; posterior RuntimeValue=null no revive la base vieja. Managed base edit durante override conserva effective hasta clear.
- É/é, CLASS/class, constructor/query/Closest/canonización de atributos; valores e ids intactos. NUL/lone surrogate bound/attach fallan antes de bind, detached puro intacto; UTF8 native inválido no se vuelve vacío ni U+FFFD certificado.
- DOM style edit→native remove slot R2→import: no reinstalar; native remove slot→DOM style edit: nueva fuente gana. Hoja ajena persiste. Native legacy style text/structure participa igual. OOM de compile/index/cola diagnósticos aborta DOM entero.
- Registry A/token aether:0; Build B reusa token y falla parse/compile/mapping. Dispatch A sigue delegado A. Reentrancia Build B nunca ve registry parcial. Direct RenderMarkup externo no cambia registry fijado de host A.
- Listener HasActive/Tick tras publish ve maps R1+transiciones R1; Tick native directo durante callback native previo invalida transition candidate antes del primer swap; tick durante replay nunca se sobrescribe. Batch con varias observaciones usa mismo nowMs. OOM en mitad del candidato deja active+finished+owners+serial idénticos.
- Fallo en cada reserva administrada y native new/malloc, desde snapshot hasta bookkeeping, child vectors, id registry, CSS, transition candidate y preview de estilo. Comparar inputs, relación recíproca, ownership/pins, next ids/epochs, runtime sheets, mapas publicados, dirty/Bnext y handlers. No C++ throw cruza C ABI; null inesperado jamás se interpreta como vacío.
- En commit instrumentado: cero allocation, cero delegates/user callbacks, cero destructores con efecto externo, ninguna escritura si precondition falla. Test after-commit notification throw confirma que no se describe como rollback.
- Guard reentrante/Dispose/replace y dos supersessions: ningún nuevo hit/paint obsoleto, entradas discretas en orden y no descartadas, Bnext persiste, un máximo de recovery inmediato y ningún retry automático por OOM.
- Warmup hover State sin reglas en 100/500/1000 tarjetas: cero snapshots DOM completos/imports, cero rebind/parse, cero clone de transición no-op, cero escaneo global de atributos/children. Registrar `DomTokenReads`, `DomSnapshotBuilds`, `DomNodesImported`, `DomEditsPrepared/Committed/Aborted`, `DomCommitAllocations`, `TransitionCandidatesCloned`, `TransitionEntriesCopied`, fallback reasons. D1 no declara que el full rebuild structural sea incremental.

Oráculo de entradas: snapshot typed exacto + recorrido managed interno bajo misma certificación, no HTML reserializado. Oráculo de estilo/layout: FULL desde esos inputs actuales, no source antiguo. Oráculo de transición: twin runtime legacy con secuencia de operaciones vigente/mismos timestamps, comparando active+finished/samples; se excluye el comportamiento de OOM parcial antiguo. ASan/UBSan y conteo de leases completan pruebas de destrucción/retirement; pruebas managed reales cubren P/Invoke y guards, no únicamente mocks.

Artefactos de revisión: contrato rector §8.1/§9.1, `D1-MANAGED-DOM-REVIEW.md`, `DOM-SYNC-REVIEW.md` y `TRANSITION-INVENTORY.md`. Son evidencia interna del proceso; la especificación del producto debe vivir en este documento y en sus APIs/tests, no depender de mensajes privados.
