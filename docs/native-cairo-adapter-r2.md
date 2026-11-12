# R2 real Cairo adapter source

This is new adapter source using official <cairo.h>, not an implemented Cairo
substitute. Cairo/Pango runtime libraries exist here but SDK headers are absent;
the real adapter was NOT compiled, linked, loaded or executed. No manual ABI
declarations, dlopen, fake SDK or dependency installation was used. CMake is
absent, so its configuration is reviewed statically only. No PNG or speedup is
claimed. Existing C# renderers remain unchanged and active.

## Supported operations

Eight R1 variants: Fill/Stroke and six controls (clip push/pop, transform
push/pop, chunk begin/end). Text is rejected during full preflight, even if
encountered last or viewport is suspended. Pango, gradients, images, shadows,
backdrop filters and scroll remain out of scope. Metadata chunks do not draw.
The eight supported variants do not imply eight painting primitives.

A pure preflight creates the exact operation plan consumed by the real adapter.
It validates clear color, capabilities, rounded geometry and every event's
scaled matrix before preparing a candidate. Matrices classify valid, singular
or invalid based on finite scaled components, determinant and inverse
coefficients. Singular drawing does nothing; a singular clip pushes an empty
clip. Invalid numeric matrices reject. Runtime Cairo rejection still aborts. Preparation errors identify index/tag;
Cairo runtime errors report the global backend status, not an attributed command.
DPI validation also applies to empty frames.

Only clip operations save/restore Cairo state. Each drawing operation and
nonsingular clip sets its absolute PreparedEvent CTM multiplied by DPI. PopClip
ONLY restores state, never installs a possibly singular matrix. Transform and
chunk controls do not touch Cairo; the next actual operation sets its matrix.
This preserves R1-supported clip/transform crossings without mimicking a
single nested save/restore stack. No singular CTM is sent to Cairo.

Rounded paths use the existing C# CairoGeometry proportional normalization,
kappa 0.5522847498307936 and stroke minimum 0.25. Double arithmetic for bounds
avoids float addition overflow. No bitwise visual equivalence is asserted.

## Lifetime and transaction

FrameStore current_viewport is an owner-thread getter by value, leaving output
untouched on error. The adapter takes no independent viewport. Before creating
Cairo objects it obtains one R0 candidate and checks the Cairo stride matches
its exact width*4 and byte extent. ARGB32 is used only on little-endian platforms.
BGRA bytes are top-down premultiplied; clear uses SOURCE, drawing uses OVER.

Candidate guard outlives both Cairo objects. Every exit destroys context then
surface before commit/discard changes pixel ownership. Both error objects and
null pointers are checked; context/surface status and final surface flush/status
are checked. Success commits only after Cairo resources are gone. Failed
prepare preserves previous candidate; failure AFTER successful prepare discards
the new candidate, NOT restoring any candidate that prepare replaced. Confirmed
frame and sequence remain intact unless commit succeeds. Commit exhaustion is
propagated and new candidate discarded. No callbacks, window or presentation
are added. The R0 allocation hook must still not reenter the store.

64 MiB is a per-pixel-surface cap, not a total RSS budget; prepared operations,
confirmed/candidate frames and Cairo internals can coexist. Owner-thread-only,
no concurrent calls/destruction or retained borrowed pixel spans across render.

## Build separation

native/src/rendering/cairo/CMakeLists.txt enables the real target only when
AETHER_BUILD_CAIRO_ADAPTER is explicitly ON (default OFF). It requires the Cairo
SDK through imported PkgConfig::CAIRO. The target compiles native_frame.cpp once
and links the preparation target, with no core or validator dependencies. This
new standalone graph is not connected to production bindings automatically.

## Pure helper test

From repository root:

```bash
N=native/src/rendering/preparation
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG -pthread \
 -I native/include -I "$N/include" "$N/paint_snapshot.cpp" \
 native/src/rendering/native_frame.cpp native/src/rendering/cairo/cairo_geometry.cpp \
 native/tests/test_cairo_geometry.cpp -o /tmp/aether-cairo-geometry-test
/tmp/aether-cairo-geometry-test
```

This test uses the actual geometry/preflight helpers but no Cairo headers,
library, core, window or rasterization. It covers path normalization, zero and
extreme geometry, DPI/composition, singular scopes/recovery, PopClip while
transform remains active, late text rejection, clear validation, and viewport
getter/thread behavior. It does not prove actual Cairo state, pixel output,
error-object behavior or raster transaction performance; those require SDK and
permitted graphics validation later. R0/R1 pure regression commands remain in
their documentation.

## References

Code: bindings/dotnet/src/AetherUI.Linux/Graphics/CairoGeometry.cs and
CairoRenderBackend.cs. External architecture references inspected read-only:
- https://www.cairographics.org/manual/cairo-cairo-matrix-t.html
- https://chromium.googlesource.com/chromium/src/+/fa35c473584ada048107d73cdf716c7913ad135c/cc/trees/draw_property_utils.cc

Cairo documents x'=xx*x+xy*y+x0, y'=yx*x+yy*y+y0. Chromium checks singular
transforms when excluding noninvertible layers; animation/property-calculation
exceptions are not a literal Cairo recipe. Neither source establishes pixel
or clipping parity for this adapter.
