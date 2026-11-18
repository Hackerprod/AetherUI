# B0 source session (v1)

`aether/aether_b0_source.h` is an additive C11 ABI. It is not HTML profile 1/2
or the interactive R3.1 session. Discovery returns additive bits
`AETHER_B0_SOURCE_STATIC_STYLESHEET_CONTROLS` and
`AETHER_B0_SOURCE_STATIC_SEMANTIC_PAGE`. All operations are synchronous
and confined to the thread that created the session. Wrong-thread calls return
`INVALID_STATE` without modifying outputs. Destroying NULL succeeds. Stale
handles and concurrent destruction are caller errors.

This increment supports immutable HTML with stylesheet/class/id cascade and
static text/password inputs and buttons, at fixed size and DPI 1. It does not
support resize, media queries, interaction, external resources, authentication,
or general browser HTML/CSS. Another size needs another independent session.

Create copies HTML and regular/bold fonts. The caller may release them on return.
Create parses once, compiles styles through the production compiler, resolves,
validates, lays out and prepares the scene but publishes no pixels. Render uses
the existing software/A8 renderer and transactional FrameStore. First success
has sequence 1; later renders are no-ops. Failure leaves confirmed pixels intact.
Copy has no borrowed memory; NULL/0 queries metadata. An undersized destination
receives no pixels but metadata is returned. Other errors preserve outputs.
Metadata and pixels may not overlap. Bytes beyond `byte_count` are not written.
Pixels are top-down premultiplied BGRA8, stride `width*4`, alpha included.

All structures require exact size, version 1 and zero reserved fields. The
optional caller-owned diagnostic contains numeric status/stage/reason only,
with UINT64_MAX for unavailable indices/offsets. A malformed diagnostic is not
written. Diagnostics report only the first reached failure, not all blockers.
Password authored bytes remain in the private original HTML/DOM; this is not a
secure-erasure guarantee. Visible password products/runs contain generated
U+2022 bullets, one per scalar, never the authored value.

## Limits and subset

* HTML: valid UTF-8 without embedded NUL, at most 64 KiB.
* Fonts: both required, combined at most 16 MiB. Alias AetherB0 binds directly
  to these bytes; there is no system-font search or fallback.
* Frame: positive dimensions <=8192, at most 64 MiB, DPI 1.
* Postparse: <=256 elements, depth <=16, <=32 controls; <=4096 visible scalars,
  <=256 scalars per control. These limits do not bound all parsing allocations.
* Text: U+0020..U+007E and U+00A0..U+00FF excluding U+00AD; authored bullets
  are not admitted. Sizes 8..48 integer px, weights 400/700, line-height integer
  px >=size and <=96. Missing glyph rejects. Direct leaf text only; no mixed
  inline markup, wrapping or shaping extension. Simple interior spaces only.
* Block/static/content-box, width auto/px, height auto, nonnegative px edges,
  uniform solid/none border, opaque text/solid background or transparent
  background. No authored display:none, flex, positions, percentages, calc,
  custom properties, gradients, shadows, radii, transforms or overflow.

UA v1 is private and precedes authors: zero box edges, auto width/height,
transparent backgrounds, block display, AetherB0 16/400/24, #202020, nowrap,
ltr/start. Metadata head/title/style/meta is hidden and never enters the font
provider. Root clips to viewport and remains transparent. Body background is
propagated by the existing native builder to the viewport; transparent body
leaves transparent black. Static inputs use existing atomic-control presentation:
placeholder gray (0.42,0.45,0.5,1), vertical centering and content clipping.

Any CSS loss rejects, including nonmatching rules and inline declarations. The
runtime-owned DOM association proves each style's live identity, exact bytes,
epochs, handle and document order. Merely adding identical CSS through the
public stylesheet function does not cover a DOM source. Compiler accounting is
aggregated once, never cleared or replaced with a coverage flag. All at-rules
are outside B0. AST subset evidence is collected by the existing compiler;
there is no second CSS parser.

The provider retains its existing cache/payload/raster limits. Those are partial
instrumented domains, not a total process-memory quota. The historical login,
full browser coverage, reference-backend parity and P4 remain separate gates.

## B1 static semantic page

B1 adds main/section/aside as ordinary block containers in body or other flow
containers, with the same UA defaults as div. Native tag identity is preserved
for selectors and diagnostics. No accessibility, navigation or ARIA behavior is
implied. Leaf/control and metadata content restrictions still apply to the
parsed DOM, not to unparsed source spelling.

In head, one exact `meta name="viewport" content="width=device-width, initial-scale=1"`
is accepted (only those two attributes). Existing `meta charset="utf-8"` remains
accepted. Values, spelling and whitespace are compared after native HTML parsing.
This acknowledges a declaration compatible with the caller's fixed CSS viewport
and physical dimensions at scale 1, not general viewport emulation. Duplicates,
additional attributes, numeric widths, zoom, viewport-fit, min/max-scale and
user-scalable are unsupported. Metadata remains unpainted. ABI structs and their
version stay unchanged; discover this extension by its capability bit.

The new b1_semantic_page fixture is not the historical page. A separate
semantic_equivalence/div_control pair changes only element tags, retaining the
same class selectors, specificity and source order for exact pixel comparison.
Tag selector identity/cascade is verified independently. No flex, inline text,
wrapping, extra controls, media queries, automatic fonts or effects are added.
