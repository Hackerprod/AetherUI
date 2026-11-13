# S1-R0a: conservative state provenance, not a fast path

The runtime still computes and freezes the entire tree. No selector matching,
inline compilation, diagnostics, callbacks or revision epochs are skipped.
`HoverProvenance` is input evidence only, not cache eligibility.

## Mutation inventory (native/src audit)

* `dom/element_node.cpp`: attribute assignment pre-invalidates non-state evidence
  before any id-index/map/string mutation, including exceptions. Successful
  assignment retains the original mutation-epoch increment. State setter alone
  uses `touch_state`; synthetic descendants additionally invalidate non-state.
* `dom/node.cpp`: append reserves first, then invalidates source and destination
  before detach/attach. Remove invalidates before erasing. Reattachment cannot
  restore the old non-state certificate.
* `dom/html_parser.cpp`: replacement invalidates before old-tree destruction;
  all parser tag/text/attribute/structure writes and partial exceptions are covered.
  Input CR normalization precedes mutation and may fail without invalidation.
* `dom/document_node.cpp`: destruction retires retained identities. ID lookup's
  index reconstruction is derived data, not a style input.
* `style_runtime_core.cpp`: generated nodes are resolver output. Their publication
  already uses style-tree epochs and is excluded from real-input inventories.
* `layout/layout_flex.cpp`: anonymous borrowed-child wrappers are output, not
  reciprocal DOM inputs, and excluded. Positioned layout edits layout boxes only.

Internal direct writes to DOM fields are not a supported public API. Future input
mutators must use `touch()` (non-state by default), never `touch_state()`.

The saturating counter never wraps. Reaching UINT64_MAX permanently disables
provenance reuse for that root identity, even after subsequent full resolutions.
Old/new inventories retain identities and parent identities, compare all bits
(not caller changed-node lists), and reject non-hover differences and stale input.
Capture allocations occur in preparation. Commit swaps evidence before callback
replay, with no allocation, and failed preparation leaves old evidence intact.

## Reproduce

From repository root, `sh tests/native_hover_provenance/run.sh NEW_BUILD_DIR`
runs a clean build plus both provenance and atomic-revision regressions.
For individual steps, run `sh tests/native_hover_baseline/build.sh NEW_BUILD_DIR`.
Then compile `native/tests/test_hover_provenance.cpp` (includes runtime core) with
C++20/O1, `-Inative/include -Inative/src`, `NEW_BUILD_DIR/core/*.o`, `-pthread`.
The test injects bad_alloc at each preparation allocation, tests partial attribute
mutation, full-inventory/incomplete-list behavior, mixed state, removal/reattach,
parser replacement, callback reentrancy and counter saturation. Compare the three
baseline `hover-* --canonical` outputs against the prior revision's binaries.

Saturation limitation: the extra precommit non-state guard cannot distinguish two
non-state attempts after the counter is already exhausted. A partially failing
attribute mutation also leaves the legacy mutation epoch unchanged. This preserves
that pre-existing transaction limitation; it is not a reuse loophole, since
exhaustion rejects provenance reuse permanently. R0a does not claim to repair
all legacy transaction invalidation under counter exhaustion.
