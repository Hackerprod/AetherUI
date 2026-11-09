# Drawing modules

A drawing module creates one renderer that also owns its text measurement pipeline.
`IRenderModule<TTarget>` is selected explicitly by the caller. There is no plugin
scanner, global registry, or dependency on a native window handle in Core.
`IRenderBackend` remains source-compatible with existing backends.

## Units and ownership

`RenderViewport` records physical pixel dimensions and a DIP-to-pixel scale.
Display-list coordinates and text measurements are DIPs. A successful `Create`
returns a caller-owned `IRenderBackendWithText`; after successful transfer to
`UiDocumentHost`, only that host disposes it. Do not dispose the text measurer
separately. A module's target is borrowed, and must outlive its renderer.

Calls belong to one owner thread. Linux rejects cross-thread and reentrant calls;
Windows retains its existing threading restrictions without new deterministic
thread guards. Dispose is idempotent on the owner thread.

## Windows

`WindowsDirect2DRenderModule` creates the existing `Direct2DRenderBackend` directly.
The public constructor, DirectWrite pipeline, incremental interface, WARP path,
resource recovery and diagnostics remain available. `NativeWindow.Attach` uses
this factory without changing window messages or input dispatch.

Legacy Windows size/DPI behavior is retained: creation clamps dimensions to at
least one pixel, nonpositive resize leaves the previous surface in place, and
invalid DPI falls back to 1. These are not the Linux suspension rules.

## Linux scope

`AetherUI.Linux` is a software, offscreen/embedding renderer using system Cairo,
Pango/PangoCairo, GLib/GObject, and their shaping/font dependencies. It is **not a
Linux window application**: no X11/Wayland window, event loop, IME, clipboard,
accessibility, GPU, VSync, or incremental rendering is provided. The renderer
replays the full display list and supports live scroll providers.

The first deployment target is Linux glibc x64, little endian. Do not infer musl,
Arm64 or big-endian support from compilation. Cairo ARGB32 memory on this target
is BGRA8 premultiplied, top-down, with four bytes per pixel. External image I/O
remains the host's responsibility; drawing never fetches image URLs. Image tile
geometry is resolved by the existing core, including repeat/space/round. As on
Windows, commands without resolved image geometry are not painted.

Native libraries are supplied by the operating system, not downloaded at runtime.
On Debian/Ubuntu, install `libcairo2 libpango-1.0-0 libpangocairo-1.0-0 libglib2.0-0`
(or the distribution's t64 replacement) plus appropriate fonts. Package names
vary by distribution. Missing-library errors must identify the dependency. Fonts
change shaping and raster output; use fixed distribution/font versions for image
baselines. Cairo/Pango output is not promised to be pixel-identical to DirectWrite.

## Frame contract

A Linux presenter receives a complete frame synchronously. Its buffer is valid
only inside that call; copy before retaining it. The included memory presenter
commits a copied frame atomically. Zero-sized viewports suspend presentation;
resuming requires a full repaint. Invalid DPI, excessive dimensions or allocation
budgets fail explicitly. Resize prepares replacement resources before committing.

Drawing failures never publish an incomplete frame. An external presenter can
publish and then throw: that outcome is **uncertain**, not rolled back. The error
must propagate; the renderer must not count success or automatically retry.
The caller decides whether to request another frame. The last internally confirmed
frame and an external presenter's actual display may differ after such a failure.

## Required command conformance inventory

This inventory is defined before implementation. A fixture for every concrete
`PaintCommand` is required; adding a command without a fixture must fail tests.
Each entry below is a validation requirement, not an assertion that validation
has already passed.

| Command | Required behavior and fixture |
| --- | --- |
| FillRoundedRectangle | All eight elliptical radius components, alpha and clipping |
| DrawBoxShadow | Inset/outset, spread, offsets, blur halo and rounded clip |
| FillRoundedRectangleGradient | Independent gradient rectangle, stops, opacity and tile phase |
| StrokeRoundedRectangle | Elliptical corners and stroke width |
| FillBorderSide | Outer/inner geometry, each side, style and corner joins |
| DrawText | Shared Pango measure/draw, wrap, alignment, trailing spaces, multilingual shaping |
| DrawTextGradient | Same shaping with gradient and opacity |
| DrawTextShadow | Same shaping with offset, blur and clipping |
| ApplyBackdropFilter | Snapshot of earlier pixels, ordered blur/brightness/contrast/saturate |
| DrawBackgroundImage | Premultiplied source validation, core-resolved tile geometry, repeat/space/round |
| PushClip | Rounded clip captured in current transform |
| PopClip | Matched stack; underflow rejects frame |
| PushTransform | Same affine composition order as Windows |
| PopTransform | Matched stack; underflow rejects frame |
| PushScrollOffset | One consistent provider snapshot per ID/frame |
| PopScrollOffset | Matched stack; underflow rejects frame |
| DrawScrollbarChrome | Current tracks, thumbs and corner from provider |

Unknown commands, non-finite geometry and mismatched stacks must fail before
presentation. Combined fixtures cover transform + clip + scroll + shadow + backdrop
+ image. Text fixtures cover Spanish, combining marks, Arabic/RTL, CJK, emoji,
empty text, trailing whitespace, wrap and mixed font sizes. Pixel checks compare
geometry and alpha with tolerances, not universal cross-platform text hashes.

## Architectural references

Chromium uses Skia, not this Cairo backend. Its separation between platform
surfaces, software output and text shaping informs this design:

- [SurfaceFactoryOzone](https://github.com/chromium/chromium/blob/main/ui/ozone/public/surface_factory_ozone.h)
- [SurfaceOzoneCanvas](https://github.com/chromium/chromium/blob/main/ui/ozone/public/surface_ozone_canvas.h)
- [SoftwareOutputDevice](https://github.com/chromium/chromium/blob/main/components/viz/service/display/software_output_device.h)
- [HarfBuzzShaper](https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/platform/fonts/shaping/harfbuzz_shaper.h)

These references do not imply Chromium visual conformance. Windows runtime
regressions and Linux command/pixel tests are separate gates. A Linux cross-build
of the Windows project is not Windows execution evidence.
