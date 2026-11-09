# AetherUI hover: phase B foundation checkpoint

This is B1+B2, **not complete phase B**, and does not enable the incremental resolver (phase C). The original full resolver, contextual ABI, generation checks, same-owner checks, and override guard remain in place. No CSS or input/event semantics changed.

## B1: payload and bound-owner separation

- `FrozenMeasurePayload` owns only node-local immutable measures; it has no revision/document context.
- `FrozenMeasures` is the compatibility/export binding of that payload to one immutable document owner. Existing consumers retain their API and contextual validation.
- `FrozenNodePayload` owns immutable style, local measures, scopes/color metadata, identity tokens and generated-leaf storage. It does not retain an old measure document or revision.
- `FrozenNodeRecord` is the compatible bound record, with payload plus bound measures.
- `BoundNodeView::find` accepts an authentic revision owner and pins that revision with its payload. Lookup and measure-view construction allocate nothing.
- `MeasuresReadView` reads a local payload and the explicit bound document without constructing maps/vectors/wrappers on read.
- Added tests for a payload outliving and releasing its previous measure document, explicit new measure binding, zero-allocation revision lookup and existing ownership invariants.

## B2: persistent backing and legacy patches

An immutable compressed radix (Patricia) tree replaces the backing hash-table copy. Full imports still validate all input entries; patch validates the supplied changed entries and path-copies bounded routes. It never retains a prior backing/revision as an overlay. Legacy map update consumes patches instead of copying all N records. Directory traversal is allocation-free; public revision enumeration order remains unchanged.

## Remaining phase B work

1. Replace revision backing of bound records with an authentic payload directory, with revision-aware consumers/export adapters.
2. Persistent ordered manifest and a certified revision-delta factory: current `entries`/`changes` still materialize full arrays.
3. Eliminate N per-revision bound record wrappers on the future delta path. The full resolver intentionally still constructs N payloads and bound records in this checkpoint.
4. Migrate all relevant contextual consumers to the new bound views, with full differential/invariant validation. Current APIs continue through compatible bound records.
5. Persist the expanded backdrop/parent metadata required by later invalidation, without changing CSS.

Do not claim O(K) revision publication, completion of B, improved Windows CPU or incremental hover from this checkpoint. The full resolver may allocate more due to explicit payload wrappers; the demonstrated asymptotic improvement is the legacy map backing patch only. No .NET/Windows execution is claimed.

## Verification status at this checkpoint

Focused native tests pass: immutable revision/map, immutable handle, frozen backing and backing OOM, new persistent backing (also UBSan), contextual C ABI, atomic style revisions (4401 checks, 22 aligned failures), and work-counter fixtures with counters ON and OFF. Core builds ON/OFF pass. The complete native Release suite passes 229/229 tests, and independent review also passes the corrected B1+B2 product checkpoint. The alternating map-replacement benchmark separately passes ON/OFF at 18/100/500/1000 cards, with K=2 effective replacements per iteration and no timed resolver work. No .NET or Windows execution is claimed for these changes.
