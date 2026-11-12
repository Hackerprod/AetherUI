# Isolated geometry CLI: H1/H2 with H3 initial-empty canvas

Linux-only, C++20, manual build/run. No Actions, windows, .NET, Chromium or
system font fallback. This is not a general HTML/CSS conformance validator.

From repository root:

```
sh tests/isolated_capture/build.sh /tmp/h1-build
sh tests/isolated_capture/test.sh /tmp/h1-build/aether_capture /tmp/h1-results
/tmp/h1-build/aether_capture --geometry input.html NEW_OUTPUT_DIR 1280 800 1
```

The supervisor creates one worker process, which reads bytes into private
storage and creates its own document/runtime/layout. Handles never escape. No
external callbacks, loader, resolver or font lookup is installed. This does not
promise an OS security sandbox. Input paths are trusted CLI arguments; the final
component must be a regular nonsymlink file. Output must not already exist.

## Honest domains

Every report states `css_eligibility=Unknown`,
`css_reason=PARSER_COVERAGE_UNPROVEN`, `authored_css_validation=NOT_RUN`, and
`flexbox_semantic_acceptance=false`. The engine parser's OK is **not** proof of
coverage; original source spans/recovery evidence are not implemented here.
`--css` rejects instead of downgrading to geometry. `--geometry` explicitly
requests only Aether's resolved emitted commands. Unknown CSS properties may be
discarded by the existing engine; a generated image is not CSS fidelity proof.

## Actual limits versus missing limits

During the complete worker lifetime Linux enforces RLIMIT_AS=1 GiB,
RLIMIT_CPU=5 seconds and RLIMIT_STACK=8 MiB. The supervisor kills the worker
after 10 seconds wall time. Core dumps are disabled. These are process-wide
backstops, **not** per-node/per-operation parser or layout instrumentation.
The report explicitly marks internal parse/layout work limits NOT_IMPLEMENTED.
Input is bounded to 2 MiB while reading; NUL is rejected. Viewports are bounded
to 2048x2048, DPR=1. DOM preflight rejects over 50,000 nodes/depth 128 **after**
parsing; it is not claimed to protect the parser. CSS token preflight uses the
engine lexer with a work budget. Preparation/raster retain their existing limits.

For this first cut all CSS at-rules and function tokens are rejected,
including otherwise harmless media/calc/color syntax, conservatively. All
link/image/SVG/media/embed elements and executable scripts are rejected.
H3 exceptions for initial-empty canvas and hidden inert JSON are described below. No resources are
silently downloaded. Non-whitespace body text is rejected before layout;
metadata text is exempt from that preflight, but any text measurement request
invalidates the operation before image publication. The internal sentinel
callback cannot provide a supported text result; no output is accepted from a
layout that requested it. Pseudo text and unsupported draw commands can also
reject later. UA is the real engine builtin sheet; its hash is recorded.

Style blocks are traversed in DOM order and compiled separately after UA;
compiled-byte hashes are recorded. This is not a complete source provenance
certificate. HTML/parser omissions remain Unknown.

## Artifacts and metrics

`report.log` reports started stages and measured elapsed wall milliseconds,
worker exit/signal, child CPU seconds and peak RSS KiB from Linux wait4.
Unstarted stages have no duration (not zero); supervisor wall includes startup
and waiting. No comparison timing or Chromium capture is claimed. CPU/RSS apply
to the whole worker, not individual stages. A rejected/terminated worker does
not publish `aether.png`; a successful image covers emitted Aether commands only.
The original flexbox fixture is tested without edits and is expected to reject
for real missing text/resources/CSS support. This is useful diagnostic output,
not a completed flexbox render.

The manual build also emits `aether_capture_limit_probes`, a test-only variant
with compile-time probe branches. Never use it for captures. Verify the actual
same supervisor limits (allocation denial, CPU kill, wall timeout) with:

```
sh tests/isolated_capture/test_limits.sh /tmp/h1-build/aether_capture_limit_probes /tmp/h1-limit-results
```

These probes do not prove per-operation parser/layout budgets; none are claimed.

OS CPU accounting/enforcement has granularity: a configured five-second CPU
limit is not an exact five-second deadline. See measured probe evidence. This
first increment does not claim full H1 parser provenance or fine-grained work
budgets. It is a diagnostic/geometry CLI with explicit failure reports.

## H2: diagnostic inventory only

The report now enumerates lexer-observed functions, at-keywords and URLs in every
DOM-extracted stylesheet and inline style, plus resource/script elements. It
retains all preflight vetoes and adds later observed reasons instead of hiding
these behind the first function. No whitelist or new rendering support exists.
Names use the engine's CSS lexer/identifier decoder, not a regex over the HTML.
Comments and string contents are not inventoried as functions or at-rules.

Each CSS source has a byte length and SHA256. Offsets (zero-based) and line/column
(one-based, byte columns) refer ONLY to that extracted CSS string, NOT the HTML
file: entities and parser recovery may change correspondence. Element references
are traversal ordinals and selected escaped attributes; HTML source locations are
unavailable. Resource src values are hashed rather than printed. Semantic context,
property validity, parser coverage, and engine support remain unverified. Counts
are case-preserving lexical observations, not proof of unsupported engine features
or of faithful CSS interpretation. At-keywords are observations, not validated
at-rule syntax. A lexical/work-limit failure explicitly marks incomplete inventory.

Run `python3 tests/isolated_capture/test_inventory.py /path/to/aether_capture`
for the manual fixture and synthetic diagnostic regression. Python is test tooling,
not a runtime dependency of the C++ capture executable. The original fixture is
unchanged and still rejected without an image before style/layout/raster.


## H3: initial-empty canvas, not a canvas drawing API

The isolated CLI explicitly activates the private per-engine initial canvas mode.
Public bindings remain unchanged and mode defaults off. A canvas retains its DOM
fallback but does not lay out, measure or paint it in this mode. Its initial
bitmap is transparent: no content command and no bitmap storage. Ordinary box
background/border/clip/grouped opacity are still painted.

This is **scripting-enabled initial canvas semantics without executing scripts**,
not Chromium's globally scripting-disabled fallback mode. Reports say
`canvas_mode=ENABLED_INITIAL_EMPTY`, `javascript_execution=NONE`, and
`chromium=NOT_RUN`. No browser comparison has been executed.

The admitted profile is direct children of a definite-width row flex container,
no wrap, effective start/center alignment, zero growth, auto basis and proven
absence of main-axis shrink. Sizes are auto/px; CSS box-sizing and intrinsic
ratio are used. Zero intrinsic dimensions are valid; a single auto CSS axis with
zero intrinsic ratio rejects. Content never becomes negative beneath decoration.
Other profiles reject before PNG. Every canvas and ancestor must remain visible
in display-tree terms (not display:none/contents); transforms are excluded.

Attribute dimensions follow non-negative HTML integer parsing, defaults 300x150,
and Chromium's signed-int upper-bound fallback. Product limits reject valid
intrinsics above 8192 or more than 256 canvas, without allocating bitmaps.
The registry payload metric is entry count times entry size, explicitly excluding
allocator/bucket overhead. RSS and CPU/wall remain measured by wait4.

A conservative declaration lexer decodes escaped property names across inline
styles and sheets and rejects direction/writing-mode/aspect-ratio/logical sizing,
explicit unsupported minima/maxima and unclassifiable nested syntax. It is not a
coverage certificate. Absent minima are accepted: current flat style defaults
can retain AUTO despite the builtin property registration's `0`. Explicit
`min-width:auto`/`min-height:auto` is rejected by the authored gate. Only absent
or authored zero minima, and absent/none maxima, belong to this no-shrink profile.
All existing function/at-rule/resource/text barriers remain.

Only `application/json` (ASCII case-insensitive) scripts without src are inert
candidates. Raw content is preserved. Post-style admission requires computed
`display:none` on script or an ancestor; visible data rejects rather than being
forcibly hidden. Executable scripts/handlers and resources inside canvas fallback
still reject. JSON content is data and is never executed or interpreted.

Manual additional tests after build:

```
sh tests/isolated_capture/run_h3_core.sh /tmp/h1-build
python3 tests/isolated_capture/test_h3.py /tmp/h1-build/aether_capture /tmp/h3-results
python3 tests/isolated_capture/test_inventory.py /tmp/h1-build/aether_capture
```

The original Flexbox HTML remains unchanged and rejected by independent CSS,
SVG/image and text blockers. H3 does not make the original page renderable.
See H3_EVIDENCE.md for exact executed checks and outstanding independent review.

## H5 contextual profile

See [H5_EVIDENCE.md](H5_EVIDENCE.md) for the bounded nth-child, legacy rgba and
screen/media admission profile, tests and limitations. The analysis is shared
with inventory and checks inactive branches too. This does not certify the
original Flexbox page or change CSS eligibility from Unknown.

## H6: compile-time `@supports` with shared evidence

`@supports` condition truth comes from a complete, side-effect-free single
property declaration probe in the normal core parser. Captured geometry has a
narrower certified condition profile: implemented `display` literals and
`initial`/`inherit`/`unset`, homogeneous `and`/`or`, `not`, and grouping.
`general-enclosed`, unknown properties/values, `var()`, `revert`, `revert-layer`
and the `line-break` extension remain outside that profile even if negation or
another true operand would select the rule. Coverage evidence is accumulated
before boolean folding. Inactive rule bodies are still inventoried/audited.
Both directions of media/supports nesting share the maximum depth of 32.
Supports parsing shares a 65,536-token budget across each stylesheet; no parse
attempt resets it. This is not a claim of complete parser/layout work budgets.

Manual regression commands (after the build above):

```
sh tests/isolated_capture/run_h6_core.sh /tmp/h6-build
python3 tests/isolated_capture/test_h6.py /tmp/h6-build/aether_capture /tmp/h6-results
```

The original flexbox fixture remains unchanged and blocked; no Chromium render,
complete CSS compatibility or full flexbox acceptance is asserted.

## H7 paused animation profile

`@keyframes` and one initially paused linear animation can be sampled at local
 time zero before layout, for flex-grow, px flex-basis/gaps, integer order and
 discrete flex-direction. Both effective endpoints are required. The CLI
 rejects unsupported authored or effective animation states, even when a
 conditional branch is inactive. This is not a live timeline or full CSS claim.
 `order` is now a frozen private sidecar consumed by flex layout/paint/hit-test;
 legacy flat export refuses a nonzero order instead of losing it.

Run after the manual build:

```sh
sh tests/isolated_capture/run_h7_core.sh /tmp/h7-build
python3 tests/isolated_capture/test_h7.py /tmp/h7-build/aether_capture
```

See `H7_EVIDENCE.md` for exact boundaries, OOM tests, ownership and exclusions.

## H9 linear calculated flex gaps

The geometry CLI admits `calc()` linear px/% values in `gap`, `row-gap` and
`column-gap`. The existing core resolves the two axes independently against the
container's content box; cyclic/indefinite percentage bases contribute zero,
while the absolute term survives. Escaped calc/unit identifiers use a bounded
private normalization step before the existing length parser. This does not
add generic CSS math: comparison functions and relative units within calculated
gaps remain outside the CLI profile. `var()` is tested in native runtime but is
still refused by the CLI's source gate. No external browser was executed.

```sh
sh tests/isolated_capture/run_h9_core.sh /tmp/h9-build
python3 tests/isolated_capture/test_h9.py /tmp/h9-build/aether_capture
```

The original flexbox page is unchanged and remains blocked. See
`H9_EVIDENCE.md` for measured test coverage and limitations.

### H10 identity transform profile

`transform:translateZ(0)` (literal number/px zero, including identifier escapes)
now preserves the containing-block and stacking effects of a non-`none`
transform. This is not general 3D support. Only tested block/flex boxes and a
single identity function are admitted. Intermediate clip escape, root scrolling,
static transformed flex-item z-index and other untested forms reject the frame.
See `H10_EVIDENCE.md`, `run_h10_core.sh` and `test_h10.py`.

### H11 absent-selector canvas admission

Only the canvas-specific H3 authored-style check can omit an ordinary rule
whose entire selector list is proved not to match **any** element in the parsed
DOM (including head/metadata, hidden elements and canvas fallback content).
Global CSS syntax/function/resource checks still see every original byte;
the runtime also receives the original, unfiltered stylesheet. Inline styles,
conditional rules and state/pseudo-element/nested selectors get no exemption.
A matching rule remains subject to the original conservative H3 restrictions,
even if its element appears unrelated to the canvas. This does not establish
geometric independence of subtrees or support for previously unknown CSS.

The internal proof uses the native strict list parser and native matcher, with
a narrow selector profile and worst-case work reservation **before** matching.
Limits are 1,024 DOM nodes, depth 64, 64 attributes/8,192 identity bytes per node,
512 rules per capture, selector source 512 bytes/128 tokens, 16 list branches,
4 steps and 8 compound atoms. Only structural non-nested pseudos and attribute
existence/equality are eligible. Descendant and sibling branching costs are
multiplied conservatively; the capture's total allowance is 16 million abstract
proof-work units. These are not limits for the engine's general CSS pipeline.
If this proof is out of scope, malformed or out of budget, the original H3
restriction is retained. Allocation failure aborts the capture without a PNG.
No absence conclusion is cached across a mutation or a later capture.

Reports record `canvas_unmatched_rules_omitted`, `canvas_absence_match_queries`
and `canvas_absence_work_remaining`; they do not change the honest coverage
fields (`authored_css_validation=NOT_RUN`, Chromium not run).

```sh
sh tests/isolated_capture/run_h11_core.sh /tmp/h11-build
python3 tests/isolated_capture/test_h11.py /tmp/h11-build/aether_capture /tmp/h11-results
```

The original flexbox fixture still rejects. In particular, the actual matched
`FLEX-001` min-height rule remains restricted; H11 fixes only the demonstrated
false rejection for a rule with no matching element. See `H11_EVIDENCE.md`.
