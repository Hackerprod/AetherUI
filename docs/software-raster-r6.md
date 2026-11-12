# R6.1 own CPU rasterizer (experimental)

Design R6.1 independently approved before implementation. This is a limited,
original implementation, not BUI-derived code and not a replacement for full
Aether HTML rendering. Existing Cairo/Pango and Windows paths remain untouched.

`native/src/rendering/software` consumes immutable PreparedFrame and writes
FrameStore candidate pixels. It supports rectangle Fill and nested rectangle
clips with zero radii, identity transforms and chunk delimiters in paint order.
All other commands, rounded radii and DPI other than 1 reject before candidate
allocation. Preparation validates finite payloads and scope balance. No windows,
OS graphics, GPU, Skia, Cairo or Pango are required for this implementation.

## Numerical contract

Pixels are top-down BGRA8 premultiplied, encoded sRGB (not linear-light blending).
Float coordinates promote to double. Geometry is clipped to the viewport before
floor/ceil or integer conversion. Rectangle edges are semiopen; coverage is the
area of shape/accumulated clip/pixel intersection, not multiplied clip alphas.
Quantize normalized channels/coverage using floor(x*255+.5).
`mul255(a,b)=(a*b+127)/255` (integer division). Source alpha is
mul255(alpha,coverage), premultiplied channel mul255(channel,source_alpha).
Source-over is source + mul255(destination,255-source_alpha), per channel.

## Lifetime and budgets

8192 per axis and 64 MiB per surface are existing FrameStore limits, not a total
process-memory cap. Clear + clipped fill bounding boxes must total <=50 million
pixel evaluations. Preflight checks multiplication/addition by remaining-budget
division, before allocating a candidate. Preparation separately limits commands
and scope depth. Plan memory is additional, bounded by command count.

Clear is transparent zero. A candidate-discard guard covers all error paths.
The borrowed view expires before commit/discard. Cancellation is polled during
preflight, per draw and row (including clear), then once before commit. Store and
callbacks must not be reentered or externally mutated during synchronous render.
Errors preserve confirmed pixels and sequence. Cancel is cooperative; no promise
is made that an asynchronous request after the last poll aborts commit.

## Evidence and scope

- Strict C++20 compile and rectangle/clip/alpha/ordering tests PASS.
- Invalid payloads, unsupported late command, DPI, resource bound, per-callback
  cancellation (including final poll), simulated allocation failure, owner-thread
  rejection and prior-frame preservation PASS.
- UBSan standalone test PASS; existing FrameStore regression PASS.
- ASan/LSan combined attempt did not complete successfully: LeakSanitizer reports
  unsupported ptrace environment. This check is NOT PASS.
- Headless direct fixture produced a real PNG, reopened and decoded with existing
  stb_image; exact RGBA roundtrip PASS. Manual visual inspection confirms expected
  overlapping rectangles and clipped green region.
- report.log records actual raster, CPU-loop and PNG encode durations. These are
  tiny fixture measurements, not evidence of superiority versus Chromium.
- CMake files inspected only: CMake unavailable. No full engine suite, Windows
  execution, HTML parse/layout, text, flexbox page or Chromium comparison run.

## Reproduction without CMake

From repo root, create an empty evidence directory and use:

```sh
common='-std=c++20 -Wall -Wextra -Werror -pedantic -pthread -Inative/include -Inative/src/rendering/preparation/include'
sources='native/src/rendering/native_frame.cpp native/src/rendering/preparation/paint_snapshot.cpp native/src/rendering/software/software_renderer.cpp'
c++ $common $sources native/tests/test_software_renderer.cpp -o /tmp/raster-test
/tmp/raster-test
c++ -O2 $common $sources tests/software_render_smoke/main.cpp -o /tmp/raster-smoke
/tmp/raster-smoke /path/to/empty-evidence-directory
```

The runner refuses to overwrite its image/report. Build with AETHER_BUILD_REV
string macro set to the current commit to record provenance. Pixel test assertions
must remain enabled; tests reject NDEBUG. CMake optional standalone entry point is
`tests/software_render_smoke`; it defines binaries but runs no workflows or tests
automatically. Artifacts are not committed. Implementations remain pending separate
code validation; design approval alone does not approve this code.
