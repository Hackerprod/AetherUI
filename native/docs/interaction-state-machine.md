# Private interaction state machine

`html_ltr/interaction_state.h/.cpp` implements the private owner-thread state
transition layer of G0. It is not the public ABI or a native rendering session.
The coordinator owns frame freshness, frame version/sequence, hit testing and
pixel publication. The ABI layer must validate struct sizes and reserved fields
before passing a private event to this class.

## Inputs and private boundaries

Control seeds are in document order, IDs 1..N. Their kind, initial value and
placeholder originate from profile-2 admission, not an application setter.
The caller supplies the exact immutable scalar count: inventory scalar count
minus initial input-value scalar counts. Thus static text and all placeholders
remain counted, while current runtime values replace initial values. Button
labels remain static backend text; button state presentations are empty.

A native validator receives metadata and presented strings only. A password
presentation contains one generated U+2022 per scalar, or its empty-value
placeholder. It never receives the stored password. No public or private
plaintext-value getter is provided. A validation view is borrowed only for that
call and must not be retained. The accepted candidate_view is similarly borrowed
until the next successful transition.

The validator is an internal native dependency, not an application callback.
It must validate all presentations without external side effects, then either
return successfully or throw a typed Error/bad_alloc before state publication.

PointerContext comes only from the coordinator's confirmed native layout.
Public events cannot select a control ID. Pointer down/up require a current frame
and positive viewport even for otherwise-no-op gestures. Out-of-viewport finite
coordinates are valid and force an exterior hit. Key/text/cancel/blur work while
suspended. The class does not silently render or reuse a stale layout.

## Atomicity and storage

Definitions/placeholders, runtime values, scalar-count vector, authoritative
presentations and their vector buffers use the common OwnedAllocator and the
values category of one shared domain. Strings' actual capacities and vector
allocations are charged before growth. The enclosing machine object itself must
be included in its owning session allocation; it does not allocate itself.

A state transition first checks event shape, payload and quotas and computes a
POD transition. Effective no-ops return without allocating a candidate or calling
the native validator. For a change, exhaustion is checked before copying the
single candidate. Confirmed and candidate allocations coexist under the same
budget. Presentation generation and native validation occur before a no-throw
swap. Errors release the candidate and leave focus, pressure/arming, values,
version and activation unchanged. An activation exists only in the returned
successful result; there is no queue or persistent activation counter.

This layer is fully instrumented. It does not imply that legacy DOM/style/layout
or complete session memory is included in the G0-M1 accounted-storage budget.
Password values can remain in authored input bytes and allocator memory; no
secure-erasure claim is made.

## Validation order

A TEXT payload exceeding 1024 bytes returns LIMIT_EXCEEDED before scanning, to
bound work. For a payload within that byte limit, the complete UTF-8 stream is
validated before scalar/repertoire decisions and before focus is consulted.
Malformed UTF-8 or embedded NUL returns INVALID_ARGUMENT; valid scalars outside
the shared repertoire return UNSUPPORTED. Scalar count >256 and effective
aggregate count >4096 return LIMIT_EXCEEDED. Data is never reparsed as HTML.

## Verification scope

The private test covers every table transition and 40 reachable metadata states
across 800 event edges, all focus kinds, pointer/Space/Enter overlap, cancellation,
repetition, scalar editing, literal markup, passwords, aggregate quota recovery,
invalid shapes/UTF-8, stale-frame rejection, suspension/resize and state-version
exhaustion. Allocation fault injection verifies rejected edits and activation
release events preserve metadata and charged storage, and retries activate once.

A 10,000-cycle state-only test varies admitted scalars at the 256-scalar boundary
in both inputs, interleaves focus, activation and resize, and checks exact version,
100 expected activations and stable charged storage after warming. This does NOT
substitute for G0's 10,000 integrated edit/render/backspace/render cycles. No
native caret, pixels, glyphs, frame failures, sequence overflow or public C ABI
are exercised by this test; those remain coordinator integration tests.
