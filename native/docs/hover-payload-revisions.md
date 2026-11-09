# Persistent payload revisions (phase B3)

This implements the representation portion of the approved hover design. The production resolver remains full. There is no selective invalidation, hit cache, input coalescing or changed CSS behavior.

## Ownership and reads

Runtime states and revision directories retain `FrozenNodePayload`: frozen style, document-free local measures, ordinary/backdrop custom scopes, color metadata, impact metadata, identity controls and authentic generated storage. No node payload retains a measure document or historical revision. A revision owns its one document/generation, a persistent node directory and an ordered persistent manifest.

`BoundNodeView` pairs a payload with its authentic revision (or an already-bound legacy record). Contextual measure reads select that revision's document, never an old payload's generation. Lookup, full borrowed enumeration, layout, samplers and callback pins do not allocate rebound records. Public style handles and requested legacy updates may export bound records. Old exported handles retain old measures and generation.

A weak self-owner certificate rejects a revision or certificate carried under an unrelated control block. Original full-import checks remain intact, including exact backing/record/document ownership. The public C ABI and capability version are unchanged.

## Full proof, then persistent construction

`certify_full_payload_revision` takes the **entire** freshly prepared output, validates every leaf and order, and issues a privately constructed immutable certificate. It never infers CSS stability from DOM epochs alone, a hash, RGBA, a sparse list, or an `unchanged` boolean. The runtime's existing sheet/base/input checks still gate prepare/commit; captured DOM epochs also reject mutation between certification and freezing. The certificate is an internal full-result transport, not a public authorization to publish a revision.

For stable order, a node payload can be reused only when its complete local data is proven equivalent: exact frozen-style ownership, identities/generated owner, all impact metadata, ordinary and backdrop scopes, symbolic color metadata, and all local measure values/dependencies. Document/environment differences force replacement. Unproved equivalence is a conservative replacement. Pointer-free scalar byte comparisons can also cause conservative replacement due to padding; they cannot justify a false equivalence.

`freeze_certified_revision` consumes this proof and copies only changed directory/manifest routes. It stores no pointer to the preceding revision or certificate. Structural/order changes rebuild the complete persistent directory/manifest through the safe full path. The ordered full enumeration still includes zero-impact entries and retirements. Current impacts are separate from reusable payload/order state, so clearing a previous impact does not retain a stale change flag.

The full resolver still computes and traverses N nodes and constructs N candidate local payloads/entries to issue this proof. B3 makes no O(K) whole-pipeline claim. A future sparse certificate issuer requires phase C's independently validated input/dependency proof; it cannot use an unchecked flag in this interface.

## Contextual maps and legacy maps

A new contextual map pins its authentic revision and uses its directory directly. It does not materialize a backing or N bound wrappers. Exact-revision contextual override checks remain in force.

A requested complete legacy map is explicitly materialized through a dedicated factory, which exports every record from that exact revision. This is intentionally O(N) when the caller asks for a complete legacy map. It does not weaken general `make_map_version` provenance checks. Subsequent legacy partial patches preserve heterogeneous old/new bound contexts and use the existing Patricia backing; they export only K requested records. Repeated patches with the same payload and exact document owner reuse the already-bound record.

## Measurement and validation

Work-counter v1 is unchanged. `revision_records_created` now reports actual bound exports; a visually empty full resolve reports zero bound records, but `compute_nodes`, `state_entries_visited` and `revision_entries_created` continue exposing full N work. The latter counts logical full-preparation entries, not persistent copied routes. Certificate tests measure bounded allocation for K=1 independently of these counters.

Tests cover persistent keys including zero/MAX, differential updates, ordered manifests, contextual maps without backing, same-owner rejection, old/new generation binding, retired and generated lifetimes, mutation after certification, zero-allocation reads, OOM rollback and collection of historical revisions. Existing full-import, ABI, atomicity, layout, transition, callback and fault suites remain required. No assertions are removed to accommodate the representation.

Final integrated native and ON/OFF results are recorded with the implementation report. This document does not itself assert Windows CPU, renderer latency or phase C completion.
