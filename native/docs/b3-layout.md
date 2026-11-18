# B3 bounded flex source layout (candidate)

B3 extends the native B0 source-session path when an admitted effective
`display:flex` is present. It explicitly enables B2 source provenance even when
there are no inline elements, wrapping or centered text. No public ABI or
consumer-side renderer is added. This candidate requires independent review;
it does not close the full historical login or Stage 3 of the reference guide.

## Admission

Flex containers are body/div/main/section/aside/form. Items are admitted DOM
block/control elements. Direct text must contain only SP/TAB/LF/CR/FF (empty text
and comments are retained as participants); inline/br and nontrivia direct text
are rejected before the font provider. There are no synthetic anonymous owners.

The bounded profile admits row/column, nowrap, start/center/space-between,
start/center/stretch, nonnegative px gaps, width auto/px/0–100%, height auto/px,
min auto/px, max none/px, and content-box/border-box. Authored flex grow/shrink/
basis/order/self/content declarations, reverse/wrap/baseline flex, percentage
height/min/max and automatic margins remain excluded. Min greater than max is
rejected in this profile. Existing CSS-loss accounting is retained for matching,
nonmatching and overridden declarations. Explicit min:auto is newly parsed by
recognizing only that token; previous defaults and the flex solver are unchanged.
Rejected-value compiler evidence can cause a terminal B3 STYLE rejection but
never exempts lossy CSS from admission.

## Private provenance

The motor assigns monotonic result, box and subtree-generation IDs. Detached
measurement does not create final candidates. B2 snapshots bind to their exact
anchor, placement box, run/occurrence and immutable IDs. Local geometry is checked
before transport. Replacement authorizes supersession before destruction;
superseded candidates retain tombstones and cannot return via address reuse.
Every effective per-box offset is journaled and replayed with the same float
operations. A final reachable-tree seal commits exactly one surviving candidate
(or flex-discard receipt) per canonical context. Final ledger coordinates are
checked again against immutable candidates and replay, independently of boxes.

The flex collector revalidates source participants, original bytes, boundaries,
styles/identities and item order on cache hits. A hit within one generation and
anchor is idempotent; a new generation requires a new receipt. Detached cached
data is never promoted directly to final coverage.

The shared B2 PMR budget remains 4 MiB. New cumulative limits are 4096 candidates,
4096 generations,16384 provisional bindings,65536 movement/replacement events and
65536 final visits. Existing B2 limits are not raised. Work is not refunded on
supersession. OOM, quota exhaustion and integrity failures remain distinct.

## Percentage evidence

Percent width uses the actual containing content box, with semantic evidence
from px, normal-block auto fill, or definite final flex allocation. Numeric
intrinsic/shrink-to-fit width alone is not a certificate. Dependencies are bound
to live generation/box identities and checked at seal; stale references and
cycles cannot certify percentages. Stretch rebuilds instead of adjusting a
previous child by hand. Geometry oracles use tolerance0.001px; provenance and
transport equality are exact.

## Fractional native-control clipping

Only validated B3 sessions select the private `render_a8_b3` wrapper. It uses the
same LegacyRenderStorage/FrameStore and existing render kernel with the existing
FractionalRectA8Clip policy. `render_a8` defaults and previous source profiles
retain IntegerA8Clip; the kernel is unchanged.

For A8 glyph masks the policy filters already aggregated coverage by rectangular
pixel intersection, rounds `floor(mask * area + 0.5)`, then uses existing
premultiplied composition. Nested clips intersect first. This is not vector
outline intersection or a browser-parity claim. Used fractional intersections
cost9 logical units per visited pixel, integral intersections1. Budgets and
transactional publication are unchanged. No rounded clip, effects, authored
hidden overflow or transforms are admitted by this selection.

## Tests and fixtures

New tests cover source admission, exact provenance/stamps, corruption, quotas,
physical allocation failure, percent/shrink/stretch geometry and analytical
fractional coverage/cost/transaction behavior. They are registered in CMake;
manual exact-object/installed-header harnesses are also used where CMake is not
available. Frozen B0/B1/B2 pixels are compared byte for byte.

`fixtures/b0/b3_two_panel_login_layout.html` is new and frozen at SHA256
`5b8a86e09bc3ac4d90033a0534a3d8d7655dd6da9c5a19403eddb813075a74f9`.
It does not replace the original login or the original flexbox validation page.
The latter's structural/display-list tests are not raster or 403-example browser
conformance tests. A separate fractional-control fixture exercises a genuinely
cut glyph, not just a fractional rectangle containing all glyphs.
