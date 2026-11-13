# S1-R0b: experimental immutable payload capture only

**Default OFF.** Compile the runtime translation unit with
`-DAETHER_REUSABLE_PAYLOAD_CAPTURE=1` only for the explicit capture experiment.
With the macro unset or zero, the payload include, runtime member, construction
and commit swap are absent at preprocessing: the production path remains R0a,
without snapshot overhead or new payload OOM points. No consumer exists yet.
The focused payload and provenance tests explicitly enable this experiment.

Private snapshot retained by the runtime, prepared fully before publication and
swapped without allocation before diagnostic replay. Every node is still computed,
matched, accounted, frozen and published. **No cache lookup or fastpath is enabled.**

Each entry owns style, normal and backdrop scopes, symbolic color metadata,
Prepared typography (before global final-record accounting), and independent
semantic measures without a document pointer. Math is recursively copied into
const allocations, not trusted merely because an input shared_ptr is const.
Identity tokens retain node, parent, document and request-root provenance; source
and published epochs/generations have separate names. Exhaustion remains an
explicit veto on future eligibility, not a reason to skip computation today.

The test-only measures reconstruction in `test_reusable_payload.cpp` proves
ownership isolation. It does NOT prove that computed measures may be rebased
onto a new environment or root-font context. No production reconstruction exists.

## Checks

`sh tests/native_reusable_payload/run.sh NEW_EMPTY_BUILD_DIRECTORY` builds core
from source and runs semantic capture tests. `tests/native_hover_provenance/run.sh`
adds fault injection, mutation, callback reentrancy and saturation tests plus
atomic revision tests. Canonical comparisons against R0a use the existing
native hover baseline corpus (no broad pixel/rendering correctness claim).

## Cost experiment

`native/tests/test_reusable_payload_cost.cpp` uses the exact baseline fixture,
with new/delete accounting (ordinary and aligned). Build this same test file in
the R0a and R0b trees with C++20, -O1, and AETHER_HOVER_PROBE 0, 1 and 2. For the R0b capture experiment also
set `-DAETHER_REUSABLE_PAYLOAD_CAPTURE=1`; omit it for the default-off comparison. Link the
respective core objects. Name binaries `r0a-cost-N` and `r0b-cost-N` in BUILD.
Then run `python3 tests/native_reusable_payload/measure_cost.py BUILD OUTPUT`.
It alternates variants, five fresh processes per combination, 18 and 1800 cards,
cold/nochange/transition. The previous revision stays alive during publication.

Requested bytes, live delta and peak-above-start count C++ requested allocation
sizes, not allocator bookkeeping, malloc outside new, or OS RSS. Const references
share existing immutable style/scope/color storage; retained logical storage is
not additive to their original owner's size. `live_delta` includes the retained
revision and publication changes, and must be compared against R0a. Wall times
include instrumentation and are noisy diagnostics, not a performance acceptance
threshold or user-visible hover latency. Capture is expected to cost memory and
allocations; no speedup is claimed.

Known inherited limit: when the non-state counter is saturated, an additional
partial mutation can evade the legacy commit guard. Exhaustion blocks reuse;
R0b does not repair that legacy transaction behavior.

### Recorded diagnostic result, capture ENABLED (2026-11-13)

Compared with local R0a `baa8aa9` (published tree equivalent of `e3aa196`).
1800 cards / 3603 elements add **21617 allocation calls** and **5029772 requested
bytes per resolution**. Cold retained requested bytes increase by 5029756;
peak above start increases by 5029772, with the old revision retained. In later
phases the old payload is replaced, so live *delta* is unchanged; this does not
mean payload residency is free. Modes 1/2 still compute all 3603 nodes.
Counters-mode nochange medians were 53.645 ms (R0a) versus 60.452 ms (R0b).
Other modes were noisy; inspect raw samples/ranges. This is capture overhead,
not an optimization win and not a browser or host C# latency measurement.

Checks run using unchanged R0a core objects plus rebuilt runtime/test translation
units: payload PASS, provenance 312 OOM points, atomic 5573 checks/22 aligned
failures; canonical 21 fixtures × 3 revisions byte-identical. Independent clean
build/review is still required before publication. No compiler dependencies were
installed, no Actions were enabled and no public API was changed.

### Default-OFF verification

The final production-default build reproduces R0a counts exactly on 1800 cards:
252318/245107/245112 allocation calls and 75056716/64864124/64866996 requested
bytes for cold/nochange/transition. Live deltas and peaks likewise match baseline;
see `default-off-cost.csv`. Fault injection returns to 289 points (ON: 312).
Seven canonical fixtures × three revisions are byte-identical to R0a. Runtime
object files are not byte-identical (source locations/assertion metadata change),
so no binary identity is claimed. The excluded production feature introduces
neither a retained snapshot nor extra capture allocations or capture OOM points.
