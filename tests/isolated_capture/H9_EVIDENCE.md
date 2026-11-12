# H9: linear calculated flex gaps

Scope: `calc()` linear px/% lengths for `gap`, `row-gap` and `column-gap` in
bounded geometry capture. This directly covers the gap expression in FLEX-299
without modifying or claiming acceptance of the full original fixture.

## Implementation

- Reuses owned row/column Measures and `layout_used_gap`; no new layout subsystem
  or third-party dependency, and no layout implementation changes were needed.
- Source-context admission checks every literal and function. Only calc math
  with px/% (and dimensionless numeric factors) is enabled; comparison functions
  and relative-unit expressions remain refused even if terms cancel.
- Native gap parsing now decodes escaped function and dimension identifiers into
  the existing length grammar. The 16 KiB bound and token lexer retain trivia and
  boundaries. Unknown units are rejected before serialization, so an escaped
  unit cannot turn into an exponent. The normalized value is published only
  after successful parse; shorthand validates both values before writes.
- `var()` remains outside CLI admission; native runtime substitution is tested.

## Reference and expected values

Consulted original sources, not an executed browser:
- Chromium `flex_layout_algorithm.cc`: RowGap/ColumnGap use the respective
  dimensions of `CalculateChildPercentageSize`, not the viewport.
  https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/layout/flex/flex_layout_algorithm.cc
- Chromium `length_utils.cc`: child percentage inline size comes from child
  available content size; unresolved block size remains indefinite, and gap
  resolution uses zero for its percentage basis.
  https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/layout/length_utils.cc
- CSS Gaps, percentage resolution:
  https://drafts.csswg.org/css-gaps-1/#percentages

The 360 px content-width case gives 17.2 px column gap. An automatic height
retains 10 px row gap, rather than using its parent's numeric height or its own
final auto height. Intrinsic width contribution tests use the absolute term;
final inline placement uses the established content width, following Blink's
separate sizing/layout inputs. Auto-height/min-height cases remain distinct.

## Reproduction

Manual only, no Actions:

```sh
sh tests/isolated_capture/build.sh /tmp/h9-build
sh tests/isolated_capture/run_h9_core.sh /tmp/h9-build
python3 tests/isolated_capture/test_h9.py /tmp/h9-build/aether_capture
```

The implementer rebuilt all 71 core and 7 extra translation units into a fresh
final directory. Results below must still be independently reviewed before
publication.

Coverage includes row/column/reverse, wrap/nowrap, distinct two-value gaps,
content-box vs border-box, padding/border/margins, percent heights with defined
and undefined parents, automatic min-height, intrinsic widths, line boundaries,
inherit/initial/unset/revert, cascade, important, variables, comments, escaped
function/property/unit names, invalid shorthand atomicity, negative literals,
negative calculated values, overflow and allocation failures. Pixel comparisons
are native calculated-vs-explicit controls, not Chromium comparisons.

The original SHA-256 remains
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
It remains rejected without an output PNG. Text, resources and other unsupported
features are not enabled indirectly. All reports retain `chromium=NOT_RUN`,
`authored_css_validation=NOT_RUN` and `flexbox_semantic_acceptance=false`.
No original-page performance improvement or full CSS parity is asserted.

## Implementer results

- H9 native geometry: 68 comparisons PASS.
- H9 source gate: 30 checks PASS; 21 injected allocation failures.
- H9 layout: 43 hard allocation failures handled, 1 valid optional fallback;
  excessive used gap fails closed with no result.
- H9 runtime revision: 367 injected allocation failures, old revision preserved,
  successful commit atomic.
- H9 CLI: 70 real CSS/PNG/error/metric cases PASS; PNG inspected.
- H3–H8 native runners: PASS, including previous OOM and ownership tests.
- H3/H5/H6/H7/H8 CLI: 117/58/44/43/68 cases PASS respectively.

One 300x160 controlled capture recorded style 0.691 ms, layout 0.068 ms, raster
2.820 ms, PNG 6.383 ms, child CPU 0.012 s, peak RSS 3972 KiB and supervised wall
20.739 ms. These are one sample for instrumentation verification, not a benchmark
or a claim about the large original HTML page.
