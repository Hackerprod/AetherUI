# Isolated Linux raster validation (stage 1)

This is **not Chromium comparison or full AetherUI conformance**. No browser,
HTTP server, sockets, native host, DOM/layout engine, input or event loop is used.
The executable references only Core transitively through Linux. The managed
interop assembly is built transitively, but no native Aether runtime is required
or called. Production renderer files are unchanged.

## Run locally

Requires Linux glibc x64 little endian, .NET 10 SDK, distribution Cairo/Pango,
Fontconfig and an explicitly installed font family. Python 3 orchestrates logs;
Pillow is optional for the contact sheet. No dependencies are downloaded.

```sh
python bindings/dotnet/tests/AetherUI.Linux.VisualTests/test_runner.py
python bindings/dotnet/tests/AetherUI.Linux.VisualTests/run.py \
  --dotnet /absolute/path/to/dotnet \
  --font-family 'OpenAI Sans' \
  --output /absolute/new/directory/outside/checkout
```

The output directory must be new. The wrapper clears NuGet sources for offline
restore, uses the existing drawing CI's `AetherSkipNativeLibraryValidation=true`
flag, removes DISPLAY/WAYLAND_DISPLAY, and creates a writable Fontconfig cache
while preserving system configuration and fallback rules. The manifest records
all enumerated font file hashes and system Fontconfig file hashes. Identical
family names across machines are not evidence of identical font binaries or
fallback. The local SDK may differ from the workflow's 10.0.302.

Each child has a 180-second wall/CPU budget and a 256 MiB per-file limit. This is
not an RSS limit; the fixed catalog limits its largest frame to 786432 bytes.
Renderer production allocation/work limits still apply. Commands and full logs
stay local. Do not publish execution logs or paths without reviewing them.

## Evidence and outcomes

`captures/summary.json` records fixture results, command inventory, capture hashes,
contract tests and separate Linux/Chromium outcomes. `manifest.json` records the
commit/tree, dirty state, source hashes, SDK/native libraries, font configuration,
per-process exit codes, existing offscreen regression outcome, and artifact
hashes. If dirty, the source hashes describe the working candidate, not HEAD
alone. `manifest.linux` aggregates both the new raster suite and the existing
Linux regression; `linuxRaster` is the new suite alone. A failure outranks a block.

Every required fixture must pass, with three fresh captures at each of DPI 1/2.
Every capture uses a new presenter and renderer on its owning thread. Capture
requires Render to return and publish a *different* valid frame, before copying
or exporting. Negative tests cover a valid render followed by a failure, a
suspended reused viewport and a freshly suspended viewport. An exception or
no presentation cannot export the previous frame. Invalid captures produce no
PNG; a valid capture that fails analytic checks is retained as diagnostic data.

Raw files are tightly packed top-down premultiplied BGRA8. PNGs are straight RGBA:
`round(channel * 255 / alpha)` uses `(channel*255 + alpha/2)/alpha` integer
arithmetic; alpha zero produces RGB zero. PNG structure, CRC, zlib content and
conversion have synthetic tests. The optional contact sheet composites PNGs over
gray for inspection, and is not comparison input.

The comparator checks exact premultiplied pixels (no global percentage/SSIM
threshold). Distinct capture identities and non-aliased pixel buffers are
required. Missing/malformed captures or self-comparison are BLOCKED, differing
dimensions FAIL. Synthetic tests cover identical independent buffers, color,
displacement, channel swap, alpha, blank-white, missing/different dimensions,
self-comparison, result aggregation and visualization. Diff/overlay are emitted
when a repetition differs. A repetition proves determinism, not correctness or
an independent oracle. No automatically accepted goldens are stored.

Fixtures have analytic geometry/interior/compositing assertions and predeclared
exterior transparency masks. Solid interior channels are exact. Small explicit
rounding bounds apply only to fractional alpha/gradient/filter arithmetic;
shadow checks are monotonic/coverage tests rather than a claim of exact blur
kernel conformance. The catalog covers all 17 PaintCommand types, with scroll
provider values fixed as fixture data, never actual scroll events.

Text covers Spanish, combining marks, RTL, CJK, emoji and different sizes, plus
gradient/shadow paint. Bounds, ink coverage and colors are tested. These are
**smoke tests, not proof of shaping, glyph correctness, or absence of tofu**.
Family/fallback and raster behavior must be inspected alongside the contact
sheet. In particular minimum ink alone is not an oracle for every glyph.

## Deliberately unavailable

Chromium references are always BLOCKED in this stage, per fixture and aggregate.
There is no reference import/generation path and no attempt to work around prior
browser/socket/data-URL denials. The generic pixel comparator does not certify
Chromium provenance. A future comparison requires independently authored paired
fixtures and a permitted, verified reference path; approval of this stage does
not grant that permission. Linux PASS never becomes Chromium PASS.

No Windows/DirectWrite/WARP, GPU, windowing, X11/Wayland, IME, accessibility,
clipboard, VSync, performance, interaction, layout/parser or full application
validation is asserted. The existing Linux regression is executed separately.
