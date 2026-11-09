# E1: ordinary-content hit foundation, not complete phase E

Base: `c1c5548bb43ccb049cf315f4cfc05546a9cc8be5`. The exact E1 proposal
`8710113e6dacca7f811ea1170fb5e6c3dc90b9f2` received independent design PASS
(SHA256 `1285c355dbea78102a1a8ce041525c7751cc045b776f6f366c787d5e091893d1`).
Implementation is an isolated review candidate, not independently approved.
`AETHERUI_ENABLE_HIT_ACCELERATION` defaults OFF. No host opt-in or activation.

## What changed

`hit_with_pin` uses a layout-owned `HitAcceleration` for finite ordinary-content
queries without an external scroll callback and with provable live provenance.
The old `aether_layout_result_s::hit_test` remains unchanged as differential
oracle. Input chrome uses its original separate traversal. Unknown provenance,
external callbacks, nonfinite points, exhausted serials and failed staging use the
old oracle, with explicit reason counters. Inability to certify a retained foreign
legacy map never creates a new rejected hit: it uses the existing oracle contract.

An accelerator owns one selected immutable map, document epoch tokens, one index
and one exact point result. It never pins its containing layout; only the
operation pins geometry. Result cache includes exact misses only after every
post-query guard succeeds. Cached targets retain and check authentic tokens.
Geometry/base/selected-document mutation, structure and style epochs, selected map
identity, policy and nonwrapping presentation serial certify reuse. Viewport/DPI
are part of the owning geometry. There is no cursor/pending/capture host certificate
in E1; those remain E2 work.

The index is a balanced hierarchy of contiguous child paint-order ranges within
each parent coordinate space. Construction uses the existing shared paint-order
classifier. Queries descend higher paint ranges first and execute original exact
unmap, liveness, display, clip, visibility, pointer-events and own-content tests.
Bounds include the own rectangle and descendants, including degenerate endpoints
and overflow visible. Hidden/pointer-events-none parents never prune descendants.
Transformed, nonfinite or negative/unknown bounds are unbounded. Transformed
subtrees run the original exact unmap before querying their local ranges. This
avoids unsafe projection of inverse-floating-point acceptance into an AABB.

Every presentation-finalize attempt and box release invalidates the index, even if
finalize fails or rolls back. Clones start empty. Saturated presentation serials
permanently disable reuse. Switching map releases the prior index before staging
its replacement; OOM does not accumulate old/new map caches or partial indexes.

## Diagnostics and measurements

New additive `aether_hit_work_counters_get_v1` preserves the original work-counter
ABI. It exposes cache hits/misses, attempted/completed builds, build boxes/ranges,
query ranges/exact boxes, unbounded bounds and fallback reasons. Counters follow
existing opt-in, thread-local and saturating rules.

`allocation_requests` / `requested_bytes` count explicitly requested accelerator
storage and the shared paint-order vector, including failed requests. They exclude
allocator overhead, shared_ptr control-block overhead and implementation-specific
stable_sort scratch. They are NOT total malloc or retained-memory measurements.
The differential fixture additionally intercepts actual C++ new calls around the
complete query, including control blocks and standard-library sorting scratch.
It resolves the expected fixture node before that measurement; DOM id lookup is
not part of the hit query. No fixture lookup cost is reported as accelerator cost.

With counters ON, the catalog fixture measures 100/500/1000 flat cards, both with
and without explicit z-index. Cold builds visit 102/502/1002 boxes and construct
200/1000/2000 range nodes. Fifty alternating points then visit 147 exact boxes at
all three sizes and 686/882/980 range nodes, with one exact cache hit, zero rebuild,
zero fallback, zero paint classification and zero observed new calls. The 1000-card
stacked cold query performs 20 new calls requesting 308080 bytes. Construction is
explicitly O(N), not hidden under the warm result.

There are no accepted CPU/latency benchmarks in this checkpoint. Baseline timing
was collected concurrently with compilation and is unsuitable for speed claims.
The host always supplies a callback today, so it still takes the full traversal.

## Tests and scope

`aether.hit-acceleration` compares public selected-map/snapshot queries with the
old traversal across deterministic random points and exact/nextafter boundaries,
18 CSS scenes, own/ancestor clips, overflow visible, hidden/pointer-event parents,
z-order, generated/text/atomic ownership, real sampled transforms plus injected
singular/nonfinite matrices, zero/negative/unknown rectangles and signed zero.
It covers exact hit/miss cache, map changes at the same point, retained independent
maps/layouts, topology retirement, failed finalize, serial exhaustion, callback
order/effects/errors/release, dead foreign legacy provenance and allocation sweep.
OOM cases verify complete fallback or a fully completed recoverable stable_sort
operation; no partial index is published. Alternating A/B with failed B build
verifies both map pins release. Layout references prove no self-pin cycle.

`test_contextual_transition_frame` retains its ABI OOM assertions but explicitly
invalidates the query cache before injecting failure. It verifies an allocation
actually failed. A separate ON hot probe under fail-new=0 requires the correct hit
and zero failed allocation attempts, both for exact cache reuse and a different
point on the hot index. After each cold OOM, a valid query at the same point proves
the failure was not cached as a miss. The design reviewer confirmed this fixture
adjustment; it is not independent implementation approval. This preserves the query
contract while keeping the cold error boundary adversary.

The unchanged .NET input traces run with the E1 native library ON and OFF. The
separate `--e-contract-baseline` observations deliberately retain current pending
cursor and discrete-event order. E1 does not correct either latent contract.

Not complete: input-chrome indexing, certified immutable scroll inputs, host hit/
cursor/pending/capture certificate, barrier-order correction, incremental index
refit/map-hit serials, PerfReporter integration of the new E1 counters, complete
phase-E integration, full native/.NET regression suites, ASan and Windows/macOS
execution. New native index builds currently invalidate conservatively on any
captured document epoch or map change, so warmed point-query bounds are not bounds
on complete hover/style processing. All such work belongs to later reviewed steps.

## Reproduction

Use the repository's CMake 3.16+ toolchain. A focused Linux run is:

```sh
cmake -S native -B artifacts/e1-on -DCMAKE_BUILD_TYPE=Release \
  -DAETHERUI_BUILD_TESTS=ON -DAETHERUI_ENABLE_WORK_COUNTERS=ON \
  -DAETHERUI_ENABLE_STATE_INCREMENTAL=ON -DAETHERUI_ENABLE_CONTEXT_OWNERS=ON \
  -DAETHERUI_ENABLE_HIT_ACCELERATION=ON
cmake --build artifacts/e1-on --target aether_core aether_hit_acceleration_test -j2
ctest --test-dir artifacts/e1-on -V -R '^aether.hit-acceleration$'
```

Repeat in a fresh directory omitting HIT_ACCELERATION and verify CMakeCache says
OFF. Test objects derive the core compile definitions, so both modes execute real
core code. For managed traces, build AetherUI.InputTraceTests with the .NET 10 SDK,
-m:1 -p:UseSharedCompilation=false, copy the completed native library into that
output directory only when no test process is running, verify its hash, and run
the DLL normally and with --e-contract-baseline. Never overwrite a loaded library.

Chromium sources cited in the approved proposal were read at pinned revision
`2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9`; no Chromium process was executed.

## Local checkpoint results (independent implementation review still pending)

- Native core Release builds: E1 ON and default OFF pass.
- Registered CTest E1 ON differential: 27058 assertions, zero failures.
- Same differential source linked to default-OFF core objects: 26691 assertions,
  zero failures. Both include the 334-check existing public content-hit contract.
- Five C++ allocation-failure positions covered in the indexed build fixture,
  including standard-library recovery; actual aborted staging is distinguished
  from a successfully recovered sort. OOM pin-release and failed finalize pass.
- Additional current-core native tests pass: contextual paint geometry, rotated
  clip, transition frame, used geometry, atomic sampler, scroll model and scroll
  metrics. Transition frame passes ON and OFF with the explicit hot/cold probes.
- Real .NET Core input traces: 16 pass with each native ON/OFF library. The four
  historical pending-cursor/barrier observations also pass in each mode. The
  source and output library hashes were compared before executing each process.
- Full baseline native build was intentionally stopped after the baseline core
  and needed fixtures existed; no full native suite result is claimed. The full
  managed 100-test suite, its four known historical Linux failures, Windows and
  macOS execution were not rerun or reclassified here.

All results above are implementer checks, not independent implementation PASS.
