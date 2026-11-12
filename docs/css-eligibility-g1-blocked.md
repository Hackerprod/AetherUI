# Eligibility checkpoints: G1.1 BLOCKED; G1.2 standalone fail-closed barrier

Inspected base: `d7993e641903b62e7abadb88aa294727ef9ff43f`.
The historical G1.1 checkpoint (`0d022b3`) contained documentation only.
The subsequent G1.2 checkpoint (`f31dbec`) adds the standalone private barrier
described below. Neither implements a certificate or permits authored-CSS
validation; existing rendering entrypoints remain unchanged.

The sections through "Historical G1.1 verification" record the G1.1 assessment
at the inspected base, not a claim that the current branch contains no code changes.

## Approved contract

G1.1 is a narrow eligibility policy for direction, writing mode and positioning
inputs, not certification of HTML/CSS fidelity or acceptance of the complete
flexbox page. Unknown provenance must remain Unknown. A positive certificate
requires an internally complete source inventory, inspection before discarded
properties, immutable evidence tied to the precise layout revision, and runtime,
document, sheet/source/environment freshness.

The independent validator added a prerequisite: exclusive effective ownership
or synchronization of all relevant mutation must exist BEFORE reading inputs.
Checking epochs afterwards cannot make an unsynchronized read safe.

## Implementation blocker verified in source

The current ownership machinery cannot supply that prerequisite for arbitrary
existing public handles:

- `style_runtime_core.cpp`: `aether_style_runtime_clear_cache` mutates state and
  `sheet_epoch` without the prepare/owner checks used by sheet addition. Runtime
  destruction deletes the public handle directly. Merely adding a guard to the
  new certificate function would not coordinate these operations.
- `dom/element_node.cpp`: attribute setters mutate the attributes map and touch
  the document epoch. `dom/node.cpp` appends/removes children directly. These do
  not participate in a common ownership lease or lock.
- `dom/node_identity.h`: `alive` and document epochs are ordinary fields, not a
  synchronization protocol. Making the epochs atomic alone would not protect
  the maps, vectors, object lifetime or source bytes.
- `css/style_map_version.cpp`: `inputs_current` permits absent/legacy versions
  and compares DOM epochs only. It is not a G1 eligibility admission check.
- `css/style_revision.h`: the revision does not yet own a complete source
  manifest or a runtime freshness/lifetime token.
- `style_runtime_types.h`: a compiled sheet's `source_name` is not evidence of
  linkage to a specific DOM style/link source. Unknown properties are discarded
  in `style_source_compiler.cpp` before such evidence is preserved.

These are contract gaps for this NEW gate, not a claim that ordinary documented
single-threaded use of the existing renderer is broken. No concurrent-race test
was executed: intentionally racing these raw mutable objects would be undefined
behavior and cannot prove safe ownership.

## Why the historical G1.1 checkpoint added no partial runtime gate

An owner-thread equality check, caller-provided boolean, mutex used only by the
new function, or post-read epoch comparison would not establish the required
ownership. An enum that always returns Unknown could be safe but would not
implement G1.1 or satisfy its positive integration test. The historical G1.1
checkpoint implemented neither approach and made no implementation PASS claim.
G1.2 later adopted only the separately approved non-certifying barrier below.

## Required design decision before implementation

Choose and independently validate one complete ownership boundary:

1. A private capture session owns fresh DOM/runtime/style sources from immutable
   owned input bytes and never exports mutable handles or callbacks capable of
   changing them while inspection/layout/capture run. It must still provide a
   complete internally generated source inventory, reject unresolved style/link/
   import inputs, bind the exact layout revision, and distinguish historical
   snapshots from current external state.
2. A coordinated mutation/lifetime protocol covering EVERY public and internal
   DOM/runtime/source/environment mutation and destruction path. The protocol
   must specify lock/lease ownership, lifetime acquisition, callback reentrancy,
   ordering and error behavior without silently changing the public ABI.

Both are substantive design changes, not an implementation detail to invent in
this task. The parser evidence and epochs can be added after that boundary is
approved. In its absence, authored eligibility remains unavailable and R15's
ResolvedGeometryOnly restriction must remain intact.

## Historical G1.1 verification

At `0d022b3`: read-only source inspection and `git diff --check`. That commit
changed documentation only, not runtime code. No new dependencies were installed,
no Actions were executed, and no GitHub publication occurred.
No certificate, text backend, full flexbox, Windows or Chromium validation.

## G1.2 implementation: non-certifying admission barrier (`f31dbec`)

A separate, deliberately non-certifying private API now returns `Unknown /
ExclusiveOwnershipUnavailable` before touching any supplied handle. It has no
Eligible path, caller-supplied ownership flag or public result constructor.
It performs no DOM/runtime reads, callbacks, epoch checks, allocations or writes.

The result is immutable. The barrier's dedicated test exercises 64 combinations
of null, live and inaccessible sentinel values, plus compile-time checks of its
result construction restrictions. Strict C++20 and UBSan both pass. These tests
prove only the behavior of this small barrier, NOT general concurrency safety.
No concurrent race is intentionally executed.

Reproduce: `bash tests/css_eligibility/run_g12.sh`.

This API is currently a standalone private admission component. It does not add
an authored-CSS acceptance route to existing rendering entrypoints and it does
not integrate with the concurrent R15 work. That integration must preserve
R15's refusal to certify authored input. No change to public ABI or legacy
rendering occurs. G1.1 remains BLOCKED; G1.2 does not satisfy its positive test.
