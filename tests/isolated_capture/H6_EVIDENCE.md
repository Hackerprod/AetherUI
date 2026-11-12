# H6: compile-time supports, bounded geometry admission

## Scope

Built on H5 tree `203738a6901baf2167a8af41dc48a52e46125897`, identical to
published main `16b702b6be8ffd1f9f60bf1ec37e88dcca893aaa` (local ancestor
`28fcfa16a4f8967463d352b162d8ecd67d61a0b6`). No Actions or remote push.

Core `@supports` compiles at stylesheet time, preserves order and inherited
media conditions, and does not evaluate per node. Declaration probes require
one complete declaration, permit only terminal `!important`, and use the real
registered property parser without applying styles or invoking callbacks.
Display identifier decoding is shared with normal CSS, including escapes and
CSS-wide modes. The caller initializes builtin definitions before probing;
the probe itself does not create/mutate that registry.

General-enclosed is CSS false, not assumed malformed; it is always outside the
capture profile. Its grammar follows Chromium `ConsumeGeneralEnclosed` (lines
262–275 in the source consulted), including empty and non-identifier contents:
https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/css/parser/css_supports_parser.cc

All evidence accumulates before folding. The geometry profile certifies only
implemented standard display literals and initial/inherit/unset, not
revert/revert-layer, line-break, variables, arbitrary functions/properties or
general-enclosed. Longhand/shorthand registered parser truth is not a promise
of universal CSS.supports: special content/custom-property compiler paths are
outside this profile. No public ABI additions or JS API.

Conditional nesting (media plus supports) is <=32, including false branches.
A shared 65,536-source-token budget is monotonic across every supports condition
in a sheet. Lexer/identifier and normal property-parser scans add bounded
linear work; this budget is not claimed to count all parser/layout operations.
Allocation failure aborts compilation without publishing candidate rules or IDs.

## Manual validation

- H6 core: 40 checks; 56 injected allocation failures verified transactional,
  including media ownership. Mixed nesting32 passes,33 rejects; total budget
  exhaustion rejects without partial publication.
- H6 shared inventory/admission:11 checks;79 injected OOM points.
- H6 CLI:42 real PNG or deliberate fail-closed cases. Exact pixel assertions
  check a6px blue border only for true conditions, false branch absence, order,
  specificity, !important, comments/case/escapes and both media nesting orders.
  Images inspected visually too. CPU/RSS/wall fields are actual Linux metrics.
- H5 regression:48 core checks (selector OOM29, media OOM12),38 RGBA checks
  (OOM6),13 gate checks (OOM63),58 CLI cases. One former rejection of valid
  supports nested in print is now covered positively by H6.
- H3:117 CLI integration cases and native canvas core tests pass.
- H4: contextual native tests and exact PNG boundaries padding50/28,
  flex-basis150 pass.
- H1 input/preflight/domain/artifact/original-fixture tests pass.

All PNG reports retain `css_eligibility=Unknown`, `chromium=NOT_RUN`,
`authored_css_validation=NOT_RUN`, `flexbox_semantic_acceptance=false`.
Original flexbox bytes remain SHA256
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
The original remains rejected for real missing features; no full PNG or Chrome
comparison is claimed. Windows and CMake builds were not run.

Clean manual build compiled all71 core translation units plus renderer/CLI.
Repeated H6 core and42 CLI cases against clean binary. H2 inventory counts
also pass. Exported C `aether_*` symbol set is unchanged:176 names.
Example `fixtures/h6_supports.html` at1280x80 produced the genuine PNG and:
style0.596443ms, layout0.052582ms, raster3.22094ms, PNG11.2068ms,
process wall20.9616ms, child CPU0.016092s, peak RSS5328KiB.
These are one measured run, not benchmark claims or Chrome comparisons.

## Independent-review correction: escaped backslash is data

The first candidate failed review: `(display:\5c 66lex)` incorrectly became
`flex` because its decoded text was sent through display tokenization again.
Reproduced before correction (`syntax=true, matches=true, profile=false`).
CSS-wide classification now uses a separate decoded identifier; the normal
registered parser receives the original source, never decoded text.
Added probes, normal-declaration checks, positive/negated rule publication and
real-escape CSS-wide regression. Corrected result:43 core checks/56 OOM,
11 gate checks/79 OOM,44 CLI cases. Re-ran H1/H2, H3=117 CLI, H4 pixels,
H5=58 CLI and all H5 unit/OOM tests successfully. Independent re-review pending.
