# R11 uniform rounded clips

Proposal independently approved before implementation. One curved clip may be
active; arbitrary rectangular clips can surround it. Each retained Draw snapshots
the clip geometry; stack pop restores both rectangular and curved state. A second
simultaneous curved clip rejects the whole frame during preflight.

Per pixel B intersects draw exterior, clip bounding boxes and viewport. When B
lies wholly in a central clip band, preserve the exact R10 coverage path. Otherwise
sample draw and clip jointly on B's 16x16 centers: outer AND NOT inner AND clip.
Quantize and compose once. Original geometries determine centers, never clipped
bounding boxes. Identical rounded fill/clip retains exactly the fill's pixels.

Preflight adds 2N + 768C_clip to the existing R10 budget. Corner bounds intersect
clip geometry with each clipped draw; empty intersections are excluded. Successful
results expose joint_sampled_pixels and clip_corner_pixel_bound, with tests proving
the former does not exceed the latter. Existing 50M work and 64MiB-per-surface
limits, cancellation, discard guard and final atomic commit remain unchanged.

## Implementer verification
- Ten-case corpus fixed before first run. Independent vertical slice integration
  reference at 4096/8192 slices; threshold fixed 16/255. Maximum pixel error
  0.00917593; reference convergence 4.18652e-7.
- Strict C++20 and UBSan pass. Includes identical clip/fill bytes, fills/strokes,
  fractional/nested rectangle clips, offscreen, stack restoration, unsupported
  second curve/invalid radii, limits, every cancellation boundary and allocation
  failure preserving the confirmed frame.
- Real no-text HTML document parsed, styles resolved, layout and display-list
  built by the core; overflow:hidden/border-radius:3px emits curved clip and a
  colored child is rasterized successfully. Explicit display:block in test CSS;
  test does not depend on a UA stylesheet being loaded. Core sources compiled
  manually from the CMake source list with C++20, not via CMake.
- Initial test wrongly expected an r3 corner pixel to have precisely zero area;
  changed that single semantic assertion to partial alpha. Corpus and tolerance
  unchanged. Legacy style setup without explicit block declarations emitted no
  child boxes; fixture corrected rather than altering production layout code.
- Full R6–R10 regressions pass. PNG real generated/reopened to exact RGBA and
  visually inspected. Smoke is a direct primitive fixture, not full HTML.
- Full flexbox, text, Windows, CMake and Chromium remain NOT_RUN/unsupported.
  No Actions and no push; awaits independent implementation review.

## Reproduce
Use common/sources variables in software-raster-r6.md. Compile
native/tests/test_software_curved_clip.cpp with these sources; add UBSan flags for
sanitizer run. For the full emitter test compile native/tests/test_software_curved_clip_emitter.cpp,
preparation/aether_command_source.cpp, and all aether_core sources listed in
native/CMakeLists.txt. This uses no graphics SDK. The smoke runner emits
aether-r11.png and report.log and refuses to overwrite either.
