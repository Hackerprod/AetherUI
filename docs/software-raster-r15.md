# R15.2: frozen scroll geometry, not authored CSS acceptance

This private C++ path captures Aether's resolved physical geometry only.
It does **not** certify authored CSS, flexbox, RTL, vertical writing, sticky,
or equivalence to Chromium. The core currently does not preserve all those
authored semantics; silently ignored properties cannot be inferred from pixels.

Use `snapshot_native_geometry(..., ValidationDomain::ResolvedGeometryOnly)`.
Requesting `AuthoredCss` (or an unknown domain) fails with
`UnsupportedValidationDomain`. `GeometryValidationReport` supplies immutable
`authored_css_validation=NOT_RUN` and `flexbox_semantic_acceptance=false`.
The compatibility opacity-capture entry point remains geometry-only.
A future authored-CSS admission phase needs an independent proposal.

## Frozen input and lifetime

The private builder overload accepts an explicit table of container IDs and
physical nonnegative offsets. Its IDs must exactly equal the union of native
PUSH_SCROLL_OFFSET and DRAW_SCROLLBAR_CHROME commands, including chrome-only
containers at zero. Missing, extra, duplicate, nonfinite, negative, or
out-of-range input fails without replacing the caller's previous owned list.
The old overload explicitly creates zero offsets; it does not query live scroll
callbacks. The public ABI and legacy builders remain unchanged. Legacy snapshot
paths reject scroll commands without native metadata.

Offsets, scroll ranges and resolved native chrome are copied before returning.
The same frozen offset drives content and native thumb geometry. Neither capture
nor replay retains DOM, layout, styles, callbacks or borrowed sidecars.
The native float scrollbar resolver requires exactly float-representable
offset input; all accumulation and raster translations use double.

## Geometry and scope

A container's background, border and own chrome stay fixed relative to its parent.
Its descendants subtract its frozen offset; nested containers accumulate offsets.
Clips freeze at their push position. Clip/transform/chunk/opacity/scroll scopes
cannot cross; opacity compositing never translates already translated pixels.
Rounded fill, stroke, grouped border sampling and shadow geometry share translation.
Chrome uses native resolver thumb geometry with radius half the shorter dimension,
gray alpha .55 and corner alpha .28. Zero-size geometry is omitted.

Full-root scrolling is rejected. Isolated root/body/document capture remains
rejected; isolated descendants omit ancestor scroll offsets and ancestor clips.
Computed fixed descendants inside scroll are rejected. Transform commands remain
unsupported by the software renderer. This is not a claim to reject authored
sticky/RTL/vertical declarations that the core did not retain.

Input commands and prepared output are limited to 65,536, combined structural
scope depth to 256 (opacity remains 8), offsets/ranges and accumulated translations
to 65,536 physical units. Geometry endpoints inside scroll scopes and native chrome are also bounded.
Unsupported payloads and cancellation preserve the previously confirmed frame.

## Manual evidence

Run `sh tests/software_render_smoke/run_r15.sh OUTPUT_DIRECTORY`.
No workflow or Actions is installed or invoked. Assertions stay enabled.
Pure tests cover fractional/nested offsets, restoration, clipping, opacity,
border/stroke translation equivalence, malformed scopes and atomic cancellation.
Real HTML fixtures cover full and isolated subtree, simple/nested/chrome-only
scroll, max/fractional offsets, exact metadata, native thumb motion, root/fixed
rejection and rendering after source destruction. The report explicitly prevents
interpreting these geometry checks as a semantic flexbox PASS.
