# E2 proposal: certified host hit and scroll input

Status: COMPLETE PROPOSAL v2, pending independent design re-review. Revision 1
(8663b26) received FAIL/revision required for ownership acquisition and closure,
durable discrete-action admission, and the shared settling budget. No PASS is
asserted for this revision. No product code,
activation or E2 implementation is authorized by this document. Preserve E1 freeze
`51b4fb53f78d01782a7de92f6ca4d4f8668dbebb` (E1 implementation + CI follow-up).
DOMsync `585804d5eac67232f8ea7eac829ba792e4e9e20e` is a separately inspected
reference, not an imported or independently approved E2 dependency. Its claims and
lifetime gates remain pending. DOMsync work retains priority.

## 1. Findings that rule out a callback-only patch

- `UiDocumentHost.Pointer.cs::HitTest` always passes `ScrollOffsetFor` through
  `LayoutInterop.HitTest` / `NativeLayoutInterop.HitTest`. E1 correctly falls back
  for any external callback, so E1 alone does not accelerate current host input.
- `layout_types.h::hit_test_recursive` adds clamped offsets only to descendants,
  after exact unmap and before child traversal. `input_hit_test.h::layout_hit_test_input`
  has a different dynamic-scroll rule, chrome precedence and atomic/text policy.
  Replacing both with one new predicate would change the current contract.
- E1 incoming subtree bounds describe unscrolled content. Simply adding an offset
  lookup to its query can falsely prune nested scrolled descendants. The proposed
  scroll-space bounds below must be differential-tested first.
- `HitAcceleration::matches` currently couples index reuse to full mutation epochs.
  Even a no-effect hover state write can rebuild N boxes. Index validity and exact
  result validity must become separate proofs, not hidden work under cache hits.
- `NativeLayoutResult` and `NativeStyleMap` own mutable IntPtr fields and dispose
  them with Interlocked.Exchange. Keeping the managed object alive does NOT retain
  the native handle. A new owned lease or existing real native pin is required.
- E0 traces preserve two historical behaviors: CursorAt(B) returns A's cursor while
  A is pending; pending hover can run after a later down/up/click. Correcting them
  is an explicit observable change requiring PASS, not transparent optimization.

## 2. Concrete sources and limits

Aether references above are frozen at 51b4fb5. DOMsync references were inspected
using `git show 585804d:path`, not adopted from workers' mutable working trees.

Chromium pin: `2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9`, paths under
`third_party/blink/renderer/`:

- `core/layout/hit_test_cache.cc`, HitTestCache::LookupCachedResult,
  AddCachedResult and Clear: exact point/request compatibility, DOM version and
  explicit cache eligibility. This is not a certificate based on target bounds.
- `core/layout/layout_view.cc`, LayoutView::HitTest and HitTestNoLifecycleUpdate:
  lifecycle before hit, separate total/cache-hit counters and external invalidation.
- `platform/widget/input/main_thread_event_queue.cc`,
  QueuedWebInputEvent::FilterNewEvent, MainThreadEventQueue::DispatchEvents,
  QueueEvent and IsRafAlignedEvent: compatibility gates, bounded prefix dispatch,
  scheduling a processing opportunity and no passing discrete events out of order.
- `core/input/boundary_event_dispatcher.cc`,
  BoundaryEventDispatcher::SendBoundaryEventsSameOrigin: out before over, shared
  ancestors, and tree checks around callbacks. E2 does not add enter/leave events.
- `core/input/mouse_event_manager.cc`, RecomputeMouseHoverStateIfNeeded and
  HandleRemoveSubtree: consume dirty before callback-driven recomputation and lose
  removed press targets. No new physical move is needed for dirty hover.
- `core/input/event_handler.cc`, EventHandler::HandleMousePressEvent and
  HandleMouseReleaseEvent: distinct Active/Release queries and synchronous
  scrollbar branches. This supports separate query modes, not copying Blink's
  entire event system into Aether.

Additional pinned files were fetched with the GitHub connector, saved outside the
product tree, and their Git blob hashes verified locally:

- [`core/paint/paint_layer.cc`](https://github.com/chromium/chromium/blob/2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9/third_party/blink/renderer/core/paint/paint_layer.cc#L1155-L1208),
  blob `d590f86a93e3f296250949347eb53f0b6c151fab`:
  CreateLocalTransformState maps between fragment paint-property spaces;
  HitTestLayerByApplyingTransform (1800-1847) checks invertibility before local
  queries; HitTestChildren (1916 onward) uses reverse paint order. E2 keeps
  Aether's exact inverse and conservative unbounded transformed subtrees, not
  approximate rectangles or Blink's different 3D/fragment policies.
- [`core/paint/box_fragment_painter.cc`](https://github.com/chromium/chromium/blob/2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9/third_party/blink/renderer/core/paint/box_fragment_painter.cc#L2433-L2520),
  blob `b03abaf104d483f3c09e152fb42a43b82186bf5b`: NodeAtPoint checks child
  overflow clips, then changes child offsets for scroll, then tests self.
  This supports separating incoming clips from content-space queries. Its
  pixel-snapped scroll/overscroll rules are NOT adopted as Aether semantics.
- Existing `core/layout/layout_view.cc`, MapLocalToAncestor/MapAncestorToLocal
  (310-372), MapToVisualRectInAncestorSpaceInternal (413 onward) and
  OffsetForFixedPosition (530-533) distinguish transform direction, viewport
  mapping and scroll-relative coordinates. Aether's viewport/DPI certificate
  cannot be replaced by just the document version.

No Chromium executable was run. The source consultation informs the proposal;
Aether's current exact native traversal remains the differential oracle.
New small external .NET baseline probes, not E2 implementation tests, are recorded
with their full source and hashes in `hover-e2-proposal-evidence.md`.

## 3. Proposed native offset snapshot and ownership

Introduce an additive immutable scroll-offset snapshot, scoped to an authentic
DOM owner/binding and topology. Entries identify nodes with retained identity
records, owner identity and disconnect serial, not raw addresses or StyleNodeId
alone. Absence means the host's current default offset (zero), explicitly distinct
from the input-chrome API's no-source layout-default offsets.

Build once from the host-owned scroll dictionary at initial layout/owner change.
Patch K changed offsets using persistent path-copying; do not copy S scroll entries
or walk N boxes on each move/query/scroll update. Empty zero snapshots are O(1).
Creation/patch pins sources, validates owner/token once, rejects duplicate identities,
and stages all allocations before publication. Values remain immutable during a
query. Do not enumerate or invoke arbitrary user callbacks to manufacture a
snapshot; the generic callback APIs remain the original full oracle.

The new host route must have a status-bearing native query: success+miss is distinct
from OOM, invalid provenance and superseded inputs. It uses the same exact content
or chrome oracle with a pure internal lookup as its fallback. Legacy public APIs
keep their ABI and callback order/side effects. Unsupported/nonfinite offsets take
the exact oracle without index/result caching; do not silently normalize NaN or
change the existing setter contract.

A published host HitSnapshotLease must own real native references to geometry,
immutable selected/base maps, offset snapshot and authenticated DOM identity owner.
It must not be implemented as a .NET object reference to a disposable IntPtr.
Native index ownership remains one-way (layout -> index, never index -> layout).
Pending/cache records hold actual owned leases and authentic target descriptors;
no per-point deep copy or new DOM-wide snapshot, no historical owner chain. Sealed
prefixes of admitted discrete actions have separate owners as bounded in §6.1.

### 3.1 Selected lease protocol, subject to design PASS

Use one additive opaque native composite lease, not independent borrowed handles.
It retains the layout, StyleOperationPin's immutable MapVersionRefs, immutable
scroll source and the real DOM owner/root/storage pins supplied by the accepted
DOMsync lifetime contract. It stores the captured presentation/provenance token.
Geometry is retained, not deep-cloned: contextual finalize may mutate its
presentation, so a changed serial makes this lease's old certificate stale before
any index/query use. A lease grants storage lifetime, never logical liveness or
interaction authority.

The existing mechanism is concrete: `layout_box.cpp::LayoutOperationPin` increments
`aether_layout_result_s::references`; `layout_engine.cpp::aether_layout_result_destroy`
decrements it. That count is currently non-atomic. `StyleOperationPin` owns map
versions through shared ownership. DOMsync's coordinator confirms there is NO
existing guaranteed owner-thread disposal queue for layout and no finalizer
permission implied by DOMsync.

The complete source inventory and selected acquisition/release protocol are in
`hover-e2-ownership-audit.md` (normative part of this proposal). They cover all
three direct layout reference-count accesses, five LayoutOperationPin callers,
private result construction/failure cleanup and the complete transitive owner
closure, including the custom-property scope's thread-local deletion chain.

Selected protocol: checked atomic retain/release from an ALREADY protected owner;
no overflow, underflow or resurrection. A fallible noexcept LayoutOperationPin
records acquisition success and every caller checks it before use. Managed original
NativeLayoutResult AND NativeStyleMap wrappers become real SafeHandle owners;
stack-only borrows protect them before composite creation reads/retains anything.
The composite SafeHandle alone cannot protect its own creation from old IntPtr
wrappers. On any creation failure the output is null and only acquired owners are
released. All map/layout consumers use protected borrows, not raw Handle access.
Queries/mutations remain owner-thread operations, not concurrent APIs.

The inventory corrects revision 1's over-broad "memory-only" wording. Layout box
registry/index/serial writes at last release are local to the dying layout.
Synthetic-node destructors write their own identity flags. DOMsync's last-root
physical destructor may retire its exclusively dying domain when no legitimate
root/relay reader or writer survives. E2 release does not call explicit doc_destroy
or independently revoke a live domain. Shared DOM users independently retain root/
relay ownership, so a layout count of zero alone is NOT the proof for DOM cleanup.
The inspected DOMsync 585804d remains a reference; a frozen accepted global lifetime
base is still a prerequisite. No invented owner-thread Post or drain queue is used.

Pure native queries have no callbacks. Generic callback queries retain their
existing local pins/guards, now with checked acquisition. No cached native index
or result may strongly retain its containing layout or the composite lease that
owns that layout. Immutable offset/map/identity owners have no layout back-edge.
The action queue's bounded prefix owners are accounted separately, not hidden in
an index self-cycle. Replacement/build failure releases displaced owners.

## 4. Two certificates, not one epoch shortcut

Index key: owning immutable geometry/presentation sample, selected immutable map,
style policy/query traversal kind, and live geometry/base/selected provenance with
structure/style-tree coverage. Offset values and state-only mutation serials do not
change geometry bounds or child paint order. Their omission is authorized only for
this conservative broad phase; exact predicates/liveness still run.

Result/pending key: exact x/y, query mode, full authentic owner/binding and published
DOM/style/transition tokens, host input/publication serials, geometry/presentation/
paint-order identity, offset snapshot identity/serial, viewport/DPI and capture
serial. Both successful hits and misses require a completed valid query. A target
certificate is not authority to dispatch an event after an interaction claim is lost.
Unknown coverage means query/oracle again, never an approximate last-border hit.
Coordinates use exact finite float bits for E2 keys (including signed zero), not
rounded pixels. Nonfinite coordinates/offsets never cache. Query modes distinguish
ordinary cursor/content from chrome and action context; capture changes cannot
reuse an ordinary pending result. Cursor style comes from the same currently
certified selected style map, not from an old target's stored cursor enum.

All new input, scroll, capture, publication and sequence serials are checked and
non-wrapping. Resource/serial exhaustion preserves the old committed state, reports
failure and disables reuse rather than wrapping into a false match. Existing E1
presentation exhaustion remains permanently noncacheable for that layout.
Certification is a pure comparison of already published tokens. It does not import
DOM, sample transitions, run handlers, enumerate a document or request a frame.

Separating index and result validity should avoid rebuilding N boxes for a state
write with an unchanged selected map and geometry. A changed selected map remains
a conservative rebuild until a separately proved semantic order/bounds certificate
exists. E2 must report that residual rather than claim O(K) for paint-only crossing.

## 5. Conservative index per scroll space

Child ranges remain in their parent's local content coordinates. A query performs
existing exact unmap/clip first, reads its immutable offset and adds exactly the
same float/clamping result as the ordinary oracle before querying children.

For an incoming bound of a node whose descendants may scroll, E1's raw descendant
union is insufficient. Eligibility follows the ordinary oracle's actual element
plus positive-overflow condition, not a guessed CSS IsScrollContainer predicate;
chrome's separate dynamic-scroll rule is not substituted. Proposed initial
conservative construction:
- With both axes clipped by the exact current style, descendants can only accept
  a point in the padding clip before offset application. Union that padding rect
  with the node's own border input rect; retain degenerate inclusive semantics.
- With a scrollable unbounded axis, unknown/nonfinite geometry or an uncertain
  clip, mark the incoming subtree bound unbounded. Its internal child range index
  still works after entering the scroll space.
- Transformed incoming subtrees remain unbounded exactly as E1; exact inverse
  sampling decides hits. Never project approximate transformed rectangles or
  subtract offsets from bounds without a floating-point containment proof.

Thus a root scroll offset change adjusts query coordinates, not N stored positions.
Nested unbounded scroll/transform scenes may still cost O(N), and count it. Input
chrome initially keeps its full traversal with the same immutable offset source;
content results never stand in for chrome/corner/track queries.

## 6. Observable contract proposals requiring explicit PASS

P1: CursorAt(B) with pending A will query B's exact current certificate and return
B's current cursor. It does not move the pending point, deliver hover events or
replace the physical pointer's stored cursor with B's query-only cursor.
Same-point CursorAt shares the certified result; stale lifecycle keeps the current
safe default/defer policy rather than returning a cursor certified for another point.

P2: Before press/release (and explicitly enumerated other discrete input below),
drain only the compatible pending move prefix older than that action; then query
the action's own point against current lifecycle. A click must not be followed by
an older queued hover. No synthetic move is manufactured for down/up coordinates.

The E0 synthetic trace `move(A), down(A), move(B), up(A), Render` currently produces
`a:mousedown,a:mouseup,a:click,b:mouseover`. Proposed enabled trace is
`a:mouseover,a:mousedown,a:mouseout,b:mouseover,a:mouseup,a:click`, with no extra
boundary at Render. Existing bubbling adds ancestor deliveries as usual.
Direct down/up without an earlier move still does not manufacture hover.

PointerLeft remains a terminal discard of pending unobserved moves, followed only
by out for a previously committed hover; it must not invent over just before leave.
Scrollbar captured moves remain synchronous and preserve every point. Losing drag
continuity cancels the drag but keeps capture-until-release as DOMsync requires.

### 6.1 Exact barrier and prefix rules (P2)

`hover-e2-input-protocol.md` is the normative admission/state/budget protocol for
this proposal. Before PointerPressed, PointerReleased, vertical/horizontal wheel,
KeyPressed or TextInput, finish only the action's sealed older compatible prefix
at a safe lifecycle point, then query the action's own point. Keyboard click stays
inside its keyboard action. No synthetic move is manufactured for action coordinates.
PointerLeft is ordered too: when eligible it discards only its unseen prefix,
not an older action's sealed work, then sends out only for committed hover.
Capture/drag seals the segment; every captured scrollbar point remains synchronous.

Admission reserves descriptor, immutable arguments, original checked sequence,
queue slot and authentic prefix-lease ownership BEFORE EnsureVisualTree, lifecycle
notifications or any prefix callback. The E2-only queue has 64 slots. A full queue,
OOM or retain/sequence exhaustion rejects the new call before admission and leaves
prior logical state intact. This explicit resource limit requires design approval;
it does not silently discard an accepted action. Retention is bounded by one
published lease, one global pending lease, up to 64 queued prefix leases and active
scopes. There is no discrete descriptor allocation on ordinary hot moves.

Queued -> Started occurs before the first action-owned observable effect, including
focus/state/capture/scroll/text/default commit even with no registered handler.
Completed and Cancelled are terminal. Prefix callbacks have separate consumed
progress. An unstarted accepted action survives prefix/query/lifecycle failure with
its original sequence; a started action is not automatically replayed after a
callback/default fault. Dispose/replacement explicitly cancels queued descriptors.
Callback-created newer moves/actions cannot overwrite the older sealed prefix.
Recovery services older queued actions before Bnext, with one attempt per outer
entry and no automatic retry loop. A nested action cannot pass an older Queued
action; existing reentrant takeover still applies after the older action Started.

Programmatic SetScrollOffset, SetViewport and geometry publication invalidate/seal
certificates but do not synthesize old boundary events before a non-input mutation.
They request current-point reevaluation only after publication. CursorAt is a query,
not a physical input barrier or sequence change. Bubbling, StopPropagation, focus,
click eligibility and authoritative continuity remain the accepted DOMsync rules.

### 6.2 Explicit P3 corrections: stationary scroll and geometric retargeting

Additional observations over E1 ON and OFF (full details in evidence document):
- After committed A hover, SetScrollOffset revealing B and Render leave A:hover,
  no boundary events and NeedsRender=false, while CursorAt already returns B's
  Text cursor. Proposed P3 queues one current-point reevaluation and produces
  `a:mouseout,b:mouseover`, final B:hover, without a new physical move.
- A's mouseout callback moves visible B down with `margin-top:80px`. Baseline
  still delivers `b:mouseover`, retains B:hover, and a final exact CursorAt at the
  old point returns Default. Proposed P3 never delivers that stale B mouseover;
  it re-queries and dispatches the currently eligible target after layout settles.

These are deliberate observable corrections, separately requiring reviewer PASS.
Post-callback revalidation includes geometry/layout, presentation, clip/overflow,
scroll, visibility, topology/order and authoritative owner/claim continuity.
Invalidation clears the old pending target certificate before re-query. Loss of
claim authority always wins over a previously successful hit or retained lease.

Preserve the approved exception for pointer-events-only hover resettlement: do not
start automatic hover oscillation solely because applying :hover changed
pointer-events. Fresh explicit queries still use current pointer-events and their
cache is invalidated. A callback-triggered geometry/visibility/scroll/topology
change invokes P3 and uses the full current exact predicate, including current
pointer-events. A pointer-events-only style change without those resettlement
causes does not manufacture a new boundary transition. The event state machine's
policy is separate from a reusable hit result; it must not label the old target an
exact current query result. Tests cover the distinction explicitly.

For the same target after same-document atomic reparent, reconcile ancestor claims
without new target out/over. Out remains before over; ancestor bubbling and
StopPropagation are unchanged. After callbacks, no retired registry/identity may
receive an event merely because a retained snapshot names it.

### 6.3 Stopping, cursor refresh and bounded recovery

A new pending point schedules a processing opportunity even if no move follows.
After settling hover/styles, refresh the physical cursor at the last point from
the final certified snapshot. Query-only CursorAt(B) does not overwrite A's cursor.
Geometry/visibility and newly approved scroll resettlement set one dirty point;
ordinary paint-only or pointer-events-only state writes do not keep scheduling it.

All automatic settling in one OUTERMOST external entry shares eight passes and
one captured DOM lifecycle-loss batch. Render, barriers, EnsureVisualTree, internal
recovery and reentrant callbacks inherit that same budget/cutoff; no internal drain
gets a fresh eight or enlarges the lifecycle batch from Bnext. The protocol document
defines when each pass is consumed and how user-authored explicit recursion differs
from an automatic loop.

On exhaustion, one ResetUnstableHover acknowledges only its unstable prefix,
preserves newer Bnext and seals the old prefix as SuppressedUnstable. An unstarted
action remains queued for a later external entry; it does not execute after the
budget ran out or reopen that same oscillating prefix on recovery. Fresh action
query/authority checks are still required. Reset/cleanup failure keeps explicit
cleanup owed, never a replay of consumed events. Suppress self-scheduled retries;
a later external Render/new input permits one bounded recovery attempt as in the
existing style retry policy. Disposal invalidates authority immediately; retained
storage never legitimizes later stale events.

## 7. DOMsync integration contract (reference, not integration approval)

At 585804d, pure NativeDomToken reads carry owner identity, observation serial,
input/structure epochs and sticky status. NativeDomBinding.HasCurrentInputs detects
stale input without import. The host uses HasCertifiedStyleSnapshot(requireDerived)
with both published and derived NativeTransitionToken gates. E2 must not import or
invoke callbacks from NeedsRender/certificate comparison.

NativeDomNodeRecord.Identity/Owner/DisconnectSerial/Attached prove continuity.
InteractionClaim stores session/registry/target/bit and native claim generation;
IsCurrent checks actual authority. InteractionContinuity.Capture/Matches and
DrainInteractionLosses in UiDocumentHost.InteractionClaims.cs are the relevant
surfaces. Same-document atomic moves can preserve target authority but require
hover-chain reconciliation; remove+reattach/adoption does not regain an old claim.
Pending target invalidation belongs in DOMsync publication/loss bookkeeping too.

Claims, implicit owner adoption, lifecycle/finalizable lifetime and notifications
still need their own independent gates. A frozen later DOMsync base must be agreed
before E2 host implementation; this proposal cannot bypass those gates.

## 8. Atomic scroll publication and failure boundaries

`UiDocumentHost.Scroll.cs` currently writes two offset dictionaries before
InvalidateScroll, and rebuilds box indices during layout. E2 must stage capacity,
identity mapping, persistent native offset patch, new lease and checked serials
before publishing any logical offset/source change. Capacity-only growth is not
publication. The commit performs only no-fail swaps/known-key writes; no custom
comparers, user callbacks, native callbacks or allocations. RenderRequested is
post-commit. If it mutates state, acknowledgment covers the captured batch only.

Layout/rebuild can visit N boxes (already a full geometry step), but must stage
clamping/pruning of S offsets and publish the layout, chrome indices and matching
scroll snapshot consistently. Failure cannot certify new geometry with old offset
semantics. Display-list scroll visual values and hit source come from the same
published offsets/version; a retained frame is not authority for a newer query.
No per-move dictionary copy, scene walk, DOM import or layout clone is allowed.

Snapshot setup/patch failure before commit preserves prior offsets and dirty/Bnext.
New status-bearing native queries distinguish success hit/miss, unsupported
certificate, superseded/provenance failure and OOM. If accelerator construction
fails, run the complete pure-source oracle and count both attempts. A successful
full oracle result can be returned; an exception/failure cannot be cached as miss.
The C ABI contains C++ exceptions. Host maps failure to its existing safe
miss/default/defer or explicit lifecycle error, never stale input authority.

## 9. Native ABI, managed surfaces and implementation sequence

Names are provisional, contracts are not. Add versioned opaque offset-snapshot and
hit-lease create/patch/retain/release APIs plus status-bearing query/result structs
with explicit size/version. Preserve old public ABI structs/functions and callback
semantics. A native result carries authentic target identity/owner/disconnect and
chrome part, with retained source ownership, not a free-standing borrowed raw box.
Managed projection resolves that descriptor through the current DOMsync binding
index and checks continuity without importing per query. Unsupported legacy mixed
provenance keeps the legacy oracle behavior; it cannot gain a false certificate.

Expected product surfaces, only after independent design PASS:
- Native layout types/operation pins and lifecycle exports; offset snapshot and hit
  acceleration helpers; ordinary/input traversal pure-source adapters; diagnostic
  work counter extension without changing existing ABI sizes.
- NativeLayoutInterop, NativeStyleMap, their map/layout consumers, LayoutInterop
  and protected SafeHandle/composite lease wrappers;
  UiHostOptions: separate experimental E2 input flag default false, requiring a
  native capability handshake. E1 native acceleration default remains OFF.
- UiDocumentHost Scroll/Pointer/Input/Render/Publication/Style and DOMsync
  publication/loss bookkeeping: all pure certificates, authoritative claims,
  checked serials, pending prefix extraction and post-callback validation.
- Existing PerfReporter and native/managed test targets, not a second reporter.

Stages: E2a native pure source + owned lease + status + scroll-safe differential
index; E2b atomic host scroll publication and exact certificates; E2c approved
P1/P2/P3 event/cursor policy with explicit ON/OFF traces; E2d independent combined
review. No host stage before a frozen independently accepted DOMsync lifetime,
claims and publication base. A missing dependency is a gate, not permission to
copy 585804d WIP. The proposer must revise this document if that accepted contract
differs materially. No implementation, default activation or main integration is
authorized by this proposal-only commit.

## 10. Required tests and measurable gates

### 10.1 Differential and certificate matrix

- Compare new pure-snapshot queries with old callback oracle fed the same fixed
  values, separately ordinary and input chrome. Include absent source versus
  explicit zero, scroll defaults, clamping, nonfinite values and atomic/text cases.
- Nested scroll, each overflow/clip axis, visible overflow, zero-sized inclusive
  edges, +/-0, nextafter on either side, large/subnormal floats, inline fragments,
  generated/presentation owners, z order and children of hidden/none parents.
- Transforms combined with nested scroll, origins, singular/nonfinite samples,
  unknown bounds and fallback. Differential checks include target/part/owner,
  not merely hit boolean. Root/viewport/DPI and retained frame mismatch cases.
- Vary every result certificate field independently at the same point: geometry,
  presentation/failure serial, selected map, DOM observed/native R2 state, source
  slot, topology/order, scroll patch, viewport/DPI, capture/session/registry change.
  No-op exact certificate reuse must still skip work, including exact misses.
- External callback order/counts/side effects/throw/reentrant dispose stay exactly
  legacy full traversal with no cross-call reuse, even when E2 is enabled.
- A retained index cannot dereference freed boxes; old map/owner A -> B failure
  cannot retain an accumulating chain. Native/managed dispose, GC last release,
  same-doc move, remove+reattach, adopt, generated retirement and lease release
  require destructor/owner-count evidence, plus ASan/UBSan where supported.

### 10.2 OOM and publication

Inject at offset construction/path copy, host dictionary/index capacity, lease,
status/result projection, index build and oracle scratch allocation. Confirm the
allocation actually fails. Separate hot allocation-free exact and different-point
queries from cold OOM probes; a successful fail-new=0 hot query is not fault
coverage. After removing fault, same-point success proves no error memoized as miss.
Check all logical offsets/maps/serials/claims remain old on pre-commit failure,
new after successful commit even if post-commit notification throws, and Bnext
survives reentrant callbacks. Native ABI never leaks exceptions. Test saturation,
repeated A/B build failure, empty snapshot and finalizer without dispatcher.

### 10.3 Real managed event/cursor traces

E2 OFF preserves historical E0 and additional evidence exactly. E2 ON uses distinct
assertions for P1, P2 and P3, not edited baseline expectations hidden under one test.
Run native E1 OFF/ON with managed E2 OFF/ON where capability permits; print active
branches in standard targets. Re-run 16 existing input traces, adapting only
explicitly approved event-order expectations in the E2 ON suite.

Required traces: move A-B-A with/without barrier; move/down/move/up without paint;
point-B CursorAt while A pending; duplicate same-point query; shared ancestors;
StopPropagation; callback hides/removes/reparents/adopts/replaces/repositions the
incoming target or changes scroll/stacking; reentrant move/press/left/dispose and
new Bnext during a barrier; keyboard/text/wheel boundaries; capture/drag point
sequence and continuity loss through release; stale transition/DOM/source slot;
pointer-events-only exception; eight-pass unstable hover; cursor style changing on
hover; stop immediately after entry; stationary scroll/geometry; multi-second idle.
No extra events or busy frames after settling. Errors must not replay delivered
callbacks or drop an unstarted discrete action silently.

### 10.4 Work and performance evidence

Report cold setup and amortized/warm work separately: actual managed/native
allocations/bytes, persistent entries/pages copied, snapshot/lease creates and
retains/releases, full scene/owner/offset enumerations, order classifications,
index boxes/ranges built/visited, exact boxes, offset lookups, fallback attempts
and completed fallbacks, native callback calls, interop calls, certificate reads,
cache hit/miss, pending/drain/barrier work, style/layout/display work and GC.
Existing E1 counters omit some allocator/control/sort overhead; retain that caveat
and use independent allocation instrumentation for total cost.

100/500/1000-card fixtures must include same-target different points, crossing
without CSS effect, paint-only changed selected map, root and nested scroll,
overlap/unbounded worst cases, stopped cursor and mutation/rebuild. On warm same
map/geometry queries there must be no N build/sort/snapshot scan. A K-offset patch
copies O(K log S) paths and changes query coordinates, not N positions. Log N is
not a promise for all overlap or transformed scenes. A new selected map may still
require N index rebuild; show that residual explicitly. Count failed and discarded
builds, not just successful hot queries. Native callback elimination and measured
host results cannot be claimed until E2 host actually runs.

At phase F, use the approved same-machine warmed baseline and 125/500/1000 moves/s,
CPU ms/s, p50/p95 and input-to-present. Linux synthetic queries/.NET traces are not
Windows CPU, OS delivery, GPU/presentation or macOS execution evidence.

## 11. Review decision requested

Review this document, its evidence, `hover-e2-ownership-audit.md` and
`hover-e2-input-protocol.md` together as a proposal only. Explicit decisions:
composite atomic ownership with destructor audit; persistent authentic offsets and
atomic host publication; split broad-phase/result certificates; scroll-space bound
proof and fallback; P1 cursor, P2 barriers and P3 observable corrections with the
pointer-events-only exception; 64-slot pre-callback admission and explicit resource
rejection; exact action states/default effects; one outer-entry hover/lifecycle
budget and bounded recovery; full gates.

No PASS is asserted here. E1 remains frozen separately at 51b4fb5. Existing E1
implementation PASS does not approve E2, DOMsync integration or any CPU benefit.
