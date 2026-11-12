# H5 contextual admission (manual Linux evidence)

Implementation awaiting independent review. Base source tree is published main
`e8dae8dc6b21a3f93561aac076e99ce1edf8f252` (tree
`beae6086751ab4cd911027d652eb3000b51a78aa`), cloned locally from the identical H4
tree at `cd76cf03458ab3996a8994b1a76743e96a7420b3`. No Actions, push or PR.

## Scope

- The same core selector parser provides an opt-in StrictNonEmpty wrapper with
  complete consumption, strict nested functional lists, depth32, source64KiB,
  and transactional output. Runtime forgiving behavior is unchanged. Only
  nth-child functions are admitted by this harness; valid other functions are
  still rejected. Integer overflow coefficients are rejected, not truncated.
- The same media scanner/parser records recovery/unknown/profile evidence before
  any AST constant folding. Only all/screen/print, one width/height/min/max
  feature with finite nonnegative px, or type AND that feature are admitted.
  Queries cap64KiB; nested stylesheet media cap32. No lists/not/only/or/ranges.
- Legacy comma rgba with homogeneous RGB units and finite alpha uses the same
  core parser through an internal finite-only mode. The wrapper is transactional;
  the public try_parse transparent-on-error contract has not changed. Entire
  color declarations or exterior box-shadow lists are validated, not fragments.
  Shadow lengths are px/zero, maximum32 items, no inset/currentColor/other
  functions. Native preparation retains its geometry/blur/radius limits.
- A single bounded contextual token/block analysis feeds both admission and
  inventory. It descends into inactive media; unsupported functions and resources
  there remain blockers. H4 math analysis runs on individual declaration blocks,
  so padding/flex-basis AST admission works inside media without flattening CSS.
- Source bytes sent to style compilation are unchanged. Core media compilation
  preserves cascade order, specificity and important. No public API/ABI changed.
- This is still a geometry-only profile, not general CSS validation. In particular
  canvas H3's additional authored guard may conservatively reject nested media;
  H5 does not weaken that independent gate. Escaped nth function names are tested;
  escaped coefficients unsupported by the existing nth parser remain rejected.

## Verified

Full71-TU core compile and capture link. Test executables use freshly compiled
changed core objects. No external graphical stack or browser was used.

- H5 core48; selector OOM29 / media OOM12 allocations.
- H5 rgba38; OOM6 with destination unchanged, public error contract checked.
- H5 shared gate13; OOM63 allocations, complete publication only on success.
- H5 real CLI59 cases: PNG pixels, whitespace/comment element nth indexes,
  2n/3n/4n/negative/zero/overflow/escapes, media widths699/700/701/1280 and height
  boundaries, all/screen/print, nested media, cascade/important, alpha0/.07/.08/1,
  percent alpha, nonzero blurred exterior shadow, H4 math inside media. Fail-closed
  malformed/truncated/unknown/overflow/media33/inactive-resource cases create no
  PNG and never start styling. Every run retains real CPU/RSS/wall metrics.
- H1 input/preflight/domain/artifact/original checks; H2 complete inventory.
- H3 real CLI117, including pixel checks and unchanged original rejection.
- H4 core, state/ABI, padding/flex used values, ownership OOM215 + parse14/freeze14/
  clone1, checked/void applicators, gate24, real pixels padding50/28 and flex150.
- C ABI smoke and contextual flat offsets/handle lifetimes passed.
- Core subtask regressions: media parser10580 + typed10583 + percentage16;
  selector escapes14778 and comments286 passed.
- `git diff --check` passed. No sanitizer or Chromium comparison claimed.

Combined fixture `fixtures/h5_context.html` exported a genuine PNG, inspected
visually: alternating nth tile colors, rgba over white, media-controlled padding,
soft shadow. Example local run:15.773ms child CPU,20.4341ms process wall,
5476KiB peak RSS. These are one local run, not a Chrome speed comparison.

Original fixture SHA256 stays:
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
It still rejects for unsupported text/resources/functions/at-rules. No original
PNG, no403-case pass, no equivalence claim. Reports retain CSS Unknown,
authored_css_validation=NOT_RUN, chromium=NOT_RUN.

## Reproduce

```
sh tests/isolated_capture/build.sh /tmp/h5-build
sh tests/isolated_capture/run_h5_core.sh /tmp/h5-build
python3 tests/isolated_capture/test_h5.py /tmp/h5-build/aether_capture
python3 tests/isolated_capture/test_h3.py /tmp/h5-build/aether_capture
python3 tests/isolated_capture/test_inventory.py /tmp/h5-build/aether_capture
sh tests/isolated_capture/run_h4_core.sh /tmp/h5-build
/tmp/h5-build/aether_capture --geometry tests/isolated_capture/fixtures/h5_context.html /tmp/h5-combined 1280 80 1
```

Use new output directories; the CLI refuses to overwrite prior artifacts.

Chromium source actually consulted in the proposal (media evaluator, including
Unknown/Kleene value handling):
https://chromium.googlesource.com/chromium/src/+/e3e593ee7a9700c6e572ad0d329220532fe20194/third_party/blink/renderer/core/css/media_query_evaluator.cc
Nth/color changes reuse Aether core parsers; this evidence does not claim a
Chromium nth/color implementation audit.
