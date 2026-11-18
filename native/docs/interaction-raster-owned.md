# Interaction raster storage and G0-M1 bridge

Private infrastructure only. This does not implement an event/session API or
claim the interactive gate is complete. The legacy public ABI is unchanged.

## One algorithm, different storage

`text/detail/raster_kernel.h` contains the existing TrueType A8 raster algorithm.
Legacy entry points instantiate it with the existing vector and immutable-mask
factories. `html_ltr/owned_raster.h` instantiates the same algorithm with owned
scratch/output and `allocate_shared` using the session's allocator domain. The
algorithm's work counters, reservations, scan/sort/integration and quantization
are shared. Unknown legacy exceptions still map to numeric failure.

`software/render_kernel.h` contains the existing software composition algorithm,
including all legacy command branches. Legacy wrappers preserve FrameStore and
its container types. The owned policy supplies native owned transports, actual
allocator-backed scratch, and an InteractionFrame adapter. There is no alternate
pixel renderer or host drawing callback.

The adapter first constructs a candidate. Only successful composition swaps it
into the supplied confirmed frame. Failed allocation, quota, cancellation or
validation leaves confirmed bytes and supplied metadata unchanged. Version and
sequence advancement/exhaustion remain the session caller's responsibility.

## Allocation inventory

- A8 edges, crossings, partial coverage and delta vectors: temporary owned
  allocator; all capacity growth is charged before allocation.
- A8 output bytes and mask/control block: glyph-category owned allocator; the
  final vector is moved without a legacy conversion or extra pixel copy.
- Software mask lookup map and font set: temporary allocator, including rebound
  nodes and per-node shared-reference storage.
- Draw, clip and transform vectors, layer-vector array, layer pixels, alphas and
  shadow scratch: temporary allocator. Nested vectors receive the same domain
  explicitly; an outer allocator alone is not assumed to cover inner storage.
- BGRA candidate and prior confirmed frame: InteractionFrame exact-capacity
  buffers in the same domain. Prior storage remains live/charged until swap and
  destruction; failure cannot invalidate it.
- Shared font/run/mask payloads are not charged again for each reference.

The renderer retains its legacy admission/work estimates as separate checks.
Those estimates are not presented as the actual owned-memory counter.

## Explicit G0-M1 policy

`render_legacy_accounted` uses the same composition kernel with a hybrid storage
policy. Its prepared frame, shaped runs, fonts, sources and A8 masks are existing
immutable legacy derived inputs, explicitly excluded under G0-M1. It neither
converts them to owned objects nor duplicates glyph/font bytes. New scratch,
reference-containing map/draw nodes and both BGRA frame buffers are counted.
This bridge must not be described as accounting for the excluded legacy graph.
For a shared-source alias, the legacy font's compatibility `bytes` member is
empty. Both software policies consult `FontInstance::byte_view()` for renderer
admission/work estimates, preserving the real retained font byte count without
copying it.

## Verification scope

The private test compares legacy/owned A8 bytes and every RasterWork field for
line, curve, composite, empty and bullet outlines at three phases. It compares
BGRA composition and work with BorderGroup, clip/chunk, opacity and transform
scopes, then compares the G0-M1 bridge against the same legacy output, including real shared-source aliases whose
byte addresses equal the original owned source. Fault
sweeps include both normal frame allocation and aligned/rebound allocator
allocations, quota failure, retry and confirmed-frame preservation.

Focused normal and ASan/UBSan runs use rebuilt affected TUs and other baseline
objects. They do not replace the coordinator's clean rebuild, public regression
matrix or 10,000 interactive edit/render/backspace/render cycles. LSan and other
platforms are not validated by these tests. No P4 algorithm or status is changed.

### R3.1 rectangular fractional clip policy (R2)

`render_interaction_fractional_rect_accounted` explicitly opts into
`FractionalRectA8Clip` in the same kernel. Legacy entry points default to
`IntegerA8Clip`, preserving their rejection and composition behavior. Clip
rectangles intersect geometrically without snapping. The opt-in A8 branch
filters the stored mask byte with `floor(mask * area(pixel ∩ effective_clip) +
0.5)` before the existing alpha multiplication/source-over. Nested clips do not
multiply coverage repeatedly. Fills/caret already integrate the intersected
rectangle and receive no additional factor. Curved A8 clips remain unsupported.

No allocation or persistent field is added by this policy. For a mask draw whose
effective bounds have fractional endpoints, logical work is charged at nine
units per visited pixel before composition, versus the existing one unit for
integer bounds. This is a bounded logical-work model, not a CPU instruction or
time claim. Existing limits, masks, lookup identities, allocator domains and M1
legacy-product exclusions remain unchanged. The existing finalizer quantizes
baseline26.6 with ties away from zero; this policy does not change it.
