# Certified rule-index candidate ranges

## Contract

Runtime-owned compiled rules have stable IDs and are each placed in one anchor
bucket. A completed runtime build now verifies ID uniqueness once and certifies
that its candidate ranges are disjoint. Generic indexes still borrow mutable
rules and retain the original first-rule-ID-wins query, including duplicate IDs
and repeated insertion.

The certificate and buckets are private. Bucket views are const. Generic copies
lose both runtime certificates; explicit synchronous runtime-to-staged copying
restores them only after the copy completes. Move/swap transfer certificates with
the buckets and leave moved-from indexes empty and uncertified. Clear/add/rebuild
invalidate before fallible work. Sheet changes still publish via the existing
transactional swap, after all allocations and validation finish.

Certified queries append ID, class, ASCII-folded type and universal ranges in the
original order. Only matching class-bucket addresses need deduplication: eight
addresses are inline, with a lazy local hash set beyond eight. Missing classes
consume no slots. Existing output prefixes are not deduplicated, and no query
state or bucket addresses survive the call. There is no public C ABI change.

## Correctness and fault coverage

Validation on Linux, GCC 14.2.0, against base
`a4a0fea3e23cb5f6e10bb3361ec4be6851bdcec3`:

- Release CTest: 262/262 passed, including existing selector, cascade, pseudo,
  callback/reentrancy and revision tests.
- Rule-index test: 60,878 checks; 20,000 deterministic generated cases;
  checksum `8322279257969885009`. Exact candidate pointer/order comparisons cover
  mixed anchors, case and non-ASCII names, shared source positions, caller
  prefixes, mutable generic IDs, copies/moves/swaps and 0/1/8/9/16/128 buckets.
- Runtime differential: 3,778 checks. Certified versus generic queries compare
  the full scalar/payload manifest, measure/color metadata, custom properties,
  entry order and update masks. Only the separate documents' identities are
  normalized. Fixtures cover selector lists, sheet layers, media changes,
  generated pseudo-elements, class mutation, cache clearing and remove/re-add.
- Allocation-failure sweeps cover candidate output/overflow growth (up to 132
  points), build/certification (54), trusted and generic copies (33 each),
  stylesheet add (102), remove (54) and revision preparation (265). Tests assert
  committed sheets, counters, epoch, cached records, revision and output
  contracts survive failure, followed by an exact successful retry. Actual
  ordinary and aligned/PMR allocation families are exercised.
- ASan+UBSan passed the two new tests plus existing source-order/reentrancy and
  atomic-revision tests with `halt_on_error=1` and `detect_leaks=0`.
- Installed C package consumer passed. Exported symbols exactly match the base.

Leak validation remains unconfirmed. The environment's previously observed
LeakSanitizer/ptrace limitation was not retried, and the sanitizer runs above do
not establish leak freedom. No Windows/macOS validation is claimed.

## Isolated candidate collection

The real certified query is compared with its unchanged generic fallback in the
same Release executable. Five alternating paired samples, median nanoseconds,
512 matching rules, fresh output vector each iteration. Allocation counts reserve
output capacity separately, isolating temporary bookkeeping.

| Matching buckets | Generic ns | Certified ns | Generic allocations | Certified allocations |
| --- | ---: | ---: | ---: | ---: |
| One class | 15,933.06 | 80.79 | 518 | 0 |
| One class repeated 24 times | 45,193.57 | 154.44 | 518 | 0 |
| Eight classes | 16,507.72 | 264.19 | 518 | 0 |
| Sixteen classes | 16,610.86 | 669.77 | 518 | 9 |
| 64 missing class tokens | 100.62 | 100.67 | 0 | 0 |

These large differences apply only to candidate collection, not the engine as a
whole. Cases with empty indexes, 1/8/64/512/4096 rules, ID/type/universal anchors,
misses, and both fresh/reused output capacity are retained in the harness.

## Public-ABI runtime measurement

The same benchmark source was separately linked against freshly built exact-base
and candidate shared libraries, both Release `-O3 -DNDEBUG -ffp-contract=off`.
Five complete process pairs alternate base/candidate order, without concurrent
build work. Each process records five samples per case, with twenty complete
fresh-output prepare/commit/destroy revision cycles per timing sample. The table
uses all 25 samples per version; each process produced checksum `154612`.

For 32 matching DOM nodes and 256 rules:

| Workload | Base resolve ns | Candidate resolve ns | Base allocations | Candidate allocations |
| --- | ---: | ---: | ---: | ---: |
| One class | 838,156.70 | 531,246.70 | 9,969 | 1,361 |
| Repeated class | 1,306,715.50 | 542,071.45 | 10,129 | 1,521 |
| Sixteen classes | 1,249,625.05 | 903,953.70 | 10,127 | 1,935 |
| Missing classes | 153,142.50 | 147,429.95 | 1,489 | 1,489 |

This includes index copying, node-info creation, cascade, freezing and revision
publication, but no layout/rendering. It remains a synthetic style workload, not
an overall browser/engine speedup. Small/empty cases vary: the slowest candidate
median relative to base was the one-rule/32-node case, 145,930.65 to 148,100.95 ns
(about 1.5% slower); no universal improvement is claimed.

Certification adds build-time work. Adding 256 rules to an existing seed sheet
increased allocations from 2,865 to 3,127 for one class, and from 2,952 to 3,214
for sixteen classes. Observed timing medians for these builds were near the
baseline (roughly -7% to +2% across the fixtures); this does not eliminate the
extra uniqueness-check cost. The empty incoming-sheet case still rebuilds the
seed rule, adding two allocations.

## Reproduction

Build the usual Release suite and run `ctest --test-dir build --output-on-failure`.
The candidate test executable accepts `--benchmark` for the isolated CSV.
`aether_rule_index_runtime_benchmark` is an opt-in CMake target. For base/candidate
runtime comparison, compile `tests/benchmark_rule_index_runtime.cpp` unchanged
against each version's shared library, set each executable's library search path
to that build, and alternate whole-process order. Keep raw samples and report
build/rebuild allocation costs alongside query/resolve timings.
