# E2 proposal: certified host hit and scroll input

Status: PARTIAL PROPOSAL, pending independent design review. No product code,
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

## 2. Concrete sources read so far

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

Pending before design submission: inspect pinned Chromium scroll/transform hit
implementation directly, then cite exact functions and translation limits.
No Chromium executable was run. No new product tests or benchmarks were run in
this proposal-only step.

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
Pending/cache records hold a bounded shared lease and authentic target descriptor;
no per-point deep copy or new DOM-wide snapshot, no historical owner chain.

Open implementation choice to resolve in the completed proposal: additive native
composite query lease versus individually retained native handles. In either case,
query-local pins survive wrapper dispose/reentrant callbacks, and retain/release
must follow DOMsync's approved owner-thread/finalizer contract rather than invent
an unsafe cross-thread destructor route.

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

Separating index and result validity should avoid rebuilding N boxes for a state
write with an unchanged selected map and geometry. A changed selected map remains
a conservative rebuild until a separately proved semantic order/bounds certificate
exists. E2 must report that residual rather than claim O(K) for paint-only crossing.

## 5. Conservative index per scroll space

Child ranges remain in their parent's local content coordinates. A query performs
existing exact unmap/clip first, reads its immutable offset and adds exactly the
same float/clamping result as the ordinary oracle before querying children.

For an incoming bound of a node whose descendants may scroll, E1's raw descendant
union is insufficient. Proposed initial conservative construction:
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

Open in this partial: exact other-action barrier list, bounded reentrant prefix
rules, post-mouseout retargeting, stationary scroll reevaluation and failure policy.
These must be resolved explicitly before submission, not guessed in implementation.

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

## 8. Next completion step

Finish source-backed decisions above and publish a complete proposal for independent
review only when reviewer capacity permits. Planned gates: native differential
ordinary/chrome against callbacks with fixed snapshots, nested scroll/transforms,
retained/mismatched snapshots and identities, OOM/staging/retirement/reentrancy;
real .NET enabled/disabled event traces for P1/P2 and existing hover stabilization;
work/allocation counters that include snapshot patches, index builds, fallback,
interop, callbacks and pending drains. No Windows CPU claim before phase F.
