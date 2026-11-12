# H7: deterministic paused CSS animation sample before layout

## Base and scope

Published base: `8d8bb5592b00ffda6aa9e1ce24d109b0e36d1c47`, exact tree
`91d8b3efb44e0a11d596ee68215c8bff4f296c87`. Local checkout reconstructs
that content from H6 `105cd72` minus its nine generated PNG/log files. Local
history is not asserted to be complete remote history. No Actions or push.

The five actual authored CSS cases FLEX-374..378 now pass through stylesheet
compilation, cascade, deterministic sampling, native layout and software PNG:
flex-grow 2, flex-basis 150px, both gaps 30px, order 0, direction row at .49.
Their original CSS lines are retained in the five fixtures, without a special
ID/name lookup in the implementation. PNG checks assert resulting geometry.

This is NOT a live animation clock. The bounded profile is one named animation,
paused from creation, local time zero, positive finite duration, linear timing,
one normal iteration and fill none. Both effective endpoints (from/to or
0%/100%) must exist per target longhand. Intermediate/comma selectors, omitted
endpoints, variables, mixed/relative lengths, unsupported targets, lists,
running states and other timing/fill/direction/iteration modes are outside the
capture profile, not necessarily invalid CSS. Explicit animation inheritance
is outside this profile. Native diagnostics plus the CLI's post-cascade gate
reject unsupported effective states before layout; missing keyframe names are
also conservatively rejected. No implicit underlying endpoints are synthesized.

## Semantics and ownership

- Keyframes are owned by their compiled sheet, retain conditional context and
  global source ordering, and are excluded from the selector index. Duplicate
  names choose the last applicable definition in the applicable sheet layer.
- Preparation borrows the owner's keyframe records only during the guarded
  synchronous transaction, under the same lifetime/epoch rules as selector
  records. It materializes final scalar/measure values before freeze. Final
  snapshots contain no pointers to keyframe records or animation clocks.
- Every resolution starts with an unanimated base; it never reuses a sampled
  value as underlying. Sheet removal, animation:none and media environment
  changes restore/recompute from that base without mutating old snapshots.
- The effect overrides normal declarations, never !important. Keyframe
  !important declarations are ignored before endpoint completeness checks.
- At delay=-duration the animation is after its active interval and has no
  effect (fill:none), as with a later delay. Positive delay has no effect.
  Phase comparisons use finite mathematical boundaries, not Chromium's 1µs
  implementation tolerance. order ties round toward positive infinity.
- Keyframe scanning is bounded to 32 nesting levels and 65,536 tokens per sheet,
  including inactive definitions. Publication and IDs remain transactional.

Order was a pre-existing no-op, so H7 also makes it real: a private frozen
measure sidecar, stable order-modified item sequence before layout, and explicit
layout/paint/hit-test invalidation (mask7). DOM order is untouched; anonymous
items have order0. Frozen clones retain it; the legacy flat export rejects
nonzero order without altering the destination. Layout box child order feeds
paint and hit testing. No public struct or function ABI was added.

A pre-existing adjacent bug surfaced during tests: @media(...) with no space
was dropped because the component lexer grouped '(' into a function token.
The compiler now retains that parenthesis as the condition prelude. No second
identifier decoding is introduced.

## Validation

- Parser:54 checks;73 injected OOM points, atomic shorthand expansion.
- Core:45 checks;143 compiler OOM points;155 preparation OOM points.
  Includes endpoints, phases .25/.49/.50/.51/.75, exact end, positive delay,
  important, duplicate names, case/escapes, sheet removal, none, false
  media/supports, environment changes, get_update/clone/frozen lifetime,
  integer/source/depth/token limits and unchanged prior snapshots.
- Gate:22 checks;136 OOM points, including inactive unsupported bodies.
- CLI:43 real PNG or deliberate rejection cases. The first five use original
  authored flexbox CSS, not a replacement engine or hardcoded expected styles.
- Order:negative/tie/int32/parser/reverse/wrap/DOM/base reset/flat guard/clone/
  frozen lifetime tests, animated negative half ties, exact impact7, and two
  software-rendered overlap PNGs with hit-test agreement. PNGs inspected.
- H3:117 CLI cases + native canvas tests. H4:contextual core and exact PNG
  boundaries padding50/28, basis150. H5:48 core,38 RGBA,13 gate, OOM and58 CLI.
  H6:43 core/56 OOM,11 gate/79 OOM,44 CLI.
- H1/H2 input/preflight/artifact/original inventory tests and real memory/CPU/
  wall limit tests pass.
- Existing transition property pipeline/runtime/contextual/frame tests pass,
  including 10,000 interruptions. Style diff catalog:94 keys; classifier1659
  exact comparisons with zero allocations.
- Exported C function set remains176 names, identical to H6.

One actual FLEX-374 run at600x180 measured style0.788973ms, layout0.062848ms,
raster3.65549ms, PNG12.1097ms, child CPU0.017829s, peak RSS5476KiB,
process wall20.442ms. These are one Linux run, not comparative benchmarks.

All outputs retain css_eligibility=Unknown, chromium=NOT_RUN,
authored_css_validation=NOT_RUN, flexbox_semantic_acceptance=false.
The original document remains blocked by other missing features (including
text/SVG); SHA256 remains
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
Windows/CMake/Chromium were not run. No whole-document or Chromium equivalence
claim. Generated PNG/log evidence is delivered in the backup, not committed.
Independent code review is still required before publication.

## Reference basis

Proposal and independent proposal validation consulted CSS Animations and
Chromium, rather than extrapolating paint-only transition semantics:

- https://drafts.csswg.org/css-animations-1/
- https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/animation/css/css_animations.cc
- https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/animation/timing_calculations.cc
- https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/animation/number_property_functions.cc

Final verification rebuilt all71 core translation units, renderer and CLI from
current sources into a new empty directory. Repeated all four H7 unit suites
and43 CLI cases successfully against that clean build. Earlier manual test
invocation mistakes (PNG checker given a binary, and a white-box transition
source linked twice) were corrected; the proper PNG and transition runs pass.
