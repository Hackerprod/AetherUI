# H10: bounded identity translateZ and positioned ownership

Candidate after H9 (published main 93f4eb0). Original fixture remains unchanged.

## Implementation

- Accept one lexical zero number or `px` length in `translateZ`, including CSS
  identifier escapes. Reject percent, nonzero significands even if conversion
  underflows, contextual expressions and untested units. Preserve a nonempty
  translate identity operation: identity is not `none`.
- CLI admits only a single tested identity function; mixed lists and other 3D
  transforms remain blocked. `perspective` and `preserve-3d` are not enabled.
- CLI prepares the existing effective layout context. Transformed containing
  blocks apply to fixed and absolute descendants. Finalization uses completed
  ancestor padding boxes, including auto-height ancestors; a nearer positioned
  owner wins for absolute descendants.
- Static transformed children enter stack level zero in the shared paint/hit
  order. This is local stacking, not a claim to full stacking-tree support.
- Native rasterization accepts scrolling with a transformed owner at/inside the
  innermost scroll scope. Escape from an intermediate clip is rejected, not
  drawn incorrectly. Document/root scrolling remains unsupported in the native
  snapshot profile. Transformed static flex items with non-auto z-index remain
  rejected by CLI; block and flex boxes only, no body/html/canvas transforms.
- Gate reads frozen style views instead of exporting contextual styles through
  the compatibility flat ABI (which would reject supported math properties).

## References consulted, not copied

- Chromium `LayoutObject::ComputeIsFixedContainer` and `ComputeIsAbsoluteContainer`:
  https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/layout/layout_object.cc
- CSS Transforms rendering/containing block model:
  https://drafts.csswg.org/css-transforms-1/#transform-rendering

## Verification

Manual Linux build: `sh tests/isolated_capture/build.sh NEW_DIRECTORY`.
Run `sh tests/isolated_capture/run_h10_core.sh BUILD_DIRECTORY` and
`python3 tests/isolated_capture/test_h10.py BUILD_DIRECTORY/aether_capture NEW_RESULTS`.

Tests cover fixed/absolute ownership, direct/remote/nearest ancestor, padding
box percentages, viewport sizes, margins, cascade/variables/escapes, dynamic
removal/readdition, stack level zero and opacity, real PNG pixels and metrics,
local scroll offsets in native preparation/rasterization, explicit rejection
of unsupported clips/root scrolling, and atomic allocation failures.

The original `flexbox_runtime_validation.html` retains SHA-256
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
It still fails the conservative gate due to other unsupported content.
Chromium was **not executed**. Comparisons use explicit geometry/pixel controls,
not browser-reference images. No original-page speed claim or full CSS fidelity.

Final local results (assertions enabled, fresh build directory, all current sources):
- Clean build: 71 core units + 7 extra units, no reused objects.
- H10 core: 121 checks; gate: 20 checks; CLI: 44 PNG/metric/rejection cases.
- Allocation injection: runtime revision atomicity 370 failures; layout/context
  37 failures and 0 fallback allocations; gate 2 failures.
- Native H3–H9 regressions PASS, including earlier allocation-failure suites.
- CLI H3=117, H5=58, H6=44, H7=43, H8=68, H9=70 cases PASS.
- Generated final PNGs visually inspected and exact pixel assertions passed.
- `git diff --check` clean. Independent review is still required before publish.

## Independent-review anchor correction

Review found a block preceding an out-of-flow block with auto insets moved from
(0,40) to the preceding block's inline end (100,0) during transformed-owner
re-finalization. The earlier candidate must not be published.

Flow now captures block insertion markers for fixed as well as absolute boxes.
Before replacing a block child during finalization, its completed margin-anchor
geometry is retained on the stack and passed to the positioned sizing pass.
Explicit inset axes continue to win; flex alignment is recalculated rather than
frozen. Logical marker records are still cleared and never published in snapshots.
The new controls cover both transform:none and identity, both positioning modes,
block/flex children, direct/remote ancestors, relative and inline predecessors,
partially explicit axes and following-flow invariance. Four actual CLI PNG tests
cover the review's failing case and its non-transform controls.

Reference for retained out-of-flow static position and container adjustment:
https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/layout/out_of_flow_layout_part.cc

Additional positioning contracts after the anchor correction:
- Block static positions: 105 scenarios, 306 rectangles, 3,233 checks PASS.
  The former fixed-boundary legacy inline-end expectation was corrected to the
  block insertion position, with matching independent-repro and CLI controls.
- Block static lifetime: 9 scenarios, 31,873 checks PASS, legacy/prepared
  allocation-failure sweeps 49/57 and prepare/clone sweeps 2/30.
- Flex static position: 314 scenarios, 10,787 assertions PASS; contextual flex:
  4 scenarios, 203 assertions PASS.
- Positioned auto margins: G01–G15 and S1–S3, 52 candidate snapshots PASS.
- Existing `test_flex_static_position_lifetime.cpp` is NOT PASS: it asserts
  that successful layout cannot follow any injected allocation failure. The
  same assertion fails with the pre-correction H10 candidate objects. A
  temporary diagnostic-only copy accepted the optional allocation fallback at
  ordinal 27 and validated output/prior-state geometry through the complete
  sweep (23,641 assertions; 72 injected layout failures). The repository test
  was not weakened. This pre-existing strict-oracle issue remains disclosed;
  the diagnostic is supplemental evidence, not a substitute PASS.
