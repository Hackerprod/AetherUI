# R8 uniform circular rounded fills (experimental)

Proposal independently approved before implementation. Supports Fill with eight
exactly equal finite nonnegative radii, positive dimensions and 2r <= min(w,h).
Zero radius retains R6 analytic coverage. Unequal/elliptical/overlapping radii,
rounded clips and rounded strokes remain UNSUPPORTED. DPI=1, identity only.

Coverage intersects the pixel with original bounding rectangle and accumulated
rectangular clip, then samples 16x16 cell centers in that intersection. Coverage
is intersection area times the fraction inside the original rounded shape.
Corner centers NEVER derive from the clipped rectangle. Quantization and
premultiplied source-over happen once. This is approximate encoded-sRGB coverage,
not analytic curves, physically linear blending or Chromium equivalence.

All R6/R7 frame and cancellation guarantees remain. Rounded fill charges 256
units per bounding-box pixel, plus clear/other commands, under 50M. This limits
useful rounded area to roughly 195k pixels (less after other work); it is a
correctness-first bounded cut, NOT an optimized general renderer.

## Evidence and scope

- Fixed ten-case precision corpus in test_software_rounded.cpp written before
  first execution, radii 0/.25/.5/1/3/half dimension, fractional geometry and clips.
- Independent reference integrates exact vertical circle intervals with 4096
  and 8192 midpoint slices. Maximum convergence difference 4.19e-7; maximum
  output coverage error 0.007847 < predeclared 8/255 threshold.
- Initial separate assertion wrongly expected zero coverage for the first
  corner pixel; a radius-3 circle partially covers it. Changed that assertion
  to the truly exterior pixel. Corpus, algorithm and threshold not loosened.
- Strict new code, R6/R7 regression and R8 UBSan PASS.
- Real private emit_fill_rounded_rect -> real display-list ABI copy -> snapshot
  -> prepare -> raster integration PASS. No stub emitter. TU inclusion and ELF
  section GC isolate the existing emitter without linking the full engine.
  Legacy TU warnings (unused-function, Wextra enum conditional, strncpy
  truncation) suppressed only around that include; new code remains strict.
- Headless 320x200 fixture PNG reopened exactly; report has real durations.
- No full HTML/layout, flexbox, typography, Windows or Chromium execution.
  CMake NOT_RUN; ASan/LSan not rerun (R6 documented environment restriction).
- Independent implementation review pending. No Actions or push.

## Reproduce
Use common/sources from software-raster-r6.md:
```sh
c++ -O2 $common $sources native/tests/test_software_rounded.cpp -o /tmp/r8-test
/tmp/r8-test
c++ -O2 $common -ffunction-sections -fdata-sections $sources \
 native/src/abi/aether_abi.cpp native/src/rendering/preparation/aether_command_source.cpp \
 native/tests/test_software_rounded_emitter.cpp -Wl,--gc-sections -o /tmp/r8-emitter
/tmp/r8-emitter
```
Add -fsanitize=undefined -fno-sanitize-recover=undefined for UBSan.
The smoke runner exports aether-r8.png and report.log, refusing overwrites.
