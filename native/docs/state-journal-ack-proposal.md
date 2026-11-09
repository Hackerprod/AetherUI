# C3 journal acknowledgment proposal

Historical predecessor protocol. This branch implements the separately reviewed
C3d extension documented in state-publication-receipts.md; restrictions below
describe the earlier checkpoint and remain its historical evidence.

Status: revision 2 of this protocol received independent design PASS. C3a implements FULL-only acknowledgment in b5297b3 and received independent implementation PASS. Incremental acknowledgment is not enabled.
Base: integration commit 45c9647. Approved design reference: DESIGN.md sections 4 and 8.

## Purpose

C1 records at most 128 mutations and its loss flag is irreversible. A transaction that has computed every style from the current DOM needs a safe point from which future state-only batches can again be proven complete. A consumer must never clear missing evidence while still using an older revision.

## Proposed representation

The document journal adds `floor_epoch`, initially zero. It retains records strictly after that floor, with the existing contiguous last epoch and loss reason. The existing capacity remains 128 and append stays allocation-free. No external/public reset or caller-supplied trusted flag is introduced.

A private acknowledgment operation is available only to the native style transaction publication implementation. Its arguments are the document token and the exact input epoch of the successfully published authentic candidate. The method is called after all fallible preparation and all commit validation have finished, before any diagnostic callbacks. At that point the document mutation epoch must equal the candidate input epoch. This invariant means there can be no later journal entries to preserve in an owner-thread commit. The operation releases the bounded retained record tokens, sets size to zero, sets floor and last epoch to the committed input epoch, and clears loss. It allocates nothing and never touches DOM state.

Both an authentic complete FULL publication and a genuinely certified incremental publication may acknowledge. The first implementation uses acknowledgment only on FULL publication until the incremental certificate exists and passes review. A generation increment, status success from a wrapper, arbitrary epoch, prepared but unpublished candidate, imported legacy snapshot or mere absence of errors is not authority to acknowledge.

## Reconciliation conditions

Before a state-only prepare can reuse the base, it checks:

1. Base revision is the runtime's current authentic contextual revision, from the same document token, structure/style-tree epochs, stylesheet/registry and environment inputs.
2. Base input epoch is at least `floor_epoch` and no later than the current document mutation epoch.
3. Journal has complete coverage through the current epoch. Iterate only retained bounded records after the base epoch; their epochs must be contiguous through the captured current epoch.
4. Each record is a state mutation of an authentic, live, attached node token represented in the base ledger. No raw owner is dereferenced before the token's liveness and provenance checks.
5. For each identity, the first old state equals its committed ledger state, intermediate old states equal the prior new state, and the final new state equals the current DOM state. Caller declarations may add validation but cannot delete journal records or overwrite its facts.
6. Consolidate against the committed ledger, not the most recent event. A→B→A can produce no style agenda while the new input epoch is still committed.

Any failure causes an explicit FULL fallback (or invalid input status where the API contract requires rejection), never partial certification. An older base cannot reconstruct lost state from the caller's list.

## Multiple runtimes and retained revisions

The floor is document-wide, not per runtime. If runtime A publishes and advances the floor beyond runtime B's current base input epoch, B must perform FULL. This may reduce optimization when multiple runtimes interleave, but cannot omit an input. B's successful FULL may establish the next floor; A then applies the same rule. A retained revision remains immutable and readable regardless of the floor. It is not an authority to prepare incrementally unless it is the matching runtime's current revision and still satisfies all coverage guards.

A floor at the same epoch as an older runtime base does not itself invalidate coverage: there are no missing changes after that base. Separate stylesheet/runtime/version guards still apply. A FULL publication may increment style-tree epoch even without logical structure change, so the existing style-tree guard may conservatively force the other runtime FULL.

## Failure and callback cases

- Prepare never acknowledges, mutates journal storage, changes its floor, or changes DOM state. Its records/pins/candidate are private.
- OOM anywhere before commit leaves the active revision, generated links, ledger, floor, loss and records unchanged. Already-applied input DOM mutations remain pending.
- Stale base generation, sheet edit, DOM mutation, document death, wrong thread or double commit rejects before acknowledgment. Failure does not consume evidence.
- No callback runs within publication. The acknowledgment is completed before diagnostics replay. A diagnostic mutation appends after the new floor and remains pending. An inner successful publication can acknowledge its own current input independently. The outer function never writes runtime or journal again after replay begins.
- If append overflows after a commit, coverage becomes unknown. The next prepare falls back FULL. Only successful FULL commit clears that loss at its exact current epoch. Failed FULL does not.
- Unknown/non-state mutations similarly require FULL before recovery. Loss may clear only because FULL recomputed all current inputs, never because a caller declared the batch state-only.
- Journal capacity and token release work are bounded by 128, not document size. This cost must be counted separately from style work.

## Required tests before merging

Interleaved two-runtime FULL/state publications; same-epoch and older-epoch bases; retained old revisions; >128 mutations and loss recovery; unknown/attribute/structure records; A→B→A and duplicate declaration order; OOM at all preparation points; commit mutation rejection; stylesheet removal between prepare and commit; diagnostic mutation after acknowledgment; diagnostic inner publication plus subsequent mutation; double commit; document destruction; pointer reuse with a different token; default gate OFF. Confirm old revisions and handles retain their original values/generation and that no failure clears dirty evidence.

## Trade-off

The single floor sacrifices cross-runtime incremental hit rate in exchange for bounded memory and a simple proof. It does not promise a multi-consumer retention protocol. A future per-runtime watermark scheme would need a separate reviewed design.

## Revision 2: monotonicity poison and synchronization

`floor_epoch` is monotonic for the lifetime of a document identity. There is no epoch era/reset in this change. The journal gains a separate sticky `monotonicity_lost` bit, checked on EVERY append, even when the first visible loss is already overflow or unknown. Any zero epoch, noncontiguous epoch (including forward discontinuity), or wrap from UINT64_MAX poisons monotonicity permanently. Ordinary overflow and unknown mutation remain recoverable through successful FULL; epoch discontinuity is not.

Acknowledgment additionally requires `!monotonicity_lost`, captured input epoch equal to current/last epoch, and input epoch >= existing floor. If any check fails, it does not clear records/loss or lower the floor. A valid FULL publication may still finish, but the journal remains unusable for incremental resolution for that document identity. A new document identity starts a new journal. No amount of FULL publications repairs an old poisoned identity. This intentionally sacrifices optimization after internal epoch corruption rather than attempting to infer a new era.

Tests must separately cover overflow → subsequent wrap → FULL, because preserving only the first JournalLoss would miss the wrap. Also cover floor already greater than candidate input, zero epoch, UINT64_MAX→0, forward gap, and repeated FULL after poison; verify floor never decreases and no incremental certificate can be produced.

The document is not concurrently accessed. The existing native ABI contract (README lines 251–255) requires callers to serialize access to one document externally. Runtime owner-thread checks remain in addition to this contract; they are not a document mutex. Interleaved runtimes in this proposal mean serialized calls, including reentrancy after publication, not simultaneous mutation from multiple threads. Introducing concurrent document access is outside scope.
