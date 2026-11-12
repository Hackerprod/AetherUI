# H8: owned intrinsic sizing keywords (implementation evidence)

Implementation follows the independently approved H8 amendment. Base source tree
`79442dbf1b903d54117c30a0886275588c5fd918`, equivalent to public main
`a29844e601636683046659821284a03b9f3d9b00`. Local parent `3297f504` is an
exact-tree reconstruction, not a claim of complete remote Git history.

## Contract

- Private tags for `width`, `min-width`, `max-width`, `flex-basis`:
  `min-content`, `max-content`, `fit-content`. Public length/ABI unchanged.
- M/X are **border-box** intrinsic measurements, already containing decoration.
  Fit-content used with definite space is `max(M,min(X,max(0,A-margins)))`.
  Decoration is not added again and no ordered-bounds `std::clamp` is used.
- Min-content always M and max-content always X. Fit-content in min/max
  contribution phases returns M/X. Used fit-content with indefinite available
  width: width/basis remains indefinite; min-width uses M, max-width uses X.
- Raw measurement does not apply the root's own keyword or size constraints;
  descendant contributions still obey theirs. Constraints apply after raw
  measurement, with min winning conflicting max and decoration flooring.
- Intrinsic basis works on row/row-reverse. Column/column-reverse explicitly
  fails before publishing a layout, never changes to auto. Resolved intrinsic
  basis is geometry, not semantic CSS definiteness for descendant percentages.
- Keyword state survives frozen records, inheritance, owned clones, and update
  handles. Literal/initial/unset replacement removes it; mutations invalidate
  layout/paint/hit. Public flat copy returns INVALID_STATE untouched. Internal
  apply_checked has a boolean result rather than an ABI status: it fails with
  CONTEXT-REQUIRED before changing the output.
- CLI authored gate decodes declarations and rejects direction/writing-mode,
  including inactive branches, unsupported vertical keywords and functions
  fit-content()/calc-size(). This is NOT a claim that core can detect authored
  declarations discarded by its parser. CLI var() remains conservatively
  excluded, although native runtime var() keyword expansion is tested.

Reference: Chromium Blink `ResolveInlineLengthInternal`, which distinguishes
min/max-content from fit-content and separates property/available-space phases:
https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/layout/length_utils.cc

## Reproduction (manual Linux; no Actions)

```
sh tests/isolated_capture/build.sh /tmp/h8-build
sh tests/isolated_capture/run_h8_core.sh /tmp/h8-build
python3 tests/isolated_capture/test_h8.py /tmp/h8-build/aether_capture
```

All 71 core translation units rebuilt. No CMake/Windows/Chromium execution.

- Sidecar: 43 parser/runtime/ownership checks; OOM preparation 349, freeze 9,
  clone 1. Retained old generations, update ownership after runtime+DOM teardown,
  var(), shorthand, escapes, trailing-junk rejection and unchanged flat sentinel.
- Layout: 57 assertions of phases, differing M=60/X=100 without text, available
  widths below/between/above, margin and decoration/box-sizing, constraints,
  row/reverse basis and column failure, keyword→length→keyword and old layout.
  88 hard allocation failures publish no layout. Two optional stable_sort
  buffer allocation failures use its valid in-place fallback with exact geometry;
  the test continues injection until a run performs no failed allocation.
- CLI: 52 cases including byte-identical CSS→native PNG versus explicit-size
  equivalents, negative/positive margins, min/max constraints, basis, errors,
  CPU/RSS/wall metrics. No reference Chromium output is fabricated.
- Gate: 73 checks +52 injected allocation failures.
- Regression CLI: H3 117, H5 58, H6 44, H7 43.
- Gate regressions: H4 24, H5 13+63 OOM, H6 11+79 OOM, H7 22+136 OOM.
- H4 native core/applicators/ownership: PASS; OOM prepare383, parse14, freeze14, clone1.
- Diff catalog94; classifier1707 zero-allocation comparisons; semantic diff
  both APIs and OOM468/452 PASS. Test catalog extended for intrinsic tags; the
  prior stale `order`-as-no-op test now checks H7 real impact7.
- 176 C symbols identical to H7; no public header/struct extension.

The original `flexbox_runtime_validation.html` remains byte-identical SHA256
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`
and still fails with TEXT_RENDERER_UNAVAILABLE, without a PNG. No claim of full
CSS coverage, full 403-test acceptance, native text/SVG completion or browser
visual equivalence. This evidence is implementer testing, not the independent
code validation gate.

## Correction after independent review: border/flex shorthand isolation

The reviewer correctly rejected the first candidate: an intrinsic-basis expansion
was accidentally duplicated in `border`. Removed that block; intrinsic keywords
remain accepted by `flex` only. Added four direct shorthand negative/positive
pairs and six compiler/runtime regressions that retain `flex:2 3 17px` despite
invalid intrinsic `border` values (including case, CSS escape and `var()` input).
The direct regression failed against the original candidate objects before the
corrected source was rebuilt. Post-correction native H8 tests pass: 53
parser/runtime checks, layout57/OOM88, gate73/OOM52, runtime OOM349,
freeze9/clone1. Recompiled the affected translation unit and relinked the CLI
against the existing H8 build; this correction pass is not a clean full rebuild.
Independent revalidation is still required before publication.
CLI regression rerun after relinking: H3 117, H5 58, H6 44, H7 43, H8 52
cases PASS. Original fixture remains rejected unchanged; no Chromium claim.

## Correction after independent review: positioned intrinsic sizing

The next independent review found `position:absolute;left:0;right:0` still
consumed the private intrinsic width marker as a public length and confused it
with `auto`. The positioned path now checks owned keyword metadata before length
resolution and only forces inset stretching for genuinely automatic widths.
`fit-content` receives a separate inset/static-position available span; padding,
margins and other percentages retain the original containing-block width. The
normal layout entry remains the sole owner of min/max constraints and sizing.
Descendant intrinsic min/max markers are likewise excluded from flat resolution
in raw contribution measurement.

Architectural reference for separating inset-modified space from percentage
resolution: Chromium Blink `absolute_utils.cc`:
https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/layout/absolute_utils.cc
This is a source reference, not an executed browser comparison.

Manual clean reconstruction used a new output directory:

```
sh tests/isolated_capture/build.sh /tmp/h8-final-clean-20261112
sh tests/isolated_capture/run_h8_core.sh /tmp/h8-final-clean-20261112
python3 tests/isolated_capture/test_h8.py /tmp/h8-final-clean-20261112/aether_capture
```

All 71 core and 7 extra translation units were rebuilt from the corrected source.
H8 now passes 53 parser/runtime checks, 84 layout checks (27 additional cases),
73 gate checks, and 68 CLI/CSS/PNG/error/metric cases (16 additional cases).
Layout allocation injection passes at 139 hard failure points with 3 valid
optional-allocation fallbacks; runtime349, freeze9, clone1 and gate52 also pass.
New cases cover absolute/fixed placement, both/one/no specified horizontal
insets, static flex anchors, positive/negative/automatic margins, percentage
padding and min/max constraints. Pixel tests compare against explicit-size
native fixtures, not Chromium. H3/H5/H6/H7 CLI regressions remain 117/58/44/43
PASS. Original fixture remains rejected and byte-identical. This is implementer
evidence; independent revalidation remains mandatory before publication.
Native H3–H7 regression runners also pass after the clean rebuild, including
contextual sizing, frozen ownership, gates, selector/media/RGBA, paused animation
and order/pixel/hit testing. The independent review's original positioned repro
now returns OK and widths 60/100/100 for min/max/fit-content, versus controls
100px=100 and auto=200. No external CI or Actions were started.
