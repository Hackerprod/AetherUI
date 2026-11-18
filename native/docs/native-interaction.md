# Bounded native interaction (ABI 0.38)

The opaque `aether_interaction_t` API is separate from the one-shot HTML profiles.
It retains a native document, immutable font backing and authoritative runtime
control values. The host supplies events and presents copied BGRA pixels; it does
not measure text, lay out controls, draw carets or implement focus traversal.
Profiles 1 and 2 retain their previous admission and limitations.

The bounded fixture contains real text/password inputs and a button. Supported
interaction is end-only text insertion and scalar backspace, Tab/Shift+Tab,
primary pointer focus/activation, Enter/Space activation and cancel/blur. There is
no authentication, form submission, networking, selection, IME, clipboard,
horizontal scrolling, general CSS focus/hover state, or accessibility integration.
The original unrestricted login is not claimed supported.

## Lifecycle and frames

Pass the exact `struct_size` and zero reserved fields. Creation copies HTML and
font bytes; the caller can free its source buffers afterwards. Session operations
are synchronous and owner-thread only. Destroying a null session succeeds.
Control queries return metadata, never values or credentials.

A successful state-changing event increments `state_version`. Rendering publishes
an independent confirmed frame and sequence. Rendering an already-current frame
is a no-op. An event failure leaves state unchanged; a render failure preserves
the confirmed old frame without reverting an accepted event. Compare frame and
state versions to detect staleness. Pointer events require current geometry.
Zero-size viewports suspend rendering and preserve the old frame until a later
successful render. Resize does not activate a control. Counters never wrap.

`copy_frame(NULL, 0)` queries metadata. An undersized non-null buffer returns
BUFFER_TOO_SMALL with metadata and does not write pixels. Successful copies do
not modify bytes beyond the frame. Output format is premultiplied BGRA8.

## Text and privacy

Runtime text is literal UTF-8, not HTML. The supported scalar repertoire and
per-control/document limits are enforced before committing a complete event.
Caret is fixed at the native advance, clipped to content. Empty inputs display a
placeholder but keep caret at the insertion origin. Long values clip without
scrolling. Password display uses one bullet per scalar; derived native runs never
receive runtime plaintext. The API has no secret getter, trace or persistent
activation queue. It does not promise secure memory erasure.

## Memory scope (G0-M1)

The 256 MiB ceiling covers instrumented storage, not total session memory or RSS.
Instrumented inputs, authoritative values, private metadata/control blocks,
raster scratch and frames share one owner-thread reservation domain. Allocation
reserves first, old/new storage overlap is charged and failed candidates release
reservations. Strong immutable source owners prevent per-event font-byte copies.

Existing DOM/style/layout, derived native presentations/preparation and legacy
font-cache storage are explicitly excluded. Existing admission and work limits
still apply. No new uncounted authoritative value cache is introduced. See
`interaction-native-backend.md`, `interaction-font-owned-storage.md`,
`interaction-owned-infrastructure.md`, and `interaction-raster-owned.md` for
allocation, ownership and release inventories.

## Reproduction

`native/tests/test_native_interaction.c` is a C11 consumer of only the public
header and production DSO. `native/examples/native_login_interactive` demonstrates
the bounded login and synthetic activation without claiming authentication.
Private fault-injection and stress tests are separate from this public proof.
Other toolchains/platforms remain NOT_VALIDATED until executed; no compiler or
standard-library exclusion is added. P4 is a separate unresolved gate.

## Fractional rectangular clipping

Only R3.1 opts into filtering a glyph's existing A8 coverage by the area of its
pixel's intersection with the effective rectangular clip. Nested clips intersect
geometrically before filtering. The filtered byte is rounded half-up, then uses
the existing alpha/premultiplied blend. This is filtering of aggregated A8, not
analytic vector-outline clipping. Layout and hit geometry are not snapped or
expanded. Existing x/baseline quantization to 26.6 remains unchanged. Integral
clips retain their old pixels and work accounting; one-shot profiles retain the
original clipping policy. The extra work charge is a logical budget, not a CPU
instruction or timing claim.
