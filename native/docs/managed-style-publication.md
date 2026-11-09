# Managed style publication: D1 partial implementation

This is an intermediate foundation, not completion of D1 or activation of the
incremental resolver. The host still calls the complete native prepare API. The
experimental state resolver remains OFF by default. No incremental ACK, context
owner optimization, hit index, or O(K) claim is included.

## Implemented boundary

- A pending state batch is extracted before callbacks; new writes enter a new
  batch. Failed batches are retained by linking preallocated owners, avoiding a
  fallible union during exception unwind.
- Host input/publication serials and per-category invalidation production/ACK
  serials protect acknowledgments. Pointer requests also have a monotonic serial.
- Document.InputVersion is a pure serial for attached tree/attribute/text,
  State, and RuntimeValue changes. Document.Version retains its tree semantics.
  Equal State/RuntimeValue assignments do not produce a mutation. Internal
  notifications only update host bookkeeping, never RenderRequested or user code.
- Known host writes acknowledge only their exact before/after InputVersion.
  They cannot acknowledge an earlier unknown mutation by copying a newer global
  serial. Unknown writes fail closed pending the separately reviewed DOM bridge.
- Native prepare, handle export/marshalling, managed style conversion, projected
  box assignments, legacy map construction/patching, diagnostic buffers and .NET
  transition arguments are staged before commit.
- Native publication tokens identify pinned revision/document/generation, input,
  structure and style-tree epochs, sheet and registry. A pure current read checks
  live input validity without callbacks. Host instance/document references and
  managed serials are checked separately.
- Only a still-current output can swap host styles/map and apply prepared box
  assignments. An outer R1 superseded by native R2 is discarded without overwriting
  R2. Recovery uses current native inputs, not an old state array from batch B.
- A superseded projection requires a full geometry/paint rebuild: native R3 may
  report zero change relative to R2 even though this host still represents R0.
- The style/replay guard queues reentrant input actions in FIFO order and prevents
  recursive render/resolve. Disposal invalidates the host immediately and delays
  native/resource destruction until the in-flight operation has unwound.
- Hit/cursor requests return miss/default while uncertified; cached pending hits
  cannot bypass native-current checks. No new frame is presented from a rejected
  candidate. The previously presented frame can remain visible.
- At most one immediate style recovery is allowed. A second supersession retains
  dirty state and reports failure. Allocation and notification errors are surfaced,
  retain pending work, and do not automatically retry. Explicit Render or a new
  input can start another bounded attempt.

## Native staging-only additions

Declared in aether_state_style.h, without changing existing ABI structs/exports:

- read_publication_token on runtime and prepared transaction;
- transaction preview_revision and preview_legacy_map;
- preview_legacy_patch, which never mutates its authentic legacy base;
- get_publication_style, which omits handle export only for a reusable payload and
  equivalent complete document measure context. Semantic metadata changes with
  zero visual impact still export/patch.

A contextual base is rejected for legacy patching. Existing exact-revision
contextual compatibility checks remain unchanged. Candidate previews never
constitute permission to use layout/hit-test before commit and certification.
Transaction inspection requires its runtime alive and the owner thread. Opaque
identity values are equality-only and must remain protected by retained owners.

The patch still enumerates the full candidate/base and managed dictionaries/boxes
can still be traversed. This is transactional preparation of the existing legacy
projection, not the D2 persistent/no-scan optimization.

## Diagnostics

CssDiagnostics captures during synchronous staging without Seen insertion, log
output, or listener invocation. Aborted staging consumes no dedup key. Post-publish
replay performs global triple (code, property, value) deduplication under the lock;
listeners run outside it. Replay attempts every buffered message even when an
earlier notification fails, then reports NotificationException with the first
failure. Already-notified external effects and Seen entries are never rolled back.
Concurrent diagnostic batches cannot emit the same triple twice.

## Blocking work before full D1 acceptance

1. **Managed DOM synchronization.** Unknown direct mutations are retained and
   rejected; they are not mirrored by reserializing old markup. Mixed managed input
   and a newer native R2 require explicit reconciliation rather than blindly
   replaying managed states. The serializer/rebind proposal was rejected; an
   in-place, identity-preserving, transactional bridge is a separate design cycle.
2. **Native transition transaction.** TransitionStyleMarshaler.Prepare only reserves
   .NET argument buffers. After valid publish/replay, Apply still calls native
   Observe, which can allocate and mutate active/finished transition state. If a
   later observation fails, the host prevents presentation and retains dirty work,
   but does NOT undo earlier observations. Atomic publication of style/map plus
   transition state is NOT implemented. It needs native prepared/clone state,
   transition serial checking (including tick/retire), and no-fail same-handle
   publication with retained ownership.
3. **Complete document replacement.** Existing component RenderMarkup/Load and
   handler-registry replacement are not a newly transactional DOM replacement.
   Their failure/ownership protocol belongs to the bridge design. The guard delays
   disposal, but does not claim rollback of those operations.
4. Derived layout, context preparation and display-list creation remain fallible
   after style publication. Their errors must not present a new frame; this work
   does not claim rollback of the whole renderer or external callbacks.

## Tests and runners

CTest includes shared preview/patch checks on supported platforms and Linux
real-singleton registry, operator-new OOM, and wrapped-malloc tests. The Linux
white-box tests compile the actual core sources into one test object library.

bindings/dotnet/tests/run-style-publication-tests.sh runs the portable publication
and existing input-trace suites, copying and comparing the exact native library
before execution. .github/workflows/style-publication.yml adds this default-OFF
Linux job; existing native CI picks up the CTest registrations.

Passing these focused tests is not a full-D1, Windows execution, macOS execution,
performance, or complete managed-suite PASS. The implementation report lists exact
runs and the four inherited Linux failures in the general managed suite.
