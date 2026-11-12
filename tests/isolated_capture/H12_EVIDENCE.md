# H12: complete flex lifetime allocation sweep

## Scope and cause

Test-only change based on the source tree published as main `dc06e0e`. No engine,
CSS profile, original HTML fixture, CI or workflow is modified.

The previous lifetime test required `!injected` on every successful layout and
stopped immediately. A successful `std::stable_sort` without its optional
nothrow buffer was therefore treated as a defect. H10's independent review had
reproduced that failure in H9 as well. Historical H10/H11 evidence remains an
accurate record of the test as it existed then; this change resolves that test
contract rather than retroactively calling those runs green.

## New contract

- Mandatory allocation failure: require `AETHER_ERROR_OUT_OF_MEMORY`, previous
  output pointer and its complete saved state intact.
- Success after an injected failure: validate the new result's ownership,
  reachability, primary map, paint membership, child lists, geometry and static
  anchors. Destroy it, prove engine reuse with a fresh successful layout and
  continue the allocation sweep.
- Success without reaching injection: validate and destroy output, then finish.
- An explicit 20,000-position ceiling fails if no terminal success occurs.
- Attempts must equal OOM results + recovered successes + one terminal success;
  the terminal position must be after any recovery. No ordinal or fallback
  count is fixed as a portable expectation.

All original saved-box, assets, map, epoch and prior-result checks remain.
Recovery does not excuse partial output, an arbitrary status, or a changed
previous result.

## Added oracle coverage

A second fixture retains the original positioned/static-anchor geometry but
adds four in-flow flex items. Their orders are 2, 1, 2, -1. Explicit expected x
coordinates (155, 145, 165, 135) check both sorting and stable DOM order of the
tied items. The fixed parent and nested positioned child retain their literal
expected geometry. No browser execution or self-comparison generates these
expected coordinates.

The runner executes two negative controls on a previously valid output:
swapping the equal-order boxes' x coordinates and shifting the fixed box by one
pixel. Both must terminate with the lifetime test assertion signature, not just
any nonzero exit. Core dumps are disabled. The same validation routine is used
for successful recovered layouts.

## Measured results

Fresh manual build: 71 core translation units + 8 auxiliary units.

| Scenario | Attempts | Required OOM | Valid recoveries | Terminal position |
| --- | ---: | ---: | ---: | ---: |
| Original prepared lifetime | 74 | 72 | 1 | 73 |
| Mixed orders and stable ties | 97 | 95 | 1 | 96 |

66,728 cumulative assertions; both negative controls PASS. These counts are
observations on this compiler/library, not future fixed-count assertions.
Every recovered output and every subsequent injection position is examined.

Run from repository root:

```sh
sh tests/isolated_capture/build.sh /tmp/h12-build
sh tests/isolated_capture/run_h12_core.sh /tmp/h12-build
```

This is allocation-failure/ownership/state testing, **not a leak detector or a
performance benchmark**. It does not make the original flexbox page render,
certify complete CSS coverage, or prove parity with Chromium. Chromium remains
unexecuted. Independent review is required before publication.

## Regression results on the fresh build

- Native scripts H3 through H11: PASS, preserving their existing OOM sweeps.
- CLI suites: H3 134, H5 58, H6 44, H7 43, H8 68, H9 70, H10 44,
  H11 55 cases PASS (including their generated-image/metric checks).
- Baseline input/preflight/domain/artifact/original-fixture checks: PASS.
- Original SHA-256 remains
  `eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.

The formerly failing flex lifetime test is now included explicitly via the H12
runner. These named suites passed; this is not a claim that every repository
configuration, platform or test was executed.
