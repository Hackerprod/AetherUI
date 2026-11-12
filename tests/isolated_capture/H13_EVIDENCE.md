# H13: finite min/max constraints in the initial-empty canvas profile

Based on the source tree published as main `a7fbc6c`. No engine code,
workflows, dependencies or original HTML fixture changes.

## Contract

The authored canvas restriction now permits single finite nonnegative pixel
limits, or unitless zero, in min/max-width/height. Max `none` remains permitted.
The decimal grammar is bounded to CSS numbers; overflow, underflow, hexadecimal,
percentages, relative units, math/functions and unproved escaped units remain
rejected. Existing global CSS/resource/function diagnostics are unchanged.

A canvas with new nontrivial limits must have both **CSS** axes explicitly
resolved in px, not merely HTML width/height attributes. Existing auto-axis
profiles remain available only with their old zero-min/no-max constraints.
There is no automatic aspect-ratio transfer when both CSS axes are defined.

The independent expected content size subtracts decoration for border-box,
clamps maximum, then minimum (minimum wins), flooring content at zero. `none`
and `hidden` borders occupy no space. The same explicit hypothesis is applied
to direct siblings' main sizes, then compared against actual boxes. The existing
sum-of-hypothetical-sizes no-shrink check is retained; this is not admission of
arbitrary flex shrinking or overflow just because one size happens to match.

## Validation

- Clean rebuild: 71 core and 8 auxiliary translation units. The final header
  edit was followed by rebuilding both CLI entrypoints against these new objects.
- 34 core checks, including runtime sheet removal/re-resolution and expected
  dimensions before and after removal; authored gate sweep: 6 OOM failures.
- 38 scenarios / 59 real CLI invocations, with exact native PNG controls,
  timings and process CPU/RSS in report.log. Max/min conflict, border-box,
  zero dimensions, decoration floor, none/hidden borders, different intrinsic
  ratios, parent/sibling limits, cascade and inline controls included.
- Rejections cover auto dimensions with new limits, percentages, math, var,
  intrinsics, escaped units, negatives, overflow/underflow and real shrink/overflow.
- Native H3–H12 and CLI H3/H5/H6/H7/H8/H9/H10/H11 pass; H12 lifetime's
  66,728-check recovery sweep and both negative controls also pass.
- PNG inspected. Pixel controls are native explicit-size equivalents, **not
  an executed Chromium comparison**.

H3's three legacy inline-pixel-limit rejection expectations are now successes;
canvas-auto controls remain rejected. H11's absence/matching tests use a
percentage limit instead of a newly supported pixel limit so they continue to
exercise rejection, including hidden nodes, metadata, conditionals and budgets.
Historical H3/H11 reports remain historical, not retroactively changed.

Run manually (no Actions):

```sh
sh tests/isolated_capture/build.sh /tmp/h13-build
sh tests/isolated_capture/run_h13_core.sh /tmp/h13-build
python3 tests/isolated_capture/test_h13.py /tmp/h13-build/aether_capture /tmp/h13-pixels
```

The original `flexbox_runtime_validation.html` hash is unchanged and remains
rejected. This is neither full CSS support nor a benchmark of the original.
