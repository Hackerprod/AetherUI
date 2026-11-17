# Native linear gradients at useful sizes

This change optimizes the existing software rasterizer, not the HTML eligibility
profile, public renderer ABI, Cairo, typography, DPI, parser, or CSS layout.
HTML capture remains experimental and requires the existing explicit `in srgb`
profile. A captured card/background is not evidence that arbitrary HTML works.

## Fast-path proof

For a pixel intersected with viewport/rectangular clip/coverage, call the resulting
rectangle **b**. Curved shape and active curved clip must each contain all of b:
we only admit their central horizontal or vertical bands, never partial corners.
The two extreme corners selected by direction signs bound the affine projection.
An outward error guard and `nextafter` enlarge that range before strict stop
comparisons. Equality, duplicate stops, close stops, non-finite range and
uncertainty fall back to the unchanged 16×16 sampling loop. The guard excludes
extra pixels; it never permits crossing a discontinuity.

Within one open stop interval (or a constant exterior tail), premultiplied BGRA
is affine. Its rectangular average equals its value at b's center. The fast path
performs one gradient evaluation, scales by the clipped rectangle area and uses
the old quantization/source-over. The ramp rectangle is independent of b.
The 256 multiplication merely reuses the existing blend scale and is exact in
binary floating point. Numerical operation ordering may differ by one byte in
affine differential tests; historical grid64 oracle tolerances are unchanged.

## Bounded budget

The limit remains 50,000,000 units shared by the frame. These are bounded
algorithmic work units, not nanoseconds or CPU instruction counts. The historical
model already counted one sample/interpolation/blend block plus at most seven
binary-search comparisons as eight units.

A classification reserves 16 units: two coverage tests, four projection blocks,
two numerical-bound blocks, at most seven stop comparisons, and one interval
test. The first 16 are charged **before** preflight executes classification.
Then the repeated paint classification (16) plus selected paint work is charged:
seven lookup comparisons, one interpolation and one blend for fast pixels (9),
or all 256×8 historical work for slow pixels. Totals: **41 fast, 2080 slow**, plus **4 per nonempty gradient draw** for
once-per-draw ramp geometry and
transparent clear and all other draw/layer charges. Both searches are bounded by
the existing 64-stop contract. No preflight pixel map is allocated. The charged
prefix stops immediately on exhaustion, and cancellation is checked each row.
Candidate allocation still follows the entire frame preflight. Multiple draws,
including hidden-by-overpaint draws, accumulate cost; budget is never reset.

Length and ramp centers are cached once per nonempty gradient draw in
composition, using the exact original double expressions and division. The
preflight reserves the extra four algorithmic units before candidate allocation;
empty draws reserve none. Classifier arithmetic is unchanged.

Large cardinal gradients retain two slow columns because exterior stop equality
is deliberately excluded. 1280×800 cardinal costs 46,270,404; oblique costs
43,012,082. Larger/complex/overpaint scenes can still exceed this budget.

## Reproduce

From a clean checkout with this patch, run manually:

```sh
sh tests/isolated_capture/run_gradient_useful_all.sh /absolute/new/output
sh tests/isolated_capture/run_g2_regressions.sh /absolute/new/regressions
sh tests/isolated_capture/run_g2_focal.sh /absolute/new/translations
```

The first recipe builds the core/capturer from source, tests original admitted
HTML and rejection cases, and decodes/verifies full new PNGs. Private tests include
the implementation only to inspect classification; the reference scans stops
independently and historical-grid differential never calls production sampling.
No instrumentation is added to the public ABI. Existing `Result` preparation and
composition timers can be used with identical benchmark drivers on the base and
candidate. Timing results belong to their measured machine/load, not a speedup
promise. New large cases rejected by the base are coverage gains, not speedups.

## Historical expectations and limits

The final old G1 assertion expected a simple 1024×1024 gradient to exceed budget.
That specific expectation is replaced (with review approval) by large success
with pixels and an accumulated-overpaint rejection. Running the unchanged old
suite on this candidate still fails that exact old assertion; do not relabel it
PASS. All other old assertions remain unchanged. Invalidations/classifier
historical evidence, including its known 20-versus-19 compile failure, is not
changed or re-certified by this cycle. Chromium, Cairo, production typography,
DPI other than 1 and general CSS are not certified. No Actions are enabled.

## Current acceptance status

The original fast-path candidate passed functional checks and native HTML
captures but failed performance acceptance: its entirely slow 16×16/64-stop
case showed unexplained composition overhead. That evidence remains valid for
that frozen candidate and is not rewritten here.

The reviewed correction caches invariant ramp length/centers once per draw,
reserving four extra work units. Final acceptance requires fresh functional,
capture and paired-performance validation, including same-binary runtime
controls to explain residual classification overhead. Diagnostic bypasses and
counters must remain outside the product. No arbitrary percentage threshold,
reduced sampling, forced inlining or publication is authorized by this change.

The corrected candidate completed all 11 functional/capture stages. Paired
measurements show slow composition +1.43–1.75% versus base; the same-binary
classification control measures roughly 4.1–6.7 microseconds, explaining only
part of that difference. P4 remains not fully cleared; a small residual is not
accepted merely because it is small. Full evidence is retained for D review.
