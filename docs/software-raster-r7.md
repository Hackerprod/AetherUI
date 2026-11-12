# R7.1 own CPU rectangular stroke (experimental)

Proposal R7.1 independently approved before implementation. Adds solid rectangular
Stroke to the R6 renderer, with zero radii, identity transform and DPI=1. No new
graphics SDK. Existing Cairo/Pango, Windows backend and border emitter unchanged.

Centerline rectangle expands/contracts by half stroke width in double. Inner
collapse means empty interior. Coverage is area(outer intersect clip intersect
pixel) minus area(inner intersect clip intersect pixel), clamped [0,1], quantized
once then composed once using R6 source-over. This avoids double-blended corners.
Width zero is validated no-op. Positive width with zero rectangle dimension is
UNSUPPORTED in preflight: the CSS emitter may produce such a command for a visible
border; original geometry must not be invented or silently lost.

R6 frame lifetime, transparent clear, cancellation and limits remain unchanged.
Stroke charges two clipped bounding-box pixel evaluations, including when inner
is empty; clear costs width*height. Checked remaining-budget division precedes
multiplication; conversion to integers only after viewport clip.

## Verification
- Strict new C++20 code and test compilation PASS; R6 regression PASS.
- Real border emitter integration PASS: solid box exterior, equal/excessive
  thickness in either dimension rejected with prior frame intact.
- Fractional/whole strokes, clipped anulus, transparent corners, opaque backdrop,
  collapsed interior, width zero, invalid data, huge finite geometry, budget,
  cancellation boundaries and allocation failure PASS.
- UBSan integration PASS. Existing border_painter.cpp has two unused static
  functions, so only that translation unit used -Wno-unused-function; new code
  retained -Wall -Wextra -Werror -pedantic. Section garbage collection links only
  exercised emitter functions, without replacing them with stubs.
- Real 320x200 PNG generated and reopened exact RGBA match. Measurements only for
  direct fixture, not HTML, flexbox, Chromium or Windows. CMake NOT_RUN.
- ASan/LSan not rerun following documented environment restriction in R6.
- Independent code review pending. No GitHub Actions or push.

## Reproduce
Use R6 common/sources variables (docs/software-raster-r6.md). Then:
```sh
c++ $common -ffunction-sections -fdata-sections -Wno-unused-function \
 -c native/src/rendering/border_painter.cpp -o /tmp/r7-border.o
c++ $common -ffunction-sections -fdata-sections $sources \
 native/src/css/border_radii.cpp native/tests/test_software_stroke.cpp \
 /tmp/r7-border.o -Wl,--gc-sections -o /tmp/r7-stroke
/tmp/r7-stroke
```
Add -fsanitize=undefined -fno-sanitize-recover=undefined to both compile commands
for UBSan. Smoke runner now exports aether-r7.png and report.log. No claim is made
that the small emitter fixture validates the whole HTML document or CSS borders.
