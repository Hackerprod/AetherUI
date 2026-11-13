# R1: immutable hit-order metadata

## Contract

Only hit traversal uses this optimization. Painting still calls the original
`layout_children_paint_order` with its existing style policy. Both ordinary and
content/scrollbar hit traversal borrow the same per-result ordering metadata.

Construction traverses `result->root` after fixed-container finalization,
asset freezing, contextual transition finalization and used-geometry validation.
It does not traverse provisional allocation records. Metadata is published only
when the complete temporary table succeeds. Allocation failure or the bounded
one-million-box/edge budget leaves the layout usable through the old algorithm.

States are explicit through representation: missing table/entry is unavailable;
an existing empty entry is DOM order; a nonempty entry is ordered. The latter
retains the old stack-item sequence, including stable ties. No sorting rules,
stacking contexts, top-layer behavior or flex semantics have been expanded.
The move-only traversal view borrows entries without copying vectors.

Eligibility requires snapshot policy, no supplied override and exact retained
asset identity. A base-equal or empty override is still excluded. Clones start
without metadata. Coordinates, clips, scrolling, transform unmapping,
visibility, pointer-events and scrollbar geometry are evaluated per query.
Existing operation pins, checked offset callbacks, epoch checks and liveness
checks remain in place. The internal instrumentation sink is thread-local;
this does not make existing non-atomic result references thread-safe.

## Chromium comparison

Inspected the original Blink `PaintLayerStackingNode` implementation:
https://chromium.googlesource.com/chromium/src/+/4dfb50605af1bb51dc91bd6d480538deb2a21a23/third_party/blink/renderer/core/paint/paint_layer_stacking_node.cc

Blink keeps z-order lists and marks them dirty when stacking-affecting styles
change, rather than sorting them on each query. Aether's immutable result is a
natural lifetime boundary for the analogous order metadata, without a mutable
invalidation mechanism in readers. Blink's stacking-context collection and
top-layer handling are not equivalent to this engine's local ordering profile;
this patch deliberately reuses the existing Aether classifier, not Blink code.

## Validation

See accompanying executable test and measured report. Benchmarks compare the
same result with its optional table present versus temporarily removed, so the
baseline executes the unchanged old ordering algorithm. These isolate ordering
costs, not the original flexbox page, browser latency or whole UI frame time.

### Recorded run

Linux x86_64, GCC 14.2.0, C++20 `-O1`, shared container (not a dedicated
performance machine). 31 cold samples and 101 hot samples per case. Metrics are
microseconds. `tests/hit_order_cache/report.log` contains every 100/1,000/10,000
DOM/mixed case and counters. `run.sh` rebuilds the full core; `regressions.sh`
runs the additional regression suite against those objects.

| 10,000 children / query | baseline p50 / p95 | cache p50 / p95 |
|---|---:|---:|
| DOM, late in reverse traversal | 587.615 / 753.351 | 372.693 / 669.138 |
| DOM, early in reverse traversal | 317.577 / 499.007 | 0.068 / 0.075 |
| DOM, miss | 612.027 / 1035.802 | 368.096 / 497.032 |
| mixed z, late | 1043.182 / 1391.860 | 435.202 / 749.395 |
| mixed z, early | 550.279 / 705.326 | 0.076 / 0.087 |
| mixed z, miss | 1004.717 / 1452.031 | 469.861 / 909.867 |

Every eligible hot sample performed zero classifications, sorts and allocations.
The noncopyable borrowed view establishes zero vector copies structurally; this
is explicitly **not** a dynamic copy counter. The mixed baseline allocated twice
per query. At 10,000 children its 101-query batch allocated 30,300,000 cumulative
bytes; that is allocation traffic, not resident memory.

Cold metadata construction at 10,000 children costs p50/p95 1844.624/2186.231 us
(DOM) and 2050.466/3234.936 us (mixed). Logical retained bytes are 402,304 and
602,304 respectively, excluding hash-node links and allocator overhead. One cold
construction made 10,015/10,017 allocations and requested 638,960/938,960
cumulative bytes. This is a latency-memory tradeoff, not a free optimization.

The first measured implementation regressed some long DOM queries because it
looked up empty child order in the hash table for every leaf. The final version
uses the invariant empty order directly for eligible leaves. The earlier report
is preserved as `report-before-leaf-fastpath.log`, including regressions and
noisy p95 results. These are single-container measurements, not guaranteed speedups.

Focused tests cover order equivalence, scroll/clip/hit owners and input parts,
retained snapshots, clone exclusion, base-equal/empty override exclusion,
distinct assets, effective-map exclusion, and public callbacks for both input
APIs (mutation, detach, document destruction, reentry and external result release).
All 34 optional cache allocation failpoints were swept; the fallback remained
usable. The full geometry/lifetime OOM sweeps now permit valid optional cache
failure, verify complete geometry/order, and continue through every ordinal;
mandatory failure and clone contracts are unchanged.

No full original flexbox-page visual comparison, Chromium timing comparison,
real-font shaping, or sanitizer run is claimed by this report. This patch does
not resolve the separate original-page render rejection or missing font SDKs.

The explicit `display:contents` fixture is rejected by the existing legacy
layout path (`AETHER_ERROR_INVALID_STATE`) before metadata publication. Rejection
without a result is tested; its order/hit differential is **NOT_RUN**, not a
claimed supported scenario. All 22 additional regressions passed, listed in
`tests/hit_order_cache/regressions.log`.
