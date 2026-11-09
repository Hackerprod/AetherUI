# Hover work counters (phase A baseline, B1+B2 checkpoint)

Phase A added observability. B1+B2 separates immutable payloads and introduces
persistent legacy map patches, not an incremental resolver or hit-test cache.
The full resolver, publication, transition-context calculation, hit traversal and
input dispatch remain the correctness oracle. No fast path is enabled.

## Build and run

```
cmake -S native -B artifacts/hover-work -DCMAKE_BUILD_TYPE=Release \
  -DAETHERUI_BUILD_TESTS=ON -DAETHERUI_ENABLE_WORK_COUNTERS=ON
cmake --build artifacts/hover-work --parallel 3
ctest --test-dir artifacts/hover-work --output-on-failure
artifacts/hover-work/aether_hover_work_bench > hover-work.csv
```

The compile option defaults to OFF. An OFF build compiles instrumentation sites to
no-ops and exports compatibility functions returning zero counters and false for
support. In an ON build, collection is initially disabled on every thread. Call
`aether_work_counters_set_enabled(AETHER_TRUE)` on the thread doing the work.
`aether_work_counters_get_v1` and reset operate only on that thread. Reset does not
enable collection. Counters saturate instead of wrapping, allocate no memory and
never invoke callbacks. They aggregate all hosts/runtimes on that thread. They are
not per-host values or transactional state, and may include work from an aborted
prepare. Publications count only successful commits, before diagnostic replay.

The existing Validator `PerfReporter` can report these counters when started with
`--perf` and environment variable `AETHERUI_WORK_COUNTERS=1`. It enables collection
on its reporting thread on the first report, takes a baseline, and reports deltas
at the existing one-second cadence. Initial work before that first report is not
included. Missing optional exports in older libraries are tolerated. An OFF build
produces no work report. Changing the reporting thread starts a new baseline; the
reporter never resets shared counters. Host metrics and native thread metrics are
separate because attributing thread-wide counters to a single host would be false.

## What is counted

- `compute_nodes`: calls to the existing node computation, including generated nodes.
- `state_entries_visited`: old state entries visited during complete preparation.
- `revision_records_created`: successfully frozen node records.
- `revision_entries_created`: appended complete revision manifest entries.
- `revision_prepares`: valid-owner preparation attempts, including later failures.
- `revision_publications`: successful revision commits, including reentrant commits.
- `state_full_fallbacks`: valid legacy state requests sent to the full resolver.
  This is the one current fallback category, not a future typed invalidation diary.
- `map_entries_copied`: changed leaf records successfully inserted/replaced in a
  legacy map patch. In the phase A baseline it counted N entries copied into a
  temporary hash index (excluding a second full `freeze_backing` construction).
  B2 uses a persistent Patricia backing: the fixture now counts K=2 changed
  leaves, and a same-owner no-op counts zero. This counter does **not** count
  copied internal tree nodes/routes, deletions, full imports, or total allocation
  work. It may count completed earlier leaf changes in a later-aborted patch.
- `transition_context_entries_visited` and `transition_context_builds`: entries
  and invocations in the complete effective-containing-block helper, whether
  called by preparation or layout. These do not mean membership actually changed.
- `hit_queries` and `hit_boxes_visited`: traversal queries and recursive box visits.
- `paint_order_children_classified`: classifications by the shared child paint
  order helper. It can run for paint as well as hit testing; it is not hit-only.

CSV work columns are totals over measured iterations, excluding five warmups.
Divide by `iterations` for work per operation. Time samples include the operation
but exclude reading/printing counters. The same original fixture uses 18, 100,
500 and 1000 cards, two changed styles, child hover with no visual change, map
patching and first-card hit testing with and without explicit z-index.

`aether.hover-work-counters` asserts the current complete oracle's structural work
at 100/500/1000 cards, disabled behavior, thread isolation, and counter saturation.
Resolver assertions retain the phase A full-oracle baseline; the B2 map-patch
assertion requires K=2 copied leaf records instead of N. The separate
`aether.persistent-style-backing` test checks bounded path allocation, snapshots,
zero-allocation reads, history release and fault injection. These do not prove
independence of N for the complete pipeline. Later incremental phases must
preserve the full-oracle fixture and add separate fast-path expectations. Zero
visual updates still compute and freeze N records, and the revision manifest is
still fully materialized. Do not reinterpret zero updates as zero work.

## Chromium references consulted

Pinned Chromium revision: `2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9`.
Paths below are under `third_party/blink/renderer/` in that revision.

- `core/dom/element.cc`, `Element::SetHovered`: skips unchanged state and requests
  local recalculation based on dependencies, then calls pseudo-state invalidation.
- `core/css/style_engine.cc`, `StyleEngine::PseudoStateChangedForElement`: handles
  relational `:has` and descendant/sibling invalidation separately. A target-only
  optimization would not reproduce this behavior.
- `platform/widget/input/main_thread_event_queue.cc`,
  `QueuedWebInputEvent::FilterNewEvent`, `MainThreadEventQueue::QueueEvent` and
  `IsRafAlignedEvent`: coalescing requires event compatibility; scheduling retains
  low-latency/unbuffered exceptions and requests a processing opportunity. These
  are not permission to discard discrete events or delay the final pending move.
- `core/layout/layout_view.cc`, `LayoutView::HitTest` and
  `HitTestNoLifecycleUpdate`: updates lifecycle before hit testing and distinguishes
  total hit count from cache hits. Hence this phase measures actual box visits
  separately from queries instead of treating a low query count as proof of speed.
- `core/layout/hit_test_cache.cc`, `HitTestCache::LookupCachedResult`: matches exact
  point, DOM version and cache-compatible request. A previous target's border box
  alone is insufficient. No approximate cache is introduced by this checkpoint.
- `core/input/boundary_event_dispatcher.cc`,
  `BoundaryEventDispatcher::SendBoundaryEventsSameOrigin`: dispatches out before
  constructing common-ancestor boundary handling. Existing Aether event order is
  unchanged; event trace gates are still required before changing coalescing.
- `core/input/mouse_event_manager.cc`,
  `MouseEventManager::RecomputeMouseHoverStateIfNeeded`: dirty hover can be
  recomputed without a new physical move. This remains a correctness requirement
  for subsequent input changes, not a behavior implemented by these counters.

No Chromium executable was run. Native Linux measurements do not establish
Windows CPU, input-to-present latency, GC or renderer performance. Managed event
traces and full-pipeline allocation measurements remain pending. B1+B2 is only a
phase B checkpoint: payload separation and persistent legacy backing exist;
revision payload-directory/manifest migration, certified delta publication, and
agenda/context/hit fast paths remain pending before the overall optimization can
pass. See [phase B checkpoint](hover-phase-b-checkpoint.md).
