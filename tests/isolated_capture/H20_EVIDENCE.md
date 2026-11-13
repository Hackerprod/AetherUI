# H20: inherited direction and horizontal-tb flex geometry

Local implementation based on H19 (tree equal to published main bf76daa).
No Actions were enabled or executed. No remote publication by implementation.

## Contract

`direction` uses the native identifier parser, inherited internal immutable
measure metadata, cascade, custom-property substitution and CSS-wide reset.
Public flat structs and offsets are unchanged. Exporting RTL into the legacy
flat style API fails explicitly: that representation cannot preserve direction.
Snapshots, clones and style diffs retain the metadata.

Flex placement distinguishes flow reversal, physical main-axis reflection and
cross-axis reflection. In horizontal-tb, row RTL reflects main placement;
column RTL reflects cross placement. Row/column-reverse change flow, not DOM
order. Logical justify start/end differ from flex-start/end on reverse flows.
Physical margins remain physical; descendants translate without mirroring.

This is not a bidi implementation. The CLI requires bounded, definite geometry,
no text, static positioning, visible overflow, no contents, no transformed or
replaced RTL subtrees and no intrinsic/contextual RTL interactions. Root RTL,
RTL non-flex block subtrees and wrap-reverse are unproved and fail closed.
Native support is broader than the deliberately conservative CLI admission.

## Reference and independent oracles

Consulted CSS Flexbox axes specification and Chromium implementation:
- https://drafts.csswg.org/css-flexbox-1/#flex-direction-property
- https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/layout/flex/flex_layout_algorithm.cc

No Chromium execution or comparative timing is claimed. Expected coordinates
are computed independently in tests, not obtained from production helpers or
an engine-generated control. No claim of full Chromium/CSS parity.

## Verification

Core/support objects compiled clean in `/workspace/shared/h20-clean-20261113`
(71 core and 9 support translation units), then both CLI entrypoints linked.
- Layout: 4505 checks, 45 injected mandatory OOM points, retained result intact.
  LTR/RTL x four flex directions x six justify values x five align values,
  hit testing, wrap, physical decoration, auto margins, order, overflow free
  space, nested directions, inheritance and mutation versus fresh runtime.
- Style: 1247 checks, 222 injected OOM points, snapshot/clone lifetime,
  cascade/variables/resets/removal and legacy export rejection.
- Style-diff classifier: 1708 exact comparisons, zero allocations.
- Native H3–H19 regression scripts run locally; details finalized below.

Original `tests/visual_compare/fixtures/flexbox_runtime_validation.html` remains
unchanged, SHA256 eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837.
It remains rejected by the capture profile. No substituted simplified fixture
is represented as a rendering of the original.

Final verification:
- Adversarial review test: 146 checks of all four physical auto margins across
  four RTL flow directions; overlapping hit targets with explicit order and DOM
  order. No implementation defect found in this bounded scope.
- CLI H20: 54 checks, nine PNG equality tests against absolute-positioned boxes
  with independently specified physical coordinates. Row image visually
  inspected (blue x90..139 and red x150..179); correct gap and placement.
- Native H3–H19: PASS; H8 obsolete direction-rejection assertions replaced with
  explicit supported literals and unsupported-value negatives (77 gate checks,
  54 OOM). H19 dedicated gate: 14 checks, 14 OOM.
- CLI H3,H5–H11,H13–H19: PASS. Existing canvas direction refusals and original
  fixture text-blocker diagnostics retained without relaxing those expectations.
- H4 exact pixel boundary oracle: PASS.
- One local row capture: wall 10.344 ms, child CPU 5.659 ms, peak RSS 3276 KiB.
  Single run only, not a benchmark or Chromium comparison.
- `git diff --check`: PASS; original fixture SHA256 verified unchanged.

Reproduce with `build.sh`, `run_h20_core.sh`, `run_h20_style.sh`,
`run_h20_adversarial.sh` and `test_h20.py` in this directory. Regression and CLI
artifacts for this run are under `/workspace/shared/h20-*`. No images, private
logs or build products are committed.

## Independent review correction: logical box properties

Review found false admission of `margin-inline-start`: it was silently ignored,
producing the same raster as no margin. Corrected by rejecting logical property
families in the authored direction-aware gate, after identifier normalization.
No native layout behavior was changed to mask the issue.

Expanded CLI suite: 137 checks (previously 54). The new matrix covers logical
margin/padding/inset/scroll families, inline/block sizes and min/max variants,
border longhands/shorthands and logical corner radii. Escapes, case, var values,
important, inline, overridden, unmatched and inactive declarations must reject
with `DIRECTION_AUTHORED_PROFILE_UNPROVEN` and publish no PNG. Positive physical
margin-right must change the raster; custom-property names remain valid.
Both normal/probe CLI entrypoints rebuilt using unchanged verified native objects.
Correction regressions H3/H8/H9/H11/H18/H19 CLI: PASS. Diff whitespace check PASS.

## Second independent correction: explicit property/value admission

Review reproduced ignored Blink legacy aliases such as -webkit-margin-before.
The first name-segment deny strategy was incomplete and has been removed.
The final gate defaults to refusal for every non-custom property, allowing only
explicit registered physical properties and narrowly checked values. This also
refuses future/unknown/vendor properties without needing another deny keyword.

Audited Chromium css_properties.json5 (official raw source) for before/after,
start/end border, margin/padding and logical width/height aliases. Regression
cases exercise all these aliases and escaped/case/var/important/inline/cascade
variants. Added value-level negatives and positive physical cases. See
H20_DIRECTION.md for the definitive, intentionally narrow accepted set.
Final second-correction verification: H20 CLI 200 checks PASS. All prior CLI
regressions H3,H5-H11,H13-H19 PASS with the closed allowlist. Native objects are
unchanged; both normal and limit-probe CLI entrypoints were rebuilt. H8 native
source/OOM gate rerun PASS. `git diff --check` PASS. No remote publication.

## Third independent correction: literal serialization boundary

Review reproduced a gate/native mismatch: escaped keywords such as
`flex-direction:\63 olumn` decoded in the gate but were ignored by legacy native
property handlers. The gate no longer decodes keyword values. Every non-custom
value refuses escape/comment serialization, and keywords require literal
lowercase spelling. Authored property spelling must equal its normalized name;
escaped and case-variant names are conservatively refused. Native direction's
own escape support is unchanged, but not sufficient to claim all-property
normalization in this capture profile.

H20 CLI expanded to 450 checks: each admitted property family has a successful
literal case and negative escaped-name, case-name and escaped-value cases.
Additional negatives cover important, inline, overridden, unmatched and inactive
escaped keyword declarations. Refusals require the profile diagnostic and no
PNG; existing independent pixel oracles still pass. Normal/probe CLIs rebuilt;
CLI regressions H3,H5-H11,H13-H19 PASS. H8 gate 77 checks / 57 OOM PASS.
No native source/object changes or remote publication. Diff whitespace PASS.

## Fourth independent correction: numeric representability

Review found `1e100px` accepted as a finite double by the gate but rejected by
native float length parsing. Numeric gate parsing now uses the actual native
complete literal parser, with an additional conservative domain: zero or
absolute magnitude 1e-6 through 65536 for float-backed inputs. Order retains
its int32 range. This refuses float overflow, double/float underflow and
unproved extreme arithmetic rather than allowing fallback to earlier values.

Final H20 CLI: 686 checks PASS. Negative tests span all physical size, margin,
padding, border-width, gap and basis properties, factors, flex shorthand and
order. Positive zero/exponent/min/max boundaries pass. An extreme multi-item
sum/product case reaches the existing native paint guard and is intentionally
asserted rejected without PNG, not claimed as a successful render.

Layout suite: 4857 checks, 45 OOM PASS. New arithmetic stress covers both factor
bounds across four RTL flex flows, asserts finite bounded box coordinates and
compares results against a fresh runtime. Native implementation unchanged;
normal/probe CLI entrypoints rebuilt from verified native objects.

Final numeric correction CLI regressions H3,H5-H11,H13-H19 PASS; H8 gate 77 checks / 57 OOM PASS. Whitespace check PASS. No remote publication.

## Property-role contract correction after independent min/max FAIL

Review identified a second, distinct contract mismatch: native min-size ignores
CSS-valid `auto`, whereas native max-size accepts CSS-invalid `auto`. Shared
size-keyword admission is removed. Width/height/basis retain px|auto, min-size
admits only px, max-size admits px|none. Native code is unchanged; this correction
closes capture admission rather than pretending to implement CSS automatic minima.

`H20_PROPERTY_CONTRACT.md` records every admitted family, keyword domain, numeric
and shorthand constraints, actual consumer limitations and the distinction
between authored admission and final resolved scene acceptance. In particular,
native parser acceptance alone is not a CSS conformance oracle.

Rebuilt normal and limit-probe CLI entrypoints from the existing verified native
objects. H20: **1219 checks PASS**. Added per-property size keyword matrix and
important/unmatched/inactive/overridden/substitution contexts for all four min/max
properties, plus a cross-family keyword matrix for thirteen enum properties.
Positive authored-keyword probes use a final safe override to isolate the source
gate from resolved-size restrictions (width:auto can still be rejected there).
These gate tests are not claimed as raster fidelity tests for the overwritten
keyword. Existing independent physical-coordinate PNG comparisons still pass.

Regression CLI H3,H5–H11,H13–H19 all PASS. Original flexbox file unchanged;
Chromium NOT_RUN, full CSS conformance NOT_CLAIMED. No publication. Independent
implementation review is still required before any H20 promotion.
