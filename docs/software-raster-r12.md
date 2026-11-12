# R12.1 rectangular exterior box shadows

Proposal and numerical correction independently approved before implementation.
The shared snapshot now copies/validates BoxShadow payloads; software consumes them.
The source-only Cairo adapter explicitly rejects the newly representable command,
so it cannot silently drop a shadow. No public C ABI changes.

## Contract

Only exterior shadows, zero corner radii, identity/DPI1 and rectangle clips.
Blur is zero or [1,64], offsets/spread within +/-8192, input dimensions <=8192,
box/source/support endpoints within +/-65536. Unsupported input rejects the frame
in preflight. A spread-collapsed source is empty. Multiple shadows retain emitter
order. Border-box interior is excluded even on transparent backgrounds.

Sigma=blur/2. Integrate the separable Gaussian field over each clipped pixel using
fixed GL16 nodes/weights; CDF interval evaluation uses erfc tail branches rather
than subtraction of large primitives. Subtract the integral of that SAME field on
the intersection with the original border-box. Quantize and compose only once.
Blur zero uses exact interval intersections. Positive-blur support truncates at
4 sigma: this is an explicit approximation, not Skia byte equivalence.

Preflight charges 256*(columns+rows)+2*pixels, plus clear/other draws, against 50M.
One scratch vector holds four axis arrays per current shadow, at most 256 KiB
aggregate; it is released before the next draw. Finiteness/range checks permit at
most 1e-12 rounding excursions; other numerical failures discard the candidate.
Cancellation polls axis preparation, rows and final commit. Existing 64MiB surface,
RAII discard and final atomic publication apply.

## Implementer evidence

- Fixed 12-case corpus before first evaluation, threshold 2/255 including infinite
  Gaussian tails. Independent long-double composite midpoint reference 256/512
  samples per axis: max error 0.00195897, convergence 5.44936e-7.
- Strict C++20 and UBSan pass. Zero/min/max blur, fractional clips/spread, offscreen
  source, collapsed source, unsupported cases, work limit and endpoint limits tested.
- All cooperative cancel poll boundaries and injected candidate allocation failure
  preserve the previous confirmed frame and sequence.
- Real no-text HTML -> style -> layout -> display list -> snapshot -> raster passes.
  Two box shadows with blurs7/8 arrive reversed as required by emitter order; reversing
  them again changes the pixels. Existing full-core object build reused: core source
  was unchanged. New preparation and renderer sources compiled fresh.
- R6–R11 and bridge regressions run separately. CMake/Windows/full flexbox/text/
  Chromium not run. Real PNG smoke is a direct primitive fixture, not HTML proof.

## Reproduce
Use common/sources from software-raster-r6.md to compile
`native/tests/test_software_shadow.cpp`. Add UBSan flags for sanitized run.
For emitter test compile `test_software_shadow_emitter.cpp`,
`preparation/aether_command_source.cpp` and core sources in native/CMakeLists.txt.
Smoke emits `aether-r12.png` plus `report.log`, refuses to overwrite evidence and
reopens PNG to verify exact RGBA. No workflows or automatic test execution added.
