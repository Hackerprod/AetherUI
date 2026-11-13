# G1 independent test evidence

The independent test oracle is not the raster implementation: it scans stop
intervals linearly and integrates a 64×64 midpoint grid. Raster uses its own
16×16 grid and bounded search. Pixel tolerance is two BGRA8 levels for AA and
layer-rounding differences. These are native tests, not Chromium equivalence.

## Executed focal tests

`tests/isolated_capture/run_g1_focal.sh BUILD_DIRECTORY` builds without the CSS
core, CMake, fonts or Chromium. On 2026-11-13 the candidate passed:

- 27,072 checks: cardinal and oblique directions; asymmetric/nonzero origins;
  separate coverage/ramp rectangles; transparent red → opaque blue in premul
  sRGB; coincident stops; scroll once; rectangular and rounded clipping;
  command opacity and isolated layer opacity; 64 stops; producer mutation after
  preparation; invalid payloads and hidden unsupported geometry; total stop
  budget; every cancellation callback; frame allocation failure.
- Allocation injection exhausted all observed preparation (3) and render (3)
  allocation sites, with confirmed frame and sequence preserved on failure.

`test_g1_gradient.py CAPTURE_BINARY EVIDENCE_DIRECTORY` and the block/flex
fixtures tested actual HTML→PNG plus report CPU/RSS/wall values: PASS, 2,902
pixel checks across block, Flexbox, inherited effective values, transparent
colors, rounded fills, rounded overflow clipping and isolated opacity.
Unsupported interpolation/corners/external stops/hints/repeating and both
orders of supported/unsupported declarations reject. The authored CLI gate
is intentionally strict even if a supported declaration overrides an
unsupported one; native effective-value tests separately permit that override.
Hidden unsupported gradients reject in all three display/visibility/opacity
cases. Tiny repeated background tiles reject rather than exhaust resources.

Observed raster wall time / child CPU / peak RSS (Linux wait4):

- block: 1.00741 ms / 0.003828 s / 2704 KiB
- Flexbox: 1.10622 ms / 0.003623 s / 2916 KiB
- inherited: 0.94838 ms / 0.003499 s / 2844 KiB
- rounded + opacity: 1.26552 ms / 0.00349 s / 2872 KiB
- rounded clip + opacity: 0.552825 ms / 0.00412 s / 2868 KiB

Whole supervisor wall measurements were 10.33–10.65 ms; these include startup
and supervisor polling and are not renderer-only time. No broad performance
claim is made from these small fixtures.

`run_g1_native.sh CORE_BUILD_DIRECTORY` passed native source-list lifetime,
effective inheritance and override checks. It destroys all native list, DOM,
styles and layout before preparation and rasterization of the owning snapshot.
The parent and inherited child use distinct gradient geometry and both retain
correct colors.

## Solid-only regression probe

`g1_solid_benchmark.cpp` compiled with `-O2` against both baseline fedabd8
(tree-equivalent to main 3a9737b) and candidate. It measures 8 solid rectangular
fills at 256×128, 10 warmups followed by 90 samples per round. No gradients.
Median times in nanoseconds (baseline / candidate):

1. 806013 / 746955
2. 863581 / 766800
3. 794357 / 836543

No consistent slowdown appeared; last pair is +5.3% and other pairs are faster.
This noisy shared-host probe is not evidence of a general speedup or a hard
performance bound. Gradients intentionally charge the heavier sampling budget.

Chromium: NOT_RUN. Original large Flexbox fixture is untouched by these tests.

## Implementation scope and validation limits

G1 admits one effective background-image linear gradient with explicit
`in srgb`, cardinal or degree direction, and 2–64 stops. It does not claim
general CSS paint completeness. Implicit interpolation, corners, other color
spaces, hints, repeating gradients and calc/mixed stop positions fail closed.
Native preflight rejects unsupported gradients even when hidden, and rejects
empty or unlaid-out gradient geometry conservatively. Original stop positions
are checked before the legacy clamping resolver. The HTML suite also covers
oblique explicit 100% endpoints: line rounding matches the float resolver,
preventing false rejection without accepting authored external stops.

Per-frame copied stops are capped at 65,536. Tile expansion has bounded axis
counts and cross-product checks before allocation. Raster gradient work is
charged at 256×8 per covered pixel against the existing 50 million budget;
large gradients can therefore reject rather than render. Solid drawing keeps
its prior raster path. Native build adds an effective-domain traversal even
for solid HTML; the raster-only benchmark does not measure that overhead.

Additional existing regression tests passed: software renderer, stroke,
rounded, rounded stroke, curved clip, shadow, border group, opacity, scroll;
linear gradient, background gradient tiling, gradient angle, boundary,
boundary runtime and boundary dispatch. These are focused regressions,
not the entire repository test suite.

Known baseline failure: test_gradient_angle_runtime fails check 622
[sheet/C->C/background-image/literal/0x10deg], expecting zero updates on a
second unchanged resolve. The identical test and check fail against
baseline fedabd8 (tree equivalent to main 3a9737b); no unrelated fix is claimed.

Original flexbox_runtime_validation.html remains unchanged:
SHA256 eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837.
Its capture still rejects at PREFLIGHT_UNSUPPORTED. Neither that original
page nor Chromium equivalence is reported as passing.

References defining scope:
- CSS Images 3 color-stop fixup: https://www.w3.org/TR/css-images-3/#color-stop-fixup
- WPT modern-color implicit interpolation coverage:
  https://github.com/web-platform-tests/wpt/blob/master/css/css-images/gradient/css-color-4-colors-default-to-oklab-gradient.html
  G1 requires explicit sRGB and does not infer an implicit default.
