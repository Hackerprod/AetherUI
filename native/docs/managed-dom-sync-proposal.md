# D1: sincronización DOM in-place y recuperación vigente

**Estado: borrador de propuesta para discusión, NO PASS, NO implementación.**
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

### Ownership pendiente de especificar antes de PASS

Los tokens actuales no retienen el nodo real. La solución elegida debe añadir storage leases para nodos editables, igual que generated_storage ya conserva pseudos. Los mapas/revisiones/layout/transiciones que guardan raw pointers necesitan pins o prueba de no dereference después de retiro. Removal es detach, no destrucción ni cambio de identidad; reinsertar el mismo objeto conserva token. Reparse/retirement explícito invalida liveness pero conserva storage hasta liberar lectores. No conservar todos los nodos retirados para siempre.

Falta cerrar representación concreta de leases y adopción cross-document sin ciclos ni doble ownership. Es la principal pieza de lifetime pendiente de este borrador.

## 5. Orden de writes y R2

Con owner-thread no hay concurrencia oculta entre el commit nativo y managed.

- Managed attr A → native State R2: A ya fue confirmado nativamente; importar sólo actualiza State y conserva A.
- Native State R2 → managed `State |= Hover`: getter importa R2 y setter opera sobre ese valor. No sobre el State de B.
- Managed State S1 → native State S2: S2 es actual; recovery lo importa y no reproduce S1.
- Native attr/structure → managed otra operación: import previo conserva los cambios nativos y valida la operación managed sobre la forma actual.
- R2 sin nuevos inputs: no importar DOM innecesariamente; adquirir revisión vigente y rehacer proyección mediante token/preview D1.
- Abort/OOM de la operación nueva: conserva entradas previas nativas y managed ya confirmadas, no restaura una revisión vieja.

Publicación R1 mantiene §8.1: guard durante commit/replay, token postcallback, descartar R1 si fue superada, merge de pendientes por serial y una sola recuperación inmediata. OOM no activa retry automático. Unknown/internal writes que no incrementan epoch siguen fuera del contrato público y deben eliminarse/auditarse, no certificarse mediante hash.

## 6. RuntimeValue y strings

Decisión semántica necesaria: separar atributo `value` base de override runtime nullable. Tres casos: ausencia de override, string vacío y string no vacío. RuntimeValue=null restaura base presente o ausencia. SetAttribute(value) durante override cambia base, no pierde el override. El effective value debe ser el mismo para layout/paint y la política CSS `[value]` existente.

Propuesta de storage: conservar mapa efectivo usado hoy por native y una entrada base/override explícita; snapshot expone ambos, el managed recibe atributos base. Legacy SetAttribute(value) debe definirse como escritura del atributo base también, evitando inferir la base a partir de un valor ya superpuesto. Esto precisa tests de compatibilidad y documentación del getter nativo efectivo.

Las APIs typed no pasarán por C-string para datos managed: permitir longitud explícita para NUL y representación lossless, sin truncar ni reemplazar datos silenciosamente. Falta cerrar la política exacta UTF-16/WTF-8 y nombres Unicode: managed OrdinalIgnoreCase/ToLowerInvariant discrepa de native ASCII. No se propone resolverlo rechazando árboles válidos o mediante serializer.

## 7. Hojas style y handlers

Cambiar contenido/orden/adjunción de un `<style>` exige cambiar CSS autor del runtime sin `Load`, reset global ni pérdida de hojas nativas R2.

Dirección propuesta: slot autor propiedad del host, reemplazo staged de sólo ese slot, compilación/index/diagnósticos en prepare y commit por swap. Preservar sheets ajenas, registry y runtime identity. No ejecutar callbacks de compilación dentro de una mutación DOM compuesta. Falta fijar si el reemplazo del slot va unido al commit DOM o al siguiente prepare de estilo; la opción unida al commit elimina precedencia ambigua con una edición nativa posterior del mismo slot y es preferida, pero exige revisar binding/diagnostics.

La sincronización directa no llama RenderMarkup. Para invalidación real del componente, añadir RenderCandidate privado con markup y registry de handlers aislado; MarkupWriter escribe en el candidato, nunca en `_events` activo. Publicar registry junto con su DOM y token. `Dispatch` fija el registry correspondiente al DOM, no el último RenderMarkup global. BuildRenderTree y ToString son código de usuario en fase fallible y pueden invalidar el candidato. No se deshacen sus efectos externos. Parse/OOM/fallo antes del swap deja A+handlers A íntegros aunque B reutilice token aether:0.

## 8. Interacción y transiciones

- Supervivientes: conservar `_focused`, `_active`, `_hovered`, wrappers, node tokens, scroll offsets y transition runtime. Layout/estilos viejos NO certifican elegibilidad tras mutación.
- Reparent mismo hovered: reconciliar old/new ancestor sets aun con target igual, cambiar sólo Hover, conservar otros bits y pedir hit desde última posición con certificado nuevo. No emitir out/over sólo porque cambió un ancestro.
- Retirada real: marcar targets ineligibles de inmediato por attachment auténtico; preparar decisiones/eventos, ejecutar blur/out sólo fuera del commit, bajo guard/revalidación. No mouseover a un retirado ni click de press cuyo target fue retirado. Falta fijar con precisión el orden de callbacks frente al layout y removals reentrantes.
- Capture scrollbar: cancelar drag si container se retira o deja de ser utilizable; conservar `_scrollbarPointerCaptured` hasta release; esa release no se entrega a contenido.
- Scroll: conservar por wrapper superviviente sin exigir id, clamp tras layout válido, retirar usando ids capturados antes del mapping change. Nunca pedir un handle ya eliminado.
- Transiciones: nuevo prepare de observaciones/retirements sobre candidato native de active+finished, con timestamps/curvas/delay/valores actuales intactos. Serial de runtime incluye tick/observe/retire. Commit por swap del storage, mismo runtime handle/builder. No-op sin observaciones no clona. Retarget usa sample actual; cambio no relacionado no cancela. OOM a mitad de Observe candidato no altera runtime activo. Esta pieza también bloquea D1 completo y no está implementada en su corte parcial.

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

## 11. Preguntas abiertas concretas para revisar ahora

1. ¿Aprobar eager typed DOM transactions y barreras públicas de lectura como alternativa al ledger deferred? Evita el problema de precedencia sin un log ilimitado.
2. Cerrar storage lease por nodo y adopción cross-document (incluye style_node_id collision), conservando raw ABI compatible.
3. Cerrar longitud/encoding lossless y canonicalización HTML ASCII gestionada; es corrección de coherencia de interfaz, no namespace nuevo.
4. Elegir commit conjunto DOM+slot style con diagnósticos diferidos, para que una edición posterior R2 del slot gane por orden real.
5. Fijar protocolo exacto de pérdida de focus/hover/active y safe-point de RenderRequested, sin callbacks dentro de mutador compuesto.

No hay bloqueo de herramientas ni contexto faltante. La investigación no persistida demoró demasiado este primer borrador; ya queda recuperable y listo para discusión temprana, no para PASS.
