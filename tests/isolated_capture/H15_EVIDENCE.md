# H15: shared structural traversal for canvas admission

## Scope and cause

The general H5/H7 scanner already understood nested conditional rules and paused
keyframes, while H3 treated the retained stylesheet as flat declarations. A
valid `@media(min-width:1px){canvas{width:130px;height:60px}}` thus failed with
`CANVAS_AUTHORED_CSS_UNKNOWN`. H15 removes that structural mismatch, not the
actual canvas geometry constraints.

`h5::Scanner` is now the one structural traversal used by general and canvas
admission. It returns original byte ranges classified by depth and keyframe
status. H7 still validates keyframe selectors/properties; they are never passed
to the DOM selector matcher. H3 validates the original declarations from these
ranges. No new stylesheet is generated for execution. All original sheets are
still handed to the native cascade in order.

Inactive branches, unused keyframes and ignored `!important` declarations are
inspected. H11 remains unchanged: only ordinary top-level rules with a complete
absence proof are omitted from the H3 admission view. Conditional bodies and
frames cannot acquire this exception.

## Limits and failures

Global traversal shares a budget across stylesheets and inline sources: 2 MiB
CSS bytes, 524,288 lexer tokens including trivia, 65,536 rules/frame blocks,
32 Mi traversal steps, and at most 32 nested rules / 64 component blocks.
Canvas's second scan has its own bounded budget. Pair scans and declaration
source work are charged before descending or examining declarations. Existing
H7/selector/condition-specific limits remain. The input document is also capped
at 2 MiB. Any incomplete analysis is invalid; a caller never consumes spans
unless the full scan succeeded. OOM propagates to capture rejection, without
partial PNG output.

## Verification

Manual commands:

```sh
sh tests/isolated_capture/build.sh /tmp/h15-clean-20261112
sh tests/isolated_capture/run_h15_core.sh /tmp/h15-clean-20261112
python3 tests/isolated_capture/test_h15.py /tmp/h15-clean-20261112/aether_capture /tmp/h15-final-pixels
```

Final result counts are appended after completion. Pixel comparisons are exact
native PNG comparisons against explicit CSS controls, not browser comparisons.
The original HTML remains unchanged and rejected by other unsupported content.

Reference consulted in proposal/review:
https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/css/rule_set.cc
`RuleSet::AddChildRules` distinguishes conditional, ordinary and keyframe rules.
The capture's conservative inspection of inactive branches is its own admission
contract, not a claim that Blink executes inactive rules.

Final clean rebuild completed: **71 core + 8 auxiliary units**, with both CLI
entrypoints newer than the last header edit. Native H15 checks: **23**, including
budget exhaustion (bytes/tokens/rules/steps), aggregate cross-sheet bytes,
original span offsets, rule-kind distinction, malformed structure/depth and
**110 controlled allocation failures** before a successful complete analysis.

H15 integration: **32 scenarios / 50 real CLI invocations**, with exact native
PNG pairs for true/false/mixed conditions, cascade/important, escaped at-rule
names, comments/strings containing braces, paused and repeated keyframes.
Negative cases cover forbidden content in inactive bodies, unused/ignored
keyframe declarations, malformed structure, depth and nested absent selectors.
Representative nested-condition PNG inspected visually.

Native regressions H3–H14 and CLI H3/H5/H6/H7/H8/H9/H10/H11/H13/H14 pass.
H4's explicit fixture pixel assertions, basic input/domain/artifact checks and
inventory tests pass. (An initial invocation of the H4 pixel script incorrectly
passed a binary rather than its required PNG path; the correct fixture capture
and PNG invocation passed.) Original SHA-256 remains
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
The original still rejects, including global unsupported functions/resources,
text and canvas admission uncertainty. Chromium was not executed.

Logs: `/tmp/h15-build.log`, `/tmp/h15-core-final.log`,
`/tmp/h15-cli-final.log`, `/tmp/h15-native-regressions.log`,
`/tmp/h15-cli-regressions.log`. PNG and actual CPU/RSS/wall metrics:
`/tmp/h15-final-pixels/`.
