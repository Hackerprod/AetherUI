# Semantic style diff validation, 2026-11-10

Baseline: `dfbdcf3604f27def4312a2f0b81aa25dee838d58`.
Compiler: GCC 14.2.0, C++20. Functional suite: canonical CMake Release build.
This report concerns the final-state invalidation change, not a parser, layout,
resource-loading or ABI change.

## Functional and failure checks

| Check | Result |
|---|---|
| Full native Release CTest | 243/243 passed, no skips |
| Selected Debug ASan+UBSan tests | 11/11 passed |
| Direct classifier test | 1,626 exact comparisons, zero allocation calls |
| Registry/dependency inventory | 86 keys, four aliases, four no-ops, nine visual percentage consumers |
| Acceptance through both APIs | Passed, including same-flat percentage geometry and retained snapshots |
| Focused metadata-only prepare OOM sweep | 302 injected failures, then successful retry with impact 0 |
| Focused percentage-only prepare OOM sweep | 287 injected failures, then successful retry with impact 7 |
| Successful publication | Zero new calls in focused OOM tests; zero allocator calls in benchmark |
| CMake install, exported target, C11 consumer build/link | Passed |
| Official installed-consumer runtime after baseline expectation correction | 1/1 passed |

The sanitizer selection comprises frozen-style, frozen-style-oom,
frozen-backing-oom, immutable-style-revision, transition-runtime,
style_revisions_atomic, frozen-paint-callback-pins, style-diff-classifier,
style-diff-catalog, style-semantic-diff and style-semantic-diff-oom.
It is not a claim that the entire native suite ran under sanitizers.

LeakSanitizer cannot run in the ptrace-based validation environment.
ASan ran with `detect_leaks=0:halt_on_error=1`; UBSan with `halt_on_error=1`.
Memory leaks are therefore not verified. The malloc interposition test that
intentionally skips under ASan passed in the complete Release run.

The .NET binding suite was not executed because the SDK was unavailable in this
executor. The public ABI and host code are unchanged, but that does not substitute
for running binding-specific tests.

## Installed-consumer baseline mismatch

The first official installed-consumer run failed because its HTML comment test
still expected `literal&amp;\r\n` after newline normalization was already part of
the baseline. The same failure was reproduced against the baseline shared
library. Only the test expectation is aligned to `literal&amp;\n`; the parser and
its input fixture remain unchanged. Red/green validation is recorded separately
from the invalidation regressions so this pre-existing mismatch is not hidden.
The original consumer exited 1 against both baseline and candidate. The corrected
consumer exited 0 against both, and the fresh installed CMake consumer passed 1/1.

## Reproduction

Configure/build the native project with tests enabled, run CTest, install it, then
configure the independent `native/tests/package_consumer` project against that
installation. Use the same compiler/Release flags for both benchmark libraries.
The benchmark source and final-state contract are linked below.

- [Contract and test targets](semantic-style-invalidation.md)
- [Standalone benchmark source](../tests/benchmark_semantic_style_diff.cpp)

The measurements below use repeated, identical-fixture baseline/candidate runs.
No browser runtime was executed; the Chromium comparison was source-only.

## Repeated benchmark, fixture v2

The rich fixture verifies two backgrounds/gradients, two text shadows, two box
shadows, two backdrop filters and three transforms. Two runs use 30 measured
samples following ten warmups per fixture/scenario/phase. Both cores use GCC14.2,
`-O3 -DNDEBUG -ffp-contract=off`.

All legacy-resolution and contextual prepare+commit medians improved in both
runs, with no repeatable >5% regression in those complete operations. Rich1000
no-op prepare+commit fell from 77.75/76.73 ms to 35.36/35.45 ms. Rich1000 mixed
fell from 81.54/80.54 ms to 39.48/42.06 ms. Rich1000 no-op allocator calls fell
from 514105 to 178105 and requested bytes from 53058860 to 19498860. Simple
fixtures did not increase allocation counts. All 720 measured candidate commits
allocated zero bytes/calls.

Raw commit duration also includes destruction of the old revision. The controlled
retention diagnostic measured 1010 frees during commit in both cores, zero when
an outside handle pinned the prior revision, and 1011 when releasing it afterward
(the extra free is the handle). The previously observed rich1000 commit increase
did not reproduce with the final controlled fixture. This identifies old-revision
destruction within the API interval, but does not prove a specific cache/heap
cause for timing variability.

Subphase results are not hidden by the total: rich1 no-op commit medians rose
from 0.1505/0.1305 microseconds to 0.1755/0.1410 microseconds, a 25/10.5 nanosecond
increase, while complete prepare+commit improved 53.9/51.9%. Consequently the
budget passes for complete resolution/preparation-publication, not every tiny
subphase considered independently. Measurements came from a shared executor;
these figures are not a promise of universal speedup.


## Integrated main validation

The implementation was integrated without conflicts on main
`fc329459ffb9f263b5eb382b6db3d8b7617f3749`, which already contains explicit opacity
inheritance. The integrated implementation commit is
`ff0fd2cc95fbf017f604de9cd717c4aaeecac139`; its three product files are byte-identical
to the individually reviewed diff implementation.

- Canonical Release build and CTest: **244/244 passed**, no skips.
- Selected ASan+UBSan: **13/13 passed**, adding opacity-inheritance and
  opacity-pipeline to the previous eleven. Leak detection remains unavailable.
- Fresh installation and official external CMake/C11 consumer: **1/1 passed**.
- Working tree was clean for these validation runs. Parser, layout, host and
  opacity implementation files were unchanged by the diff integration.

The .NET SDK limitation remains. Integrated performance is measured separately
against `fc32945` to isolate this diff from the earlier opacity change.


Integrated benchmark repeated the same fixture v2 against `fc32945`, with the
integrated `ff0fd2c` core and identical optimized flags. Every complete resolution
and prepare+commit median improved in both runs. Rich1000 no-op totals were
81.80/82.14 ms baseline versus 34.58/35.16 ms candidate (-57.7/-57.2%); mixed totals
were 81.27/87.84 versus 41.18/41.19 ms (-49.3/-53.1%). Simple1000 mixed improved
22.4/7.3%. [All phase p50/p95 values](semantic-style-benchmark.md) are preserved,
including initialization and commit separately.

Allocation counts/bytes were identical to the earlier v2 result: rich1000 no-op
514105/53058860 became 178105/19498860. All 720 integrated candidate commits
allocated zero. No commit subphase median increased more than 5% in both
integrated runs, so the earlier nanosecond-scale rich1 observation did not repeat.
The complete-operation budget passes without changing the commit implementation.

The integrated retention diagnostic again measured 1010 frees in unpinned commit
for each core and zero with an outside revision owner. Releasing that owner later
performed 1011 frees. Commit p50/p95 in microseconds was base 319.375/468.235 versus
candidate 44.953/567.744 unpinned, and base 25.188/42.955 versus
candidate 23.009/40.501 pinned. The high unpinned candidate tail is reported rather
than suppressed; this intervention isolates old-revision destruction, not a
specific allocator/cache explanation.
