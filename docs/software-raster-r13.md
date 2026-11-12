# R13.1 own CPU rectangular side borders

Scope: solid/dashed per-side borders, zero radii, identity transform,
rectangular clip, positive inner rectangle contained in outer rectangle.
No text, curved border groups, non-solid/dashed styles, curved clips or full
flexbox-page claim. Unsupported commands reject the frame atomically.

The display list stores private `{first,count}` ranges around each real border
emitter invocation, in both whole-tree and node builders. Native-only capture
owns one `BorderGroup` per range; it checks bounds, non-overlap, cardinality,
unique sides and common geometry. Scope commands inside ranges are invalid.
External C lists without this metadata remain unsupported; ABI is unchanged.

Four outer-to-inner corner trapezoids partition the ring. Top/right/bottom/left
is the tie priority. A common 16x16 sample grid resolves one owner per sample;
premultiplied contributions are accumulated and source-over composited once,
preventing repeated alpha composition along corners. Full interior pixels skip
sampling. Coordinates bounded to +/-65536; work charges one loop per candidate
pixel plus 1024 conservative membership checks per non-interior pixel.

Dashed uses side outer length L and positive side thickness t, 1/64 <= t <=8192.
Reject q=L/(6t)>4096 before computing n=max(1,floor(q+0.5)); P=L/n.
Paint if abs(s/P-floor(s/P+0.5))<0.25. No enumeration of dashes.
Zero-thickness sides own no samples. All checks precede framebuffer publication.

Tests: `native/tests/test_software_border_group.cpp` fixes an 8-case fractional
and alpha corpus, independently partitions by normalized side distance and
compares against 128x128 and 256x256 references. Tolerance is 16/255 per
premultiplied channel, fixed before execution. Additional tests cover rejection,
limits, cancellation, allocation failure and preservation of prior frame.
`test_software_border_emitter.cpp` parses real HTML/CSS, builds through both APIs,
validates malformed metadata and non-fusion, then destroys all source objects
before preparing and rendering the snapshot. Optional output directory exports
PNG and report. This is a small no-text fixture, NOT the full flexbox fixture.

CMake is unavailable in this environment. Direct C++20 compiler validation and
full-core compilation are used; no new dependencies or workflows are enabled.

Manual reproduction: `sh tests/software_render_smoke/run_r13.sh /tmp/r13-new`.
The integration build retains a pre-existing `measure_state.h:175` enum warning;
only this build uses `-Wno-error=extra`. Pure raster/reference builds are strict.

Implementation evidence (2026-11-12, pending independent review):
- Numeric maximum error 0.0156901; independent reference convergence 0.000976562.
- Pure strict build and UBSan: PASS. Full core rebuilt (71 translation units).
- Both HTML builders, malformed metadata, separate equal DOM groups, owned
  snapshot lifetime: PASS. R6–R12 smoke/preparation regressions: PASS.
- Real HTML 40x40 PNG generated, CRC and zlib decoded, visually inspected.
- Fixture mean render 2.02737 ms over 12 runs, process CPU 24.624 ms,
  framebuffer 6400 bytes. Not a Chrome comparison or full-page benchmark.
- CMake, Windows, Chromium baseline and full flexbox page not validated.
