# Native surface ABI 0.35.0

`aether_surface_*` provides synchronous rasterization of the **resolved public
 display list**, not complete HTML/CSS rendering. It uses native snapshot,
preparation and software rendering, not a host-supplied command adapter. Public
lists have already lost some native metadata, including CSS opacity grouping;
this API cannot retrospectively detect all such omissions. Color alpha is not
proof of overlapping CSS opacity groups. Text, images, unsupported scroll and
other unsupported commands fail rather than falling back. Gradient budgets
are per-pixel interior 1 / border 2048 (cycle 03); large gradients render
in the surface route, and large-gradient P4 is PASS there. B0 still does
not admit gradients. Box-shadow budgets are `(columns+rows)·256 + 2/px`
(cycle 04); exterior shadows render in the surface route; `inset` is
rejected. B0 still does not admit box-shadow. Native text,
resources and a complete real page remain R2 work.

## Ownership and states

Create initializes its nonnull output to NULL before work. Destroy(NULL) succeeds.
All other surface calls reject NULL. Every live handle belongs to its creating
thread; all operations including destruction reject another thread without
changing the handle or confirmed frame. No overlapping calls, reentry, public
callbacks, cancellation or workers. Keep input lists alive and immutable during
render; nothing is retained after it. Arbitrary/stale handles, double destroy and
invalid C pointers are caller errors, not universally detectable conditions.

Physical dimensions are integer 0..8192 with width*height*4 at most 64 MiB. DPI
is 1. Negative, over-axis and over-product sizes fail INVALID_ARGUMENT. Zero on
either axis suspends rendering. Create/resize do not allocate pixels or publish.
Resize (including same size) changes the target viewport and discards candidates,
but preserves the confirmed frame with its original dimensions and sequence.
Invalid resize preserves both viewport and frame. Render while suspended returns
SUSPENDED. A successful render commits exactly once, starting at sequence 1 and
incrementing even for identical pixels. Failures preserve confirmed bytes and
sequence; token/sequence exhaustion returns EXHAUSTED without wrapping.

## Copy

Initialize `info.struct_size = sizeof(info)`; exact size is required. Query with
NULL destination and zero capacity. NULL with nonzero capacity is invalid.
Before the first successful render, copy returns NO_FRAME. Format is top-down
BGRA8 premultiplied, stride width*4, without row padding. Info describes confirmed
pixels, not a newly resized viewport. An undersized nonnull buffer receives no
pixels but info receives the required metadata (BUFFER_TOO_SMALL). A sufficient
buffer receives exactly byte_count bytes, leaving its tail untouched. Other
errors leave info unchanged. Padding bytes of the C struct are unspecified;
fields are assigned individually. Info and pixel storage must not overlap.

Copy uses a temporary owning copy even for metadata queries and can return
OUT_OF_MEMORY. The 64 MiB constraint is per-frame payload, **not total process
memory**: confirmed, candidate, temporary copy and snapshot/preparation storage
can coexist. No second persistent pixel store or zero-copy pointer is exposed.

## Errors and build

New stable int32 statuses are INVALID_ARGUMENT=-6, WRONG_THREAD=-7, NO_FRAME=-8,
BUFFER_TOO_SMALL=-9, UNSUPPORTED=-10, LIMIT_EXCEEDED=-11, SUSPENDED=-12,
EXHAUSTED=-13 and INTERNAL=-14. NULL_ARG=-1, INVALID_STATE=-2 and OUT_OF_MEMORY=-3
remain unchanged. Status messages are static and do not contain document data.
Unexpected internal cancellation maps to INTERNAL, not a public cancellation
contract. ABI minor is distinct from the CMake/package version.

See `native/examples/native_surface/README.md` for the external C11 consumer and
`tests/native_surface/` for the reproducible shared-library build and test recipes.
The production DSO exports no fault-injection hooks. Private failure tests compile
an instrumented wrapper separately with authentic core objects. They are not
proof that a consumer linked those private objects: the public consumer links only
the production shared library.
