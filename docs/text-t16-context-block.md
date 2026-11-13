# T1.6 prerequisite audit: still closed

This is **not** the T1.6 grouped native-layout implementation. The approved
implementation requires layout-authorized, retained context provenance; it
explicitly forbids inventing evidence. That prerequisite is unavailable on
base `0baede2` (published main `b4209cc`, identical tree).

## Evidence

- `rendering/text/shaped_run.h`: FontInstance owns bytes, hash, face index,
  size and raster flags. Its constructor checks size, but does not parse a face
  or prove family, weight, style, features, language or font coverage. Arbitrary
  bytes and a nonexistent face index are valid transport values.
- `rendering/text/measurement_provider.h`: selection carries only provider and
  FontInstance; provider receives source, font and width. There is no retained
  resolved-context identity or proof that CSS selected this face.
- `rendering/text/context_contract.h`: complete is a caller-written boolean;
  language/features/family are caller strings. Equality is not provenance.
- `style_runtime/builtin_properties.cpp`: direction and writing-mode have
  inherited retained sidecars; family, weight/style, size and white-space are
  represented. This is useful partial evidence, not resolution of language,
  shaping features or a concrete face corresponding to CSS.
- `layout/layout_types.h`: native selection rejects before calling a provider.
  Keeping that guard is necessary. Neither explicit nowrap nor ASCII supplies
  the missing font/context proof.

## Work deliberately not done

No public gate or ABI changed. No new path pretends to have one-run intrinsics,
normalization mapping or final placement. No synthetic success is reported as
real shaping. Adding a second mock-only grouping helper would not satisfy the
required layout integration. No SDK installation was attempted.

Next dependency: implement/review a retained layout-authorized font/context
binding with provenance (including the explicit synthetic-test authority, if
that is intended), resolution and invalidation semantics. Only then can the
approved sequence plan replace both intrinsic and final per-node measurement.
A test fixture cannot self-certify ordinary CSS as eligible.

## Reproducible negative regression

`sh tests/isolated_capture/run_t16_context_block.sh BUILD_DIR` verifies:
- AV, To and ffi with one text node versus comment-separated nodes;
- spaces, tab, br, separate span owners and weight mismatch;
- explicit nowrap/LTR/horizontal CSS and widths 1 and 400;
- zero provider calls on all cases, preserved prior legacy output when rejected,
  and released layout transaction state.

All-space input may need no text measurement at all, so successful empty layout
with zero calls is permitted. This is not admission to native shaping.

These tests **do not** measure kerning/ligatures, source maps, one placement,
normalization equivalence, native OOM/cancellation or native run lifetime.
Those positive T1.6 requirements remain NOT_IMPLEMENTED. Real shaping,
HTML PNG and Chromium: NOT_RUN; their timings: NOT_MEASURED.

Executed on the unchanged native source tree: clean manual build succeeded;
prerequisite regression 286 checks with zero provider calls; T1.4 provider
32 checks; T1.5 context preparation 27 checks. Existing enum/non-enum warning
in measure_state.h remains. Focused suites only, not the full repository suite.
Original fixture SHA256 remains
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
