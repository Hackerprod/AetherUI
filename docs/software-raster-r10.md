# R10 uniform rounded strokes

Design approved independently before implementation. Own CPU rasterizer only;
no Cairo/Pango/Skia or external code imported. This is a limited primitive, not
full HTML, typography, CSS border fidelity, or Chromium-equivalent rasterization.

For a nondegenerate center rect, uniform circular radius r>0 and width w,
outer geometry expands by w/2 with radius r+w/2; inner contracts by w/2 with
radius max(r-w/2,0). Collapsed inner has zero coverage. Each shape retains its
original centers and is clipped independently. Subtract unquantized coverage,
clamp, then quantize and source-over once. Inner radius zero is analytic.
Radius-zero strokes retain R7 exactly; width zero is a validated no-op.
Unequal/elliptical/overlapping radii and degenerate positive-width strokes reject
the complete frame. No attempt recovers radii already lost by the upstream
emitter's saturation at zero.

Preflight charges 4*N_outer + 256*(C_outer+C_inner) with checked accumulation.
C values use the R9 conservative corner bounds, excluding empty intersections.
Results expose separate inner/outer sampled-pixel and bound counters.
Existing clear charge, 64MiB per surface, 50M budget, cancellation and candidate
atomic commit remain unchanged.

## Verification at implementation
- Ten-case precision corpus fixed before first execution, explicit 16/255
  threshold. Independent vertical integration reference at 4096/8192 slices.
  Maximum error 0.0151439; reference convergence 3.62523e-7.
- Real aether_paint_border .badge-like border r3/w1 -> snapshot -> prepare ->
  raster PASS: center radius 2.5 produces exterior r3 and interior r2.
- Transparent/opaque backdrops, no double corner alpha, empty center,
  nested clips, fractional and offscreen geometry, collapsed inner, radius-zero
  inner, invalid/late unsupported commands, zero width, budget, allocation
  failure, and every cancellation boundary PASS.
- R6/R7/R8/R9 regressions, strict C++20 and independent UBSan build PASS.
  Legacy border_painter TU alone requires -Wno-unused-function, as in R7.
- Real 500x500 PNG reopened to exact RGBA and visually inspected.
- CMake, Windows, full HTML/flexbox, text and Chromium NOT_RUN.
  ASan/LSan not rerun (documented R6 environment restriction).
- Implementation awaits separate review; no Actions or push.

## Reproduce
Use common/sources variables from software-raster-r6.md, then:
```sh
c++ -O2 $common -ffunction-sections -fdata-sections -Wno-unused-function \
 -c native/src/rendering/border_painter.cpp -o /tmp/r10-border.o
c++ -O2 $common -ffunction-sections -fdata-sections $sources \
 native/src/css/border_radii.cpp native/tests/test_software_rounded_stroke.cpp \
 /tmp/r10-border.o -Wl,--gc-sections -o /tmp/r10-test
/tmp/r10-test
```
UBSan: add -fsanitize=undefined -fno-sanitize-recover=undefined to both.
Smoke runner outputs aether-r10.png and report.log (refuses overwrite).
