# G2-R2: bounded native raster translations

Proposal independently accepted before implementation for the native command ABI
only. Implementation validation is a separate gate; this file is not a PASS.

Each local PushTransform must have exactly identity linear coefficients and
finite offsets bounded by ±65536. The raster accumulates local offsets in double
and saves/restores them in a separate stack. It deliberately does not use the
PreparedEvent float matrix product as translation truth: `32768 + 1/1024 - 32768`
must preserve the fraction. Every intermediate local sum and its combination
with prepared scroll is checked, including scopes with no visible geometry.

The total displacement is applied once to coverage/ramp rectangles, stroke,
shadow source/support, device-space clips at push, and glyph origin before A8
phase selection. Native border ownership/dash sampling subtracts that same
combined displacement. Transformed geometry, expanded stroke and glyph bounds
are checked before viewport clipping. Shadow source/support retains its existing
bounds checks. Clip state and opacity remain independent of transform restoration.
Allocation failure, cancellation, unsupported operations and bounds rejection do
not replace the confirmed frame. Existing work/memory budgets are retained.

## Scope boundaries

- Native raster ABI only. No HTML whitelist expansion is included. The existing
  HTML gate rejects authored translate(), including translate(0); this remains
  intentional until separately reviewed. No HTML translation end-to-end PASS.
- CSS transforms may already have been collapsed by the producer. An ABI matrix
  with identity linear terms is accepted regardless of which CSS syntax produced
  it. This does not promise support for arbitrary authored scale/rotation/skew.
- Synthetic immutable A8 masks only, with exact 26.6 origin grid and matching
  phase. No new real-font loading, shaping or raster provider is implemented.
- No general stacking/overflow/containing-block conformance claim.
- Chromium comparison NOT_RUN. Original Flexbox fixture status unchanged.

## Local validation entry point

From repository root: `sh tests/isolated_capture/run_g2_focal.sh OUTPUT_DIR`.
Tests use explicitly displaced reference geometry, not the implementation's
translation helper. The fractional cancellation fixture has a half-pixel edge
and also asserts its result differs from the zero-offset image, avoiding an
8-bit quantization false positive. Failure tests retain pixels and sequence.
OOM injection sweeps every observed allocation site in a nested transformed
opacity/gradient frame; cancellation sweeps every observed callback in it.
Independent validation is recorded separately by its reviewer.

Three existing tests previously expected all translations to reject. Their
unsupported-transform fixtures now use scale instead, preserving unsupported
matrix rejection and frame rollback checks. Translation behavior is tested here.
