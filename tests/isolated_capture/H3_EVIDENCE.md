# H3.1 implementation evidence

Implementation tests, not independent implementation approval. Proposal H3.1
was independently PASS before this change. Public publication/Actions NOT RUN.

## Executed

- Complete manual `build.sh` compilation of the current core (71 translation
  units) plus geometry preparation/raster/CLI, Linux C++20 O1.
- Final harness compile `-Wall -Wextra -Werror -Wno-error=extra`; existing
  `measure_state.h:175` enum/non-enum warning is the documented exception.
- `test_h3.py`: **104 real CLI cases PASS**, PNG CRC/decode/pixels, attributes,
  default/zero/ratio/box-sizing/decorations, negative numeric margins, unused
  border widths, sibling placement, transparency, clip and opacity. Includes
  authored escaped-property rejection and conservative unsupported profiles.
- `run_h3_core.sh`: PASS nonzero text measurement sentinel; initial canvas makes
  zero calls, mode-off invokes it. Baseline off equals later off, two engines
  isolated; on result remains usable after engines destroyed. Prepared native
  output rasterizes after layout/style/runtime/DOM destruction. Parser defaults,
  overflow, decoration floor and 1/16/256 registry /257 rejection checked.
- H1 `test.sh` PASS: input limits, text/resources, domain, geometry, no overwrite,
  and original fixture unchanged/rejected.
- H2 `test_inventory.py` PASS: full original inventory (28 CSS observations,
  5 elements), escapes/comments/strings and no loosening of rejection.
- H1 OS limit probes PASS: CPU, memory allocation failure and wall timeout.
- Nine raster suites PASS with UBSan (renderer, stroke, rounded fill/stroke,
  curved clip, shadow, grouped border, opacity, scroll). Raster/preparation/frame
  and test TUs instrumented; existing core objects linked unsanitized for the
  stroke emitter dependency. No full-core UBSan claim.
- Existing R14 and R15 emitter integration tests PASS, linked against all freshly
  built core objects; legacy baseline, two builders, frozen offsets/lifetime.
- `native/include` unchanged. Defined public C `aether_*` symbols: identical
  176-name list against the previous H1 core build. CMake retains hidden C++
  visibility and hidden inline visibility. No public structs/enums/export APIs
  were added; internal layout engine/box fields are private implementation.

## Fix exposed by the new pixel tests

Uniform border opacity 0.5 previously became 0.25 on the native grouped path:
`aether_paint_border` multiplied style opacity, then the group applied it again.
The native emitter now passes opacity 1 to border painting because the enclosing
native group owns it. The public legacy path is unchanged. Test checks against
0.5 compositing over known RGB, not against the incorrect previous output.

## Bounds and honest claims

No transparent canvas bitmap allocated, zero canvas content commands. Registry
payload is 16 bytes/entry on this build, hence 16 /256 /4096 bytes for
1 /16 /256 entries, excluding allocator and hash bucket overhead. Intrinsics
1x1 and 8192x8192 produce the same payload counts. Real Linux wait4 RSS/CPU/wall
measurements are saved with every case; no noisy timing threshold claims speed.
The renderer's existing global frame/work/process bounds are unchanged.

Absent min sizing may be AUTO in the flat default style despite builtin default
registration string `0`. This is admitted only as absence: authored min:auto is
rejected from decoded declarations, including unmatched rules. No claim about
CSS automatic minimum size follows. The profile requires no flex deficit.

Original SHA256 remains
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
Its real CSS/SVG/text blockers remain and **no PNG is produced**. Chromium NOT
RUN; authored CSS coverage Unknown; flexbox_semantic_acceptance=false.

Native no-mode code paths and public API remain default-off. H3 is explicitly
activated by the isolated harness only. Full typography, SVG, executable JS,
canvas drawing/context APIs, arbitrary Flexbox/CSS and browser parity are not
implemented or claimed.

## Independent review correction: no-deficit gate

First independent implementation review found FAIL: a sibling with border-box
width 1px and 100px padding on both sides was counted as 1px instead of its 200px
decoration floor. In a 150px row with canvas border-box104px, this wrongly passed
the no-deficit admission. Reproduced with successful PNG before correction.

Corrected all sibling bases to honor used border/padding decoration floors and
min/max/profile bounds, and require each final sibling border width to equal its
hypothetical width (not merely the canvas). Gap accounting now calls the same
frozen typed `layout_used_gap` as layout, preserving column-gap, two-value gap
and percentage semantics instead of relying on the lossy legacy scalar.

Expanded integration suite: **117 real CLI cases PASS**, including ten rejection
cases for sibling padding/border floors, actual shrink, margins, column-gap,
two-value and percentage gaps; three positive controls prove valid decoration
floors and main/cross gaps are not blanket-rejected. H1/H2 rerun PASS. Native
core/raster unchanged from the initial implementation checks above. Independent
implementation re-review remains required; the original FAIL is not erased.
