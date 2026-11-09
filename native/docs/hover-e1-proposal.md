# E1 proposal: immutable hit query accelerator checkpoint

Status: awaiting independent proposal review. This is a staged subset of approved
phase E, not phase E completion. Product defaults remain OFF; no remote/main work.
Base: c1c5548bb43ccb049cf315f4cfc05546a9cc8be5.

## Observed contracts

`aether_layout_result_s::hit_test_recursive` is the ordinary hit oracle.
`layout_hit_test_input` has different dynamic-scroll and atomic/text traversal
rules and includes chrome before descendants. Neither changes in E1.
`layout_children_paint_order` is shared classification with display-list paint.
`hit_with_pin` pins geometry and selected map and checks provenance before/after
callbacks. Contextual presentation mutates layout boxes during
`aether_contextual_transition_finalize_layout`, with rollback on failure.
The existing .NET InputTraceTests explicitly assert pending hover remains AFTER
mousedown/up/click. CursorAt currently returns the pending cursor even for another
point. E1 changes neither behavior: correcting either needs a separate reviewed
contract proposal and before/after traces in E2.

Chromium pinned 2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9 was read at
`core/layout/hit_test_cache.cc`, `HitTestCache::LookupCachedResult` and
`AddCachedResult`: exact point plus request compatibility and DOM tree version;
cache can decline queries. `core/input/mouse_event_manager.cc`,
`MouseEventManager::RecomputeMouseHoverStateIfNeeded`, clears dirty first and
recomputes at last physical location; this does not authorize changing Aether
barrier ordering invisibly. No Chromium executable was run.

## E1 implementation decision proposed

1. An internal layout-owned accelerator, gated by new CMake
   AETHERUI_ENABLE_HIT_ACCELERATION default OFF. Legacy public hit APIs remain
   the oracle when OFF; internal tests compare oracle and accelerated results.
2. Cache key is exact floating point x/y, policy, retained immutable selected map,
   layout geometry owner and presentation serial, plus document mutation,
   structure and style epochs. Query kind is ordinary-content only for E1.
   Cache includes misses. Cache is bounded to the latest query and latest index;
   retained map ownership prevents ABA, layout ownership prevents dangling boxes.
   Nonfinite points never cache or index. Viewport/DPI are baked into this layout
   geometry, not approximated by target bounds.
3. Index build uses the shared paint classification once per selected map,
   geometry/presentation serial and policy. For each box, store children in paint
   order and a balanced range tree over that order, storing union of conservative
   descendant bounds. Query visits higher paint ranges first, rejects only
   impossible bounds, then executes current exact traversal predicates.
   No spatial reordering may change winning paint rank. The tree is hierarchical
   at each parent coordinate/scroll space, not a flattened border-box grid.
4. Bounds include own border content and all descendants, never prune based on
   parent visibility or pointer-events or border box. Clips can be ignored for
   bounds (over-approximation); exact clip checks still run. For nonfinite,
   negative/unknown geometry and transform samples, mark unbounded and retain
   exact traversal. Finite nonsingular affine transforms may be conservatively
   mapped with outward-rounded corners. Singular matrices fall back to oracle.
   This checkpoint may deliberately mark all transformed subtrees unbounded if
   that is required to guarantee floating point conservatism.
5. Existing unversioned external scroll callback uses full oracle and never
   cache/index, because skipping a subtree can change callback effects/order.
   This is explicit fallback, not zero-work success. E2 will separately propose
   owned immutable scroll samples or a certified read-only offset source for the
   host. E1 does not claim to fix the host's measured callback hit cost.
6. Presentation mutations invalidate accelerator before mutation, including
   failed/rolled-back attempts; cloned layouts start empty, released layout drops
   index/map pins. Index builds stage locally and publish only after complete.
   OOM/build failure falls back to exact traversal without publishing partial
   structures; failure work remains counted. No callbacks during index build.
7. Add separate versioned diagnostic counters for cache hit/miss, index build
   attempts, build boxes/range nodes, query range nodes, exact boxes, fallbacks
   and fallback reason. Keep old counters and C ABI sizes unchanged. Index memory
   and map retention bounded by current layout + one selected map/index.

## Initial gates and limits

Differential first: random/boundary points, overlapping z-order, overflow-visible
children, clips per axis, hidden/pointer-events parent, text/atomic/generated
owners, transforms (including singular/nonfinite fallback), retained layout/map,
map replacement at same point, presentation invalidation, unknown scroll callbacks
and exception/OOM. Measured build work reported separately from warmed queries;
100/500/1000-card first-card hit must have bounded range/box visits, not hide O(N)
classification behind the hot counter. Existing content/input tests remain oracle.

E1 completion is only native ordinary-content foundation. Input chrome, host
scroll snapshot/certificate/pending cursor, barrier correction, .NET ON/OFF input
traces, independent whole E implementation review and F/Windows remain pending.

## E0 trace evidence captured while proposal review is pending

The unchanged base Core runtime passed all 16 existing real .NET input traces
with input counters both OFF and ON. The new standalone
`AetherUI.InputTraceTests --e-contract-baseline` records two historical contracts:
CursorAt at B returns A's cursor while A is pending, even though B's independent
cursor is Text; move/down/move/up produces down/up/click before B mouseover at
Render. These tests intentionally describe the baseline, not proposed behavior.
They do not authorize correcting either contract.

## Exact E1 safety clarifications for independent sign-off

- A cache entry is installed only after the complete query and all post-operation
  geometry/provenance/liveness guards succeed. Null caused by exception, OOM,
  stale inputs or invalidation is never cached as an authoritative miss.
- Certificate captures geometry-document identity and its mutation/structure/style
  epochs, base-map provenance document and selected-map provenance document with
  their epochs, selected immutable map owner, policy and presentation serial.
  Missing or unprovable provenance/liveness falls back to oracle or disables
  result caching. A non-null cached target retains its authentic identity token,
  verifies token owner/alive/attached, and never trusts raw address coincidence.
- The presentation serial never wraps. Saturation permanently disables reuse for
  that layout. Every finalize attempt invalidates before possible writes, even if
  it fails or rolls back. Relevant geometry changes and release_box invalidate.
  A new clone has its own empty accelerator and independent presentation serial.
- The accelerator is owned by its layout and MUST NOT strongly retain that same
  layout (no reference cycle). Only a query-local LayoutOperationPin owns geometry
  during execution. The accelerator may pin immutable maps and identity tokens.
- Bounds retain zero-size rectangles, inclusive endpoints, signed-zero behavior
  and float-addition endpoints from the exact oracle. Tests include +/-0 and
  nextafter just inside/outside boundaries. Unknown/nonfinite/negative geometry
  is unbounded; unbounded propagates through unions without false pruning.
- E1 does not project transform bounds with approximate float math. A transformed
  subtree's incoming bound is unbounded; inside that subtree the exact existing
  unmap executes before testing its local child ranges. Singular/nonfinite samples
  therefore retain the exact oracle rejection, and are counted as conservative
  paths. Every external offset callback uses the full oracle, with original
  invocation behavior/order and guard protocol.
