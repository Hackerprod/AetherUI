# T1.3: explicit synthetic A8 composition, not native text availability

This milestone adds a private C++ `software::render_a8` entry point receiving an
immutable prepared frame and explicit immutable `GlyphMaskA8` values. It does not
load a font, shape a string, rasterize a glyph outline, register a provider, or
open the HTML capture gate. Ordinary `render` continues rejecting text. Public C
ABI is unchanged. The original Flexbox fixture is unchanged.

## Contract

Each mask owns exact A8 bytes, dimensions/stride and signed bearings. Its key is
retained `FontInstance` identity, glyph ID, flags and both 0..63 phases. Font hash
metadata is not authenticated. Duplicates, missing keys and noncanonical empty
masks reject; there are no replacement boxes, silent omissions, external mutable
handles or callbacks. Padded rows are permitted with exact stride*height length.

Coordinates use X = rect.x + scroll_x + (pen_x + offset_x)/64 and
Y = absolute_baseline + scroll_y - (pen_y + offset_y)/64. Floor division (not
truncation) derives integer origins and phases, including negatives. Bearings
then locate the bitmap. Origins must be exactly on the supported 1/64 grid;
unsupported fractions reject, without rounding. Accumulation uses bounded int64.
Scroll is applied once. Only identity transforms, DPI 1 and integer rectangular
clips are admitted by this explicit path, including offscreen commands.

A8 is already antialiased; no extra geometry AA is applied. Existing integer
premultiplied BGRA source-over is used: mul(a,b)=(a*b+127)/255, sa=mul(alpha,A8),
RGB=mul(channel,sa). Group opacity is applied only at group close. Command and
glyph ordering are retained.

All key resolution, visible work and scope validation precedes candidate creation.
The 64 MiB A8 resource bound combines unique mask bytes, unique retained font
bytes, conservative metadata reservations per supplied mask and glyph use, root
surface and peak simultaneous group surfaces. Unused supplied masks also count.
A repeated mask costs visible pixel work every time: at most 16 Mi pixel visits,
and included in the existing 50 M global work bound. No cache exists. Per-mask
8192-axis limits and exact length prevent stride/index overflow. Cancellation and
allocation failures preserve the confirmed frame through the existing Guard.

## Verification and honest metrics

`tests/isolated_capture/run_t13_a8.sh BUILD_DIR` links against a clean manual build
from `tests/isolated_capture/build.sh BUILD_DIR`. It requires only the existing C++
toolchain, not CMake, HB/FT, a window, browser or .NET. It exports an exact 16x12
PNG using the existing C++ PNG writer, explicitly named and metadata-labelled
`synthetic-a8-coverage-NOT-HTML.png`. This is a coverage pattern, not real text.

Success metrics include mask_count, mask_bytes, glyph_uses, visible pixel visits,
preparation_ns (validation, association and layer allocation) and composition_ns
(candidate allocation, raster and commit). Wall-clock samples are observations,
not comparative performance benchmarks. Shaping, font load, outline rasterization
and Chromium are NOT_RUN. font_verified=false. These counters do not represent
process CPU/RSS measurements. Tests and timings are recorded in T1_3_report.log.

Tests cover independent byte oracles at 0/1/127/128/254/255, color alpha endpoints,
transparent/opaque destination, overlap, nested opacity, negative floor/phase,
carry, scroll, Y-up offsets/advances, negative bearings, padded stride, spaces,
clips, missing/duplicate/font-identity keys, unsupported domains, resource limits,
all observed allocation and cancellation points, and retained source lifetime.

Next milestone still needs the real font provider and its independently reviewed
shaping/rasterization pipeline. This implementation supplies no evidence of
Chrome parity or real Flexbox text performance.
