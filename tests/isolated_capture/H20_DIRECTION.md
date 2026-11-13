# H20: bounded RTL flex capture

The geometry CLI accepts literal `direction: ltr | rtl | inherit | unset | initial` using literal lowercase spelling only. Direction is resolved through the runtime and read from pinned frozen measures, not the legacy flat style export. The final style audit runs before layout.

Scope: definite px-sized RTL flex containers and their definite-sized leaf boxes, horizontal-tb, physical margins/padding/borders, row/row-reverse/column/column-reverse, and nested flex. The native engine supports more than this intentionally conservative capture profile. `justify-content:start/end` is logical; `flex-start/end` follows the composed main axis.

A direction declaration activates an all-source audit, including inactive, overridden and unmatched declarations. It excludes unproven display forms, intrinsic or functional size inputs, flex-flow shorthand, wrap-reverse, space-around/evenly, baseline/self-start/self-end, positioned layout, overflow/scroll, writing-mode, unicode-bidi and all resets. Direction variables are excluded by the capture source gate, though the native direction tests exercise variables.

If any final record is RTL, a frozen global interaction audit excludes contents, replaced/generated elements, transforms, backdrop, opacity, intrinsic/contextual sizes, root RTL and RTL non-flex subtrees. Canvas keeps its existing authored direction prohibition. This does not implement textual bidi, root/block RTL positioning, vertical writing or general browser rendering.

Manual test: `python tests/isolated_capture/test_h20.py BUILD/aether_capture NEW_OUTPUT_DIR`. Nine PNG cases use independently specified absolute physical coordinates as controls, not another flex rendering. Additional cases cover nested inheritance, override, source-proof and final-state failures. Chromium was not executed by this test and PNG equality is not a Chromium equivalence claim.

## Closed source property/value profile after independent review

Two review rounds found silently ignored logical properties and legacy Blink
aliases. H20 now uses an explicit allowlist, not a name-segment deny heuristic.
Every non-custom property outside the list is refused, including unknown and
vendor-prefixed aliases. Property names are CSS-decoded for classification, but the original authored spelling must exactly match the literal lowercase name. Escaped/case-variant property names are refused by this profile.

Allowed geometry families: direction/display, explicit flex direction/wrap and
alignment subsets listed above, static position, visible overflow, box-sizing,
physical width/height/min/max, grow/shrink/basis and restricted flex shorthand,
integer order, physical margin/padding, px gaps and physical border
width/style/color plus their registered physical shorthands. Paint is restricted
to simple background/background-color/color. Only listed simple keyword, finite
number, literal px, unitless zero and limited named/hex color values pass.
Margins allow negatives and auto; sizes allow auto (resolved RTL still requires
px); max sizes allow none. Border styles are solid/none/hidden. Background only
accepts a single supported color, not arbitrary layered shorthand. Variables,
functions, other units and unproved forms on consuming properties fail closed.
Custom-property definitions are data, not a way to admit an unknown consumer.
This intentionally restricts even otherwise valid native/CSS inputs.

Audited Chromium's original property registry:
https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/css/css_properties.json5
Its aliases include -webkit-margin-before/after to margin-block-start/end,
-webkit-padding-before/after, border-before/after longhands and shorthands,
and -webkit-[min-/max-]logical-width/height to inline/block sizes. Tests cover
these aliases plus escaped/case variants, variables, !important, inline,
overridden, unmatched and inactive declarations. No Chromium execution claimed.

## Native-parser serialization boundary

An additional independent review found that decoding keyword escapes in the
gate could admit values that legacy native handlers ignored, e.g.
`flex-direction:\63 olumn`. The CLI now rejects escapes and comments inside all
non-custom values and accepts only exact literal lowercase keyword spellings.
This does not weaken the native direction parser's own escaped-identifier
support; the CLI deliberately refuses more until every consumer is normalized
and tested. Custom definitions remain data, while all consuming values are
restricted. The suite checks name escapes/case and value escapes across every
allowed property family, plus important, inline, overridden, unmatched and
inactive declarations. No unsupported spelling is treated as its decoded value.

## Numeric domain

Numeric admission now delegates complete token parsing to the native
`try_parse_numeric_literal` implementation. A finite double alone is not proof
that a native float consumer can accept a length. Float-backed numeric values
must be zero or have magnitude between 0.000001 and 65536 inclusive; negative
values are allowed only for physical margins. Unitless lengths must be zero;
otherwise literal px is required. This is a conservative capture limit, not the
native engine's full numeric range. Integer order uses its separate int32 range.

Functions and percentages remain excluded, and the final global RTL audit caps
active boxes at 2048. Bounded inputs provide substantial arithmetic headroom;
existing layout/paint guards can still reject extreme combinations. Passing a
source bound does not promise that a capture will publish an image. Tests cover
finite coordinates across both factor bounds and all four flow directions,
while a large multi-item CLI case explicitly verifies paint rejection/no PNG.
