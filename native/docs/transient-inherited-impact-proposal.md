# Proposal: canonical runtime storage for transient inherited impact

Status: awaiting focused design review; not implemented.

The existing `PreparedNodeResult::inherited_change_impact` is the propagation of
visual impact within one FULL traversal. `compute_node` receives the current
parent result, combines it with current declaration/symbolic changes and passes
it to `prepare_tree`/`prepare_generated_pseudo`. It is not a property dependency
mask and it is not an input consumed from an old payload.

The current FULL freeze also stores this temporary output in
`NodePayloadInput::inherited_change_impact`. On a later no-op FULL, it generally
returns to zero. A persistent incremental revision that does not reevaluate an
unaffected node would retain the old temporary value, causing a metadata-only
mismatch against FULL unrelated to CSS or future computation.

## Proposed change

New runtime-produced payloads store `inherited_change_impact = 0`. The real value
remains in PreparedNodeResult for the current traversal and visual impact logic,
unchanged. `declared_impact` and `declared_inherited_impact` remain retained exactly
as before: compute_node does read those historical values as fallbacks. Imported
NodeRecordInput/NodePayloadInput keep their supplied values and tests continue to
compare them. No field is removed from the private payload and no public C ABI
struct changes.

The complete semantic comparator continues to include all three fields, so a
forged/imported noncanonical value does not silently compare equal. FULL and
incremental runtime factories both follow the same new canonical convention.
No global traversal is needed to normalize an older retained revision. A first
FULL on new code builds canonical payloads; older snapshots remain unchanged.

## Evidence of readers, C3b base 25ac906

`grep -R -n inherited_change_impact native/src` finds only:
- `style_runtime/style_runtime_core.cpp`: definition and compute output,
  current PreparedNodeResult propagation, and assignment during full freeze.
- `css/frozen_node_record.{h,cpp}`: private transport/storage/getters and import
  forwarding. No public C accessor returns this field.
- `style_runtime/state_semantics.cpp`: exact semantic equality of frozen fields.

Before C3b, `css/style_revision.cpp::payload_equivalent` compared the same stored
field for persistent reuse; C3b delegates this to node_semantically_equal. No
layout, paint, transitions, public update API, measures or selector consumes the
retained inherited_change_impact. `declared_impact` and declared_inherited_impact
are consumed by compute_node and are explicitly outside this normalization.

`aether_style_revision_get_style/get_change` exports `entry.impact()`, a separate
revision impact value. `aether_style_runtime_get_update` exports update->impact,
also separate. Their values and the legacy complete enumeration are unchanged.

## Required tests

Retain a revision and handle across a color/font/currentColor change followed by
no-op; old revision stays unchanged. Compare FULL and incremental complete CSS,
measures, scopes, symbolic flags and dependency metadata. Verify visual impact
and propagation through children remain the same as pre-change full behavior.
Import nonzero private metadata and verify it remains preserved/distinguishable.
Run existing style-impact, atomic revision, contextual, inherited/color/shadow and
new semantic tests. This is a representation normalization, not authorization to
activate any unproven incremental category.
