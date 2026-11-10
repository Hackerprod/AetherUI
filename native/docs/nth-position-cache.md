# Scoped nth-child positions

The style runtime shares a temporary structural position cache across the two
candidate loops and generated-pseudo preparation in one `prepare_tree` traversal.
It is destroyed before frozen records are built. No cache is stored in the DOM,
runtime, transaction, committed revision, or thread-local storage. The ordinary
`selector_match(selector, node)` entrypoint remains uncached.

## Lifetime and matching

Preparation buffers diagnostics and stages generated child links. It invokes no
external callbacks and does not publish DOM changes while the cache is alive.
Debug assertions check the document identity and structure epoch at the traversal
boundaries. If a future preparation path allows a topology-mutating callback, it
must end or invalidate this scope explicitly; the assertion is not invalidation.

The internal matcher overload threads the same context through every combinator
and nested `:is`, `:where`, `:not`, and `:has` call. Only the initial styled subject
is normalized to a generated pseudo's host. Traversal candidates remain DOM-only,
and `:has` retains its relative anchor. Generated content, anonymous layout nodes,
comments and text do not contribute element ordinals. Document-parent and detached
semantics are preserved. Duplicate-pointer synthetic fixtures retain their first
occurrence, while subsequent positions still count each occurrence.

Ordinals are one-based `size_t`; zero means nonmembership. A checked increment
prevents wraparound. An+B matching compares magnitude and remainder without
narrowing the ordinal to `int` or forming an overflowing `position - offset`.
The existing signed-int predicate remains available to its existing callers.

## Activation and allocation

- Parents with at most 128 raw child entries scan normally.
- Any scan examining at most 128 entries creates no cache metadata.
- A more expensive successful scan memoizes only its queried element position.
  Partial-map misses still scan. Missing subjects do not create metadata.
- A parent starts with a checked budget of 16 times its raw child count. Only new
  expensive successful scans subtract from it, saturating at zero. Hits do not
  spend budget or trigger complete construction. Unrepresentable budgets decline
  caching and keep the scan result.
- Completion requires both zero remaining budget and at least
  `ceil(maximum observed ordinal / 8)` distinct memoized positions.
- A complete map is built in temporary storage and published only when finished.
  No map reserves the raw child count, which can mostly consist of skipped nodes.
- Even the outer map is constructed lazily using `optional`, because some
  standard-library empty map constructors allocate. No-nth and cheap queries
  therefore do not construct either cache map on any implementation.

Every completed map adds one parent scan only after at least 16 parent-size units
of scan work were paid. Added construction visits are bounded by that paid work
/ 16. This is a child-entry visit bound, not a runtime or allocator-cost guarantee.
Storage on valid DOM trees is O(participating parents + memoized/complete elements).
Peak storage includes the old memo map and new complete map coexisting.

Allocation failure propagates to `prepare_revision`'s existing transactional OOM
handling. A partial map is never treated as complete. The caller receives no
transaction and committed styles/topology stay unchanged; a fresh retry gets a new
scope. Scope disposal also prevents stale entries after mutation, reparenting,
stylesheet/media/cache changes, document replacement, or address reuse.

## Verification and reproduction

`aether.selector-nth-cache` covers cached/generic recursive differential matching,
independent structural and numeric oracles, activation thresholds, generated and
synthetic boundaries, fresh-scope mutation/address reuse, allocation faults, and
requested allocator memory for sparse, repeated, text-heavy and many-parent cases.
`aether.nth-cache-runtime` compares the same public runtime entrypoint with cached
and uncached dispatch through a test-TU-only seam. It compares the full style
manifest, payloads, contextual sidecars, custom properties, revisions and change
masks, plus reentrant diagnostics, abort/retry and integrated allocation failures.
Neither test adds exported hooks. Checks stay active with `NDEBUG`.

Build the normal native CMake project with `AETHERUI_BUILD_TESTS=ON`, then run
`ctest --test-dir <build> --output-on-failure`. Focused sanitizers use
`-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined`; leak detection must
be reported separately. Passing with `detect_leaks=0` is not leak validation.

The opt-in `aether_nth_cache_runtime_benchmark` uses only public C ABI calls. Compile
the identical source against fresh exact-base and candidate builds. Run alternating
isolated processes after other compilation/tests finish. Its optional mode filter
allows separate per-workload RSS observations. Timings include scope creation,
map construction, preparation, commit and destruction. Output opacity checks occur
outside timed intervals. Requested allocations and RSS measure different things;
neither proves absence of leaks. Synthetic timings are not application-wide claims.

## Linux/GCC 14.2 validation snapshot (2026-11-10)

The exact native base was main `9c5cff1860cd27263b3bbbe419168af7c044a638`
(440 native Git blobs verified). Fresh Release base and candidate builds used
identical flags. Final checks passed: 264 CTests, six focused ASan/UBSan tests with
leak detection disabled, and the installed C package consumer. The 176 public C
symbols were unchanged. The matcher test passed 2,049,812 checks and 294 allocation
fault points; the runtime test passed 43,865 checks, 1,158 general preparation
allocation faults and 228 matcher-specific faults. An independent review repeated
the unit checks and 2,000,972 arithmetic comparisons against a wider oracle.

The runtime projection compares style/payload/metadata, custom properties, epochs
and change masks. Retired entries without a style handle use a common label to
avoid dereferencing dead nodes; it does not prove individual retired-node identity
or distinguish permutations among identically labelled entries with equal masks.
Rollback assertions separately compare actual frozen records and topology.

Cache-only requested peak bytes (including metadata and temporary maps) were zero
for eight child-33 queries among 65,536 siblings; 328 for either one/eight tail
queries or 4,096 text entries followed by one element; 1,624 for 32 distinct late
subjects; and 1,064,872 for 128 complete 256-element parent indexes. These values
are allocator/build observations, not a portable ABI or RSS measurement.

The final timing pass ran 35 cases, five alternating process pairs per case, and
five samples per process. Each process handled one exact case. Small cases used
100 or 1,000 resolutions per sample; larger cases used three. Thus there were
five independent processes per version/case, not 25 independent experiments.
Both versions checked actual opacity results outside timed intervals.

| Workload | Base process median | Cached process median | Median paired cached/base ratio |
| --- | ---: | ---: | ---: |
| Dense, 4,096 siblings | 59.84 ms | 32.40 ms | 0.537 |
| Dense, 1,024 siblings | 6.81 ms | 5.75 ms | 0.881 |
| 128 parents, 256 children each | 495.85 ms | 521.50 ms | 1.042 |

All five dense-4,096 pairs improved (ratios 0.516–0.703). The first pass also had
slower small/sparse cases, so a separate ten-pair check repeated the five suspect
cases and ten identical-base pairs measured background variation:

| Repeated workload | Median paired cached/base ratio |
| --- | ---: |
| One tail-targeted rule, 4,096 siblings | 1.065 |
| Child 33, seven repeated rules, 4,096 siblings | 1.052 |
| No nth, 4,096 siblings | 0.994 |
| One selected sibling | 1.001 |
| Dense, 128 siblings | 0.991 |
| Identical base versus itself, no nth/4,096 | 0.987 |

The identical-base control ranged from 0.894 to 1.146. This variation does not
establish that every slowdown is noise. There is no universal speedup or
regression-free claim. These initial measurements suggested roughly 5–6.5%
slower sparse/cheap medians and motivated the controlled follow-up below.
The policy trades temporary allocations for a large-dense-list improvement;
its 128/16/8 constants are conservative heuristics, not optimality guarantees. No-nth/cheap cases added no allocations; a tail query
added four, regardless of repeats.

Median whole-process peak RSS was approximately unchanged: 26,204 KiB in both
versions for dense 4,096, and 189,616 versus 189,640 KiB for 128×256. This includes
DOM, snapshots, allocator retention and process startup, and is not the peak cache
allocation. Windows/MSVC, macOS, 32-bit execution and leak freedom were not
validated in this local snapshot.

For an isolated case, run the benchmark with `mode siblings parents repeats`,
for example `aether_nth_cache_runtime_benchmark dense 4096 1 1`. Running it with
only a mode selects that workload family; no arguments run all families. Keep
raw per-process samples and distinguish pooled medians from paired-process ratios.


## Follow-up: isolate sparse/cheap costs before publication

The cache lookup now keeps fallible memo/bulk construction in a separate helper.
A successful scan's maximum observed ordinal is its returned position, so its
third, redundant result field was removed. Misses were never memoized. The
128/16/8 policy, stored positions, allocation order and transactional behavior
are unchanged. On the tested GCC/x86-64 build, the lookup function shrank from
0x761 to 0x20e code bytes, its stack frame from 0x98 to 0x18 bytes (excluding saved
registers), and the two-field scan result returns in registers instead of through
an indirect 24-byte result buffer. This is a narrower fast path, not evidence of
a fixed whole-runtime percentage gain.

Additional experiments removed replacement allocation counters from timed runs.
A standalone harness loaded base, the initial cache version, and the refined
version through explicit RTLD_LOCAL API tables. Objects never crossed modules.
Loader binding audits found no cross-module Aether C self-bindings in either
load order or the two-copy control. Timing separately recorded preparation,
commit and cleanup, alternated AB/BA samples, reversed library/fixture creation
order, and pinned each child process to the same available CPU. No compilation
or other validation was running concurrently.

The main matrix used 126 processes: seven comparisons/controls, nine workloads,
and two load/creation orders, with 12 paired samples per process. This is two
independent processes per comparison/workload, not 24 independent replications.
Test-only runtime images also toggled cached/uncached dispatch in the same binary
using the **same DOM and runtime**, isolating cache behavior from module layout
and different fixture addresses. These switches are not part of the product ABI.

Median paired refined/base ratios for the two load/creation orders were:

| Workload | First order | Reversed order |
| --- | ---: | ---: |
| Child 33, seven rules | 1.0155 | 0.9763 |
| One tail-targeted rule | 1.0525 | 0.9964 |
| Dense, 1,024 siblings | 0.7747 | 0.7709 |
| Dense, 4,096 siblings | 0.6475 | 0.6300 |

The same-DOM cached/uncached ratios were 1.0083/1.0027 for child 33 with seven
rules and 0.9705/0.9807 for one tail-targeted rule. The initial 5–6.5% penalty was
not consistently reproduced as a cache cost. Dense improvement remained present.
The two-module and single-module tests have different memory working sets; their
absolute timings should not be combined with the earlier isolated-process data.

One matrix comparison nevertheless showed the refined version slower than the
initial cache on 32 sparse subjects. Rather than discard that observation, a
separate 48-process experiment repeated that case with six processes per load
order and comparison, interleaving one-image and two-image controls of the
initial cache implementation. Medians of the per-process paired medians were:

- Refined versus base: 0.9939.
- Refined versus initial cache: 1.0055 (range 0.9406–1.0502 across processes).
- Identical initial cache, one-image control: 1.0090.
- Identical initial cache, two-image control: 1.0225.

The earlier large sparse signal did not persist in this replication. This does
not prove zero regression, establish optimal thresholds, or show that every
slow result was noise. It provides no demonstrated material inherent sparse/
cheap penalty under the tested conditions. The independent reviewer accepted
the narrower lookup with these limits and without retuning the policy.

After the refinement, all 264 CTests, six focused ASan/UBSan checks with leak
detection disabled, the installed C consumer and the unchanged 176 public C
symbols passed again. An independent fresh unit run retained 2,049,812 passing
checks and the same allocation-fault/memory outcomes. Platform and leak limits
from the earlier snapshot still apply.
