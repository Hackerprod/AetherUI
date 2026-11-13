# H21: inherited writing-mode, bounded vertical flex

Implementation starts at H20 local candidate 70371c3 (tree equal to published
H20 main). Public ABI headers and original fixture are unchanged. No Actions,
publication, browser launch, external renderer or image rotation was used.

## Axis model

Writing mode is immutable inherited sidecar metadata: 0 horizontal-tb,
1 vertical-rl, 2 vertical-lr. Width, height, margin and decoration stay physical.
Main-axis orientation is separate from row/column semantics. Row uses column-gap;
column uses row-gap, including the flexible-length resolver. Main and cross
reflections move complete margin boxes, not the physical edge values.
Sizing and placement read the same pinned style map. Intrinsic measuring helpers
use physical orientation too; this is not a claim of general vertical intrinsic
layout support. Positioned vertical layout remains outside the capture proof.

The independent expected-coordinate table follows:
- horizontal row: X, RTL XOR reverse; cross Y forward.
- horizontal column: Y, reverse; cross X RTL.
- vertical row: Y, RTL XOR reverse; cross X reverse for vertical-rl only.
- vertical column: X, vertical-rl XOR reverse; cross Y RTL.

References inspected during proposal:
https://www.w3.org/TR/css-flexbox-1/#flex-direction-property
https://chromium.googlesource.com/chromium/src/+/74523ae9526cd03c038d6ed52f47d41ba6b7427f/third_party/blink/renderer/core/layout/flex/flex_layout_algorithm.cc

## Verification

Manual Linux build from new directory: 71 core objects, 9 support objects,
normal and probe CLI. A missing comparator macro terminator failed the first
compile; corrected before completing and testing the build. Existing warning
classes remain; no claim of warning-free compilation.

- H21 layout: 28,802 checks, 1,200 coordinate scenes, 48 additional grow/shrink
  cases, inherited invalidation versus fresh runtime, 96 injected OOM failures.
- H21 style: 1,475 checks, 222 injected OOM failures; cascade, reset, snapshots,
  clones, inherited layout/paint/hittest invalidation and flat export rejection.
- Classifier: 1,710 exact comparisons, zero allocations.
- H21 CLI: 46 checks, including the 24 mode/direction/flex combinations and
  22 fail-closed cases.
- H20 style: 1,247 checks / 222 OOM; adversarial 146; CLI 1,219.
- CLI regressions: H3 134; H5 58; H6 44; H7 43; H8 68; H9 70; H10 44;
  H11 55; H13 38 scenarios/59 runs; H14 64; H15 32 scenarios/50 runs;
  H16 33; H17 21; H18 53; H19 62.

Original `flexbox_runtime_validation.html`: 474,495 bytes, SHA-256
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
Original remains rejected without a success PNG. Chromium is NOT_RUN, and there
is no comparative performance or full-page parity claim. See H21_WRITING_MODE.md
for the narrow capture contract; H20 property/value protections remain active.

All native core regression scripts H3 through H20 completed successfully against
these objects, including H12's negative oracle runs. H20 layout: 4,857 checks,
45 OOM failures, zero optional fallbacks. The H8 gate fixtures that formerly
used vertical-rl as unsupported now test invalid/sideways values; explicit
positive mode cases were added rather than silently deleting the assertion.
