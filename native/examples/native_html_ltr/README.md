# Native HTML LTR card (ABI 0.36.0)

This C11 consumer supplies licensed regular/bold TrueType bytes and HTML. The core
owns parsing, style inheritance, layout, physical font measurement, rasterization,
and composition. The host only copies/presents BGRA8 premultiplied pixels. It is
not an interactive login: input, button, events, focus, IME and accessibility are
outside this profile. Existing bindings do not automatically migrate to it.

Build with the installed public header and DSO, for example:

```sh
cc -std=c11 -Wall -Wextra -Werror main.c -I../../include -L/path/to/lib \
  -Wl,-rpath,/path/to/lib -laether_core -o native-html-ltr
./native-html-ltr ../../tests/fixtures/fonts/DejaVuSans.ttf \
  ../../tests/fixtures/fonts/DejaVuSans-Bold.ttf card.bgra
```

Output is raw 640×320 BGRA, not a PNG. No host measurement callback is required or
used. Fonts may be destroyed immediately after render returns. Font fixture
license and provenance are in `tests/fixtures/fonts`.

## Profile 1, fail closed

`aether_html_ltr_profile_limitations(1)` always returns both typography-incomplete
and CSS-coverage-incomplete bits (3); unknown profiles return UINT32_MAX.
`aether_html_ltr_options_size()` provides the exact required options size.

- Explicit lowercase html/body; div containers; h1/p text leaves with explicit
  closing tags. Optional exact doctype, one double-quoted inline style attribute.
- Precomposed Latin U+0020–007E/U+00A0–00FF except soft hyphen. No entities,
  combining characters, bidi, nested inline markup, resources, scripts, head,
  comments, form controls or other tags. No shaping or kerning.
- Leaves have no leading/trailing/doubled ASCII spaces. NBSP is preserved.
- Only solid hex colors, transparent background, width:auto or integer px,
  individual margin/padding sides, uniform border-width/color/style none|solid,
  font-family:AetherLTR, font-size 8–48px, weights400/700, line-height8–96px.
  No functions, gradients, opacity, escapes, important, shorthand margins or URLs.
- Html accepts **only** font-family/font-size/font-weight/line-height/color.
  All five inherit; h1/p have no special UA typography. Line-height is absolute
  and must be at least font-size; baseline must be physically representable.
- Html has no painted box. Body is the layout root. A solid **body background
  propagates to the entire viewport**, even if body is narrower. It is not also
  painted locally. Transparent body with colored div has only the local div
  background and transparent uncovered pixels.
- Private UA fixes html overflow to clip and all descendants to visible. No
  authored overflow values are allowed. Text is nowrap, may extend beyond body,
  and is clipped only by the viewport; there are no scroll offsets or scrollbars.

Limits: viewport positive ≤2048×2048 at DPI1, HTML1–65536 bytes, depth16,
256 elements,64 leaves,4096 visible scalars, fonts total16MiB,16 font instances,
256 outlines,32768 measurements,65536 scalar visits,16384 placed glyphs,
16MiB masks and32MiB logical session payload. Logical coordinates and full-leaf
width must fit65536 DIP. The logical quota is not a process RSS guarantee.

Malformed syntax/font/UTF8 is INVALID_ARGUMENT; out-of-profile authored input,
missing bold/glyph and unsupported fonts are UNSUPPORTED; explicit quotas are
LIMIT_EXCEEDED. An unknown/retired internal occurrence is INTERNAL, never omitted
text. No I/O, network or system-font discovery occurs inside the render operation.

Every failure preserves the prior confirmed pixels, metadata and sequence. A
successful call increments sequence once. Resize itself preserves the old frame
until the next successful render. Null options, wrong thread, suspended surfaces
and buffer/canary rules follow the documented public header and native surface API.

The consumer matrix lives in `tests/test_native_html_ltr.c`. CMake/Windows must be
validated in their environments; the local recipe does not establish those passes.
