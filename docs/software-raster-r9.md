# R9 central-band fast path for uniform rounded fills

Proposal independently approved before implementation. Same supported inputs
as R8; no new CSS features. Pixels wholly inside either original shape's central
band use exact clipped area. All other pixels retain the frozen R8 grid16x16
algorithm, original corner centers, quantization and blending.

## Budget and counters

Preflight intersects each original corner square with the already clipped draw
bounds. Empty intersections contribute zero before floor/ceil conversion.
C is min(N, sum of the four conservative pixel counts); overlap overcounts safely.
Charge 2N + 256C per rounded fill, plus clear and other commands, within 50M.
Checked remaining-budget division precedes addition/multiplication. Each corner
count is viewport bounded (8192 per axis), so the four-term sum fits uint64.
The budget estimates work, not elapsed time. Admission near its threshold changes.

Result exposes successful-frame corner_pixel_bound and sampled_pixels. The
latter counts pixels sent to 256-point membership sampling, not total raster
pixels. These counters allow tests to verify actual sampled pixels <= bound.
All prior atomic publication, cancellation and allocation guarantees remain.

## Evidence (implementation, independent review still pending)

- Frozen R8 source in native/tests/r8_reference.cpp is test-only, never linked
  into the production raster library.
- 1556 deterministic differential cases fixed before execution (1500 generated,
  56 exact-band cases) produce identical bytes. Includes tiny/max radii,
  fractional/offscreen shapes, empty and fractional clips, and semitransparency.
- Total sampled pixels 14684 <= bound 14863. A 500x500 radius3 fill now passes
  with budget 759216; old R8 correctly rejects by its older budget.
- 97 repeated 500x500 fills accepted, 98 rejected, counting clear. Failure
  preserves prior frame. Cancellation, R6/R7/R8, real emitter integration and
  UBSan pass. Existing R8 precision corpus unchanged except the newly admitted
  500x500 expectation.
- Same-process alternating-order A/B, -O2, 21 warm pairs on one 320x200 fill:
  R8 median 19.98 ms / P95 25.87 ms; R9 median 0.85 ms / P95 1.12 ms.
  Membership tests fall from 16,384,000 to 16,384 for this case. Shared-host
  measurements vary. This is NOT full HTML, real-world UI, or Chromium speedup.
- 500x500 PNG produced and reopened exact. Large dark background radius3,
  smaller rounded fills, rectangular strokes and clipped fill are real output.
- Windows, CMake, full HTML/flexbox, text, Chromium and ASan/LSan NOT_RUN.
  No Actions/push. Parent handles publication after independent code review.

## Reproduce

Use common/sources from software-raster-r6.md:
```sh
c++ -O2 $common $sources native/tests/r8_reference.cpp \
 native/tests/test_software_bands.cpp -o /tmp/r9-test
/tmp/r9-test
```
Add -fsanitize=undefined -fno-sanitize-recover=undefined for UBSan.
The standalone CMake target is aether_software_bands_test (not executed here).
The smoke runner now emits aether-r9.png and report.log at 500x500.
