# R4 concrete Pango provider source

PangoLayoutOwner is real source using official Cairo/Pango headers. It owns one
R3 request, PangoContext and PangoLayout. metrics and draw use that same layout,
not independently shaped approximations. No cache, fake provider or heuristic
metrics exist. No core integration; R2 still rejects Text. Existing C# stays.

## Precisely limited capabilities

Intrinsic supports unconstrained nowrap (multiline text retained); any present
max width, including zero, rejects. Paint supports nowrap, one paragraph and
physical LEFT/CENTER/RIGHT only. START/END/JUSTIFY and wrap reject. CR/LF/VT/FF,
NEL, U+2028/U+2029 reject in paint. Pango auto_dir remains true for bidi shaping;
no unavailable CSS direction is invented. Intrinsic cannot draw because no
paint rectangle is supplied. Paint zero width/height is an empty clip/no-op,
not an unconstrained drawing rectangle. Native layout width=-1 represents nowrap
for BOTH purposes; paint width is a separate retained clip/alignment dimension.

All requests retain R3 validation. Additional conservative input limits are
font size<=16384 and UTF8 byte count*font size<=1,000,000. This is a resource
input bound, NOT a predicted text width or assurance against native allocation
failure. Size conversion is double*PANGO_SCALE, checked without saturation or
truncation. Context resolution is96DPI and font size is absolute DIPs. Pango
fallback/tofu may succeed and is not evidence requested fonts were available.

Metrics return logical x/y/width/height and baseline relative to layout origin,
all in DIPs, plus supplied optional used_line_height unchanged. No baseline
clamp or synthesized line height. Placement subtracts logical offsets:
layout_x=paint_x+factor*(paint_width-logical_width)-logical_x;
layout_y=paint_y-logical_y, factor0/.5/1 for LEFT/CENTER/RIGHT. This is a single
paragraph contract, not a claim to align mixed-direction paragraphs identically.

## Ownership, drawing and errors

Noncopyable/nonmovable, owner-thread methods. Wrong-thread methods reject before
native calls; wrong-thread destruction terminates before releasing native objects.
Layout is unreffed before context. Default font map is borrowed. Creation uses
RAII for partial failure and catches C++ bad_alloc only. GLib/Pango allocation
abort is not a catchable error and no status coverage beyond native APIs is claimed.

Cairo target is borrowed and prechecked pointer/status. Position/color and metrics
are validated before changing Cairo state. Clip is exact paint rect. A temporary
group draws at alpha1 with OVER, then composites OVER with requested alpha so
color glyphs receive overall opacity. CTM is unchanged and update_layout is never
called: it would mutate the private context for target CTM/surface. Group pattern,
Cairo save and prior path are guarded; all exits attempt pop/destroy/restore. Cairo
sticky errors can prevent restoration operations from taking effect; errors are
reported, not cleared. Failures may leave partial target pixels, NOT rollback.
Future integration must draw into R0 candidate, not confirmed frame. Cairo group
resources are additional memory, not bounded by R0's per-surface64MiB cap.

## Verification limits

SDK headers and CMake are absent. Real pango_layout.cpp has NOT been compiled,
linked or executed. No Pango/Cairo runtime loaded, no fake ABI declarations,
no SDK installation, graphics execution, PNG or performance evidence. CMake is
opt-in default OFF and statically reviewed only; it requires real pangocairo and
Threads targets. Provider is not installed or linked into production bindings.

Pure helpers used by the source provider are tested using synthetic metrics:

```bash
N=native/src/rendering/preparation
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG \
 -I native/include -I "$N/include" "$N/paint_snapshot.cpp" \
 native/src/rendering/text/text_request.cpp \
 native/src/rendering/text/pango_request_conversion.cpp \
 native/tests/test_pango_request_conversion.cpp -o /tmp/aether-pango-conversion-test
/tmp/aether-pango-conversion-test
```

The placement helper requires a request already accepted by pango_input; it
checks placement, metrics and color, not provider capabilities or input limits.
PangoLayoutOwner.create enforces this precondition before any native layout exists.

No SDK included/linked in that test. It covers constraints, size budget, paragraph
separators, alignment, logical offsets/RTL-shaped synthetic metrics, empty clips
and errors. It does NOT establish actual Pango shaping, Cairo cleanup behavior,
color glyph rendering, font fallback, ABI linkage or image correctness. R0-R3
pure regression commands remain in their documentation.

## Next integration gate

Real SDK build followed by permitted tests with deterministic fonts: RTL,
ligatures, combining text, trailing spaces, baseline/offset placement and emoji.
Only then extend R2 preflight to create every required layout before candidate
mutation and enable its text capability. Unsupported alignment/wrap require a
separate explicit design, not silent fallback.

## Sources inspected

- bindings/dotnet/src/AetherUI.Linux/Graphics/PangoTextLayout.cs
- https://docs.gtk.org/Pango/method.Layout.set_auto_dir.html
- https://docs.gtk.org/Pango/method.Layout.set_width.html
- https://docs.gtk.org/Pango/method.Layout.get_extents.html
- https://docs.gtk.org/PangoCairo/func.update_layout.html
- https://chromium.googlesource.com/chromium/src/+/e459c1e7c797c2bade8d35ed20c9cbff9b4837a6/third_party/blink/renderer/platform/fonts/README.md

References explain direction/alignment, logical extents and shared font machinery;
they do not establish equivalence of this limited adapter with Chromium rendering.
