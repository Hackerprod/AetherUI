# C3b experimental no-op state certification

Historical reviewed foundation. The C3c local-category extension is specified
in state-local-certification.md; C3b restrictions below describe its predecessor.

The build option AETHERUI_ENABLE_STATE_INCREMENTAL defaults OFF. It is an internal
experimental gate, not a user trust flag. With the option ON, the only incremental
category in C3b is a state batch proven to have an empty AST candidate superset.
Affected selectors, relational routes not yet indexed, pseudos that are affected,
non-state input, missing coverage, different roots, environment, sheets or registry
retain explicit FULL fallback. A missing rule match is never evidence by itself.

## Authority and ownership

The only incremental certificate producer is StateRevisionPreparation inside the
native runtime implementation. It takes the current runtime/document, not a list
of claimed unchanged leaves. It independently reconciles every bounded journal
record after the authentic committed base. Necessary source filters and state
masks precede rejection of relevant relational routes. The C2 planner then derives
candidate host+slot identities from the authentic pinned AST programs, without
filtering by current dynamic matches. Only an empty result can mint a certificate.

Reconciliation validates authentic token membership, live/attached identity,
committed old state, intermediate continuity and final DOM state. It consolidates
A→B→A against the revision ledger. Caller declarations cannot erase observations.
No iterator into the journal escapes. Structure/style-tree epochs, the active base,
stylesheet pin and epoch, registry owner and environment must remain unchanged.

The sealed property registry, compiled stylesheet/AST/index/programs, base revision
and candidate document are retained by shared owners. Registry replacement is
recognized by owner identity even if an allocator might otherwise reuse an address.
Preparation and commit reject a changed registry. Base registry and compiled input
pins defer destruction of old definitions/AST until after publication/replay rather
than releasing their last owners inside the non-fallible publication boundary.

## Persistent output and work bound

A private certificate shares the existing complete immutable payload directory and
manifest. Only committed ledger entries that changed are replaced through bounded
persistent paths. The new frozen document has the new revision generation and the
same verified contextual values. Old handles and maps keep their original document
generation and owner. Imports still require complete certification and cannot mint
this proof. The legacy complete entry/change enumeration remains available lazily.

The no-op path does not copy a runtime state table, visit the complete manifest,
compute any style or construct output records for intact nodes. It does inspect the
bounded journal and relevant program/source data; this is not a claim of constant
work for arbitrarily large CSS or batches. Relational routes are rejected before
any unindexed DOM traversal in this category. Directories do not form overlay chains.

## Publication, failure and diagnostics

All allocations, including new persistent paths, header, transaction and outward
handle, precede commit. OOM returns status and leaves active revision, ledger and
journal unchanged; it never silently becomes zero changes or retries FULL. Stale
commit rejects before swaps. The successful outer operation returns its own pinned
revision even if a later diagnostic callback publishes another revision.

No-op performs no declaration evaluation, so it emits no diagnostics from untouched
invalid CSS. FULL retains its existing replay contract. This follows approved design
section 8.2, not a claim of identical callback counts under side-effecting listeners.

Incremental journal acknowledgment remains DISABLED. Only the reviewed FULL path
acknowledges. After enough incremental commits to fill the fixed journal, preparation
falls back FULL. Do not claim indefinite zero global work or enable incremental ACK
until the certificate implementation receives independent review and its own gates.

## Scope of semantic helpers

Payload equality compares all frozen style scalars/payloads, symbolic measures and
metadata, ordinary/backdrop scopes and statuses, currentColor flags, identities and
parent dependencies independently from VisualImpact. Unknown property dependencies
are conservative. These helpers are groundwork for the next scheduler; C3b does not
recalculate a local affected subject or propagate inheritance incrementally.

The transient inherited_change_impact normalization remains a separate proposal.
No .NET projection, hit testing, Windows performance or complete phase C gate is
claimed by this checkpoint. Exact test results and outstanding gates are recorded
by the implementation report outside this source document.
