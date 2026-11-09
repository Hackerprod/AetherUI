# Proposal: acknowledgment of certified incremental publication

Status: design proposal only, awaiting independent review. No ACK implementation
or feature activation is authorized by this document. Code baseline is C3c
60ee3b70cc6d741df0a64f94a03a3232d4e297af, kept frozen in its original checkout.
The proposed code change is a later checkpoint after both C3c certificate review
and this focused protocol review pass. Default incremental option remains OFF.

## 1. Purpose and inherited invariants

The128-record document journal currently recognizes only reviewed FULL commits.
Even genuine no-op/local state commits eventually overflow and require FULL.
Allow a proven complete incremental publication to recognize precisely the input
epoch represented by its candidate, without authorizing recognition from success,
generation, a boolean status, an arbitrary revision handle or a caller batch.

Preserve the approved C3a protocol unchanged in substance:
- Journal is per document, not per runtime. Calls on the same document require
  external serialization; per-runtime owner-thread guards are not a document lock.
- floor_epoch is an inclusive eligible BASE epoch; retained RECORD epochs are
  strictly greater than floor. Incremental base.input_epoch must be >= floor.
- floor never decreases. At most128 records remain. Release at ACK is bounded,
  has no allocation, user callback or fallible operation.
- Ordinary overflow/unknown coverage may recover after authentic current FULL.
  Any epoch discontinuity/wrap permanently sets independent monotonicity poison,
  including a wrap hidden behind an earlier overflow. No FULL or incremental
  publication clears poison, lowers floor or starts a new implicit era.
- Old snapshots retain all state/metadata; journal recognition changes neither
  those snapshots nor their committed ledger. A new document is the only poison
  recovery. FULL can still publish on a poisoned document, without restoring
  incremental coverage.

## 2. Exact authority: private publication receipt

Do NOT remove the state_result.incremental guard and infer FULL authority from
its false value. Do NOT add a trusted boolean or a public acknowledge/reset API.

Introduce a private immutable CertifiedPublicationReceipt produced by the same
factory operation that consumes an authentic CertifiedRevisionDelta and freezes
its exact contextual StyleRevision. A nonempty receipt has a private constructor,
a factory self-owner seal (the same control-block discipline as revisions and
certificates), a pin to the exact output revision, authentic document token and
its immutable generation/input/structure/output-style-tree epoch tuple.

The private certificate records a typed producer origin: complete FULL resolver
or reconciled self-agenda state resolver. Only the existing private FULL producer
and StateRevisionPreparation may set that origin. It is not accepted from public
inputs. The receipt copies it after certificate self-owner/header validation.
Origin alone is insufficient: the authentic receipt must bind the exact candidate
revision and all publication guards must pass.

Factory API details proposed:
- Keep the existing private freeze_certified_revision(certificate, out_revision)
  operation usable by tests/import compatibility paths without recognition authority.
- Add an internal overload/output for a receipt. It constructs the revision and
  receipt entirely during preparation, publishes neither output on failure, and
  issues a receipt only for a genuine contextual certificate.
- Existing legacy/import factories do not issue receipts. A legacy/noncontextual
  transaction can still follow its existing publication contract, but cannot ACK.
- The FULL and incremental runtime preparation paths request and retain the
  receipt in their transaction. The full path therefore no longer relies on a
  default false incremental-status field as its ACK discriminator.

The receipt belongs ONLY to the transaction. It pins the output revision but is
not stored by a revision, runtime cache, map or exported handle. The output
revision must NOT retain its certificate/base chain or receipt. This prevents a
new unbounded chain of historical revisions or any ownership cycle. Existing
transaction pins already retain base/compiled/registry owners until its retirement.
The certificate can be dropped after receipt issuance. No journal iterator or
borrowed record pointer is retained by either object.

An alias shared_ptr with the same receipt address but a different owner fails
its self-owner seal. A genuine receipt from another candidate/document/generation
fails exact binding. A fabricated status object, cloned public revision handle,
legacy import or reconstructed header cannot create a nonempty genuine receipt.

## 3. Evidence certified by each producer

FULL authority remains the reviewed exhaustive resolver certificate: every
current entry and retired entry is validated, document/generation/context is
coherent, and the candidate represents all current input regardless of ordinary
journal loss. Contextual FULL gets a receipt even if its later ACK must be skipped
because permanent poison is set.

Incremental receipt issuance requires the complete C3c proof, not just valid
memory or equal pixels:
1. Authentic complete contextual base of this document, same frozen environment,
   compiled stylesheet/AST/index/program owners and sealed registry owner.
2. Complete contiguous journal coverage after base through captured input_epoch,
   base >= floor, no poison/loss/non-state input, authentic live identities, and
   reconciliation against committed old state and final DOM state. Caller
   declarations cannot erase records. A -> B -> A is reconciled from that ledger.
3. AST superset proof for every relevant occurrence. Only the reviewed empty or
   self local agenda category is admitted. Relevant unindexed relational routes,
   unsupported coverage, affected pseudos and changed root font still use FULL.
4. Parent-first computation, complete frozen/prepared parent context, semantic
   dependency propagation even for visual impact zero, independent candidates
   retained, and exact new frozen leaves/ledger paths. Untouched subtrees are
   shared only from that authentic base.
5. Frozen exact output and receipt allocated before publication. No fallback
   candidate or partial incremental attempt issues a receipt for a different
   revision. A successful fallback gets only the receipt of its FULL result.

This proposal does not broaden any C3c category or replace its implementation
review. If that proof fails, there is no incremental recognition authority.

## 4. Prepare, validation and non-fallible publication point

Prepare is observational: it may read the journal and stage a receipt but must
not advance floor, discard records, mutate links/runtime/current revision, clear
loss or recognize a managed batch. OOM (including receipt allocation), stale
inputs or abandoned transactions retain existing outputs/journal and return the
existing explicit failure; no automatic larger OOM retry is introduced.

Immediately before publication, validate receipt authenticity and exact binding
in O(1), in addition to ALL current commit guards:
- transaction belongs to this runtime and is not committed/preparing; owner thread;
- base pointer AND authentic ownership/generation remain current;
- sheet/compiled and sealed registry pins remain those used by preparation;
- document alive/authentic, input epoch and structure/style-tree epochs captured
  by preparation are still current; candidate generation and document tuple equal
  the receipt, and receipt.output_revision has the same genuine owner as candidate.

For an incremental receipt also recheck the cheap journal conditions before any
publication: no current loss/poison, current journal epoch consistent, and floor
not above the committed base input. This is a constant-time validity check, not
another replay or full scan. An ordinary mutation updates input_epoch and fails
existing guards; the extra checks also conservatively reject lost evidence that
cannot justify an incremental acknowledgment. A missing/forged/mismatched receipt
for a contextual runtime candidate is rejected before publication. Genuine
noncontextual legacy transactions continue without any ACK eligibility.

After all allocations/validation, publish native revision/caches/links through
existing non-fallible swaps, mark this transaction committed, then acknowledge
from the bound receipt BEFORE any diagnostic replay and BEFORE returning the API.
The ACK helper accepts only the transaction's validated private authority; no
numeric epoch or public status is an alternate authority.

ACK recognizes exactly receipt.input_epoch == the document's current input epoch.
It sets floor to that epoch, clears the bounded records and ordinary loss, and
leaves permanent poison untouched. If poison is present or input < existing
floor, it does nothing and does not lower/reset anything. A FULL publication can
therefore succeed while leaving the document permanently non-incremental.

For a valid incremental publication, no future record can already exist in this
serialized callback-free interval. Recognition includes ALL inputs through the
candidate epoch, including journal subjects with no semantic/visual delta. It
never recognizes an epoch beyond the published output or follows a later live
runtime pointer after replay. The helper performs no allocation, traversal of
revision entries, comparison of payloads or user callback. Release of at most128
journal token owners has the same bounded reviewed C3a behavior.

## 5. Multiple runtimes, older revisions and replay

Suppose runtime A publishes R1 at epoch e1 and recognizes it. Another runtime B
whose base input < e1 cannot use the discarded journal interval: its next state
operation must FULL. B's old retained styles/ledger remain valid historical
snapshots. B may FULL from current inputs, publish its own authentic result and
recognize the current epoch; floor never moves backwards. A staged transaction
from either runtime cannot publish after the other's publication because shared
style_tree_epoch changes (even if input_epoch is unchanged).

If a diagnostic callback after R1 publication mutates input to e2, it appends a
new record strictly above floor e1. If the callback publishes R2 and recognizes
e2, those operations use their own receipts/guards. Any later callback mutation
e3 remains after floor e2. The outer operation still returns R1 and performs no
ACK, runtime write or journal cleanup after replay or on API return.

A callback that only mutates input (without publishing) leaves the new records
for recovery. A callback that throws leaves the committed R1 and already valid
recognition intact, preserving the current diagnostic exception contract. This
native protocol does not authorize D1 managed batch recognition: D1 still verifies
the post-callback current token and must not project or acknowledge superseded R1.

## 6. Required implementation tests

Run against exact frozen source with default flag OFF and experimental ON;
repeat relevant counters-OFF functional and actual-core ASan/UBSan gates.

Authority and binding:
- Real FULL, real empty agenda and real local propagated result each get the
  receipt of their exact revision. No-op with a ledger-only change may ACK.
- Missing receipt, fake self-owner, alias-control-block forgery, receipt from a
  different candidate/runtime document/generation, public revision clone and
  legacy/noncontextual imports cannot ACK or bypass prepublication rejection.
  Flipping/copying state_result.incremental is never authority.
- Failed FULL/incremental/fallback preparation and abandoned transactions do not
  consume the journal. Exhaust all C++ allocation failure points including the
  new receipt and verify commit has no allocation before replay.

Publication and history:
- Input/structure/sheet/registry/base/style-tree changes between prepare and
  commit reject without floor/records/revision changes. Commit twice rejects.
- FULL and state commits recognize exactly their output epoch, never a later
  current runtime epoch. Old snapshots/handles/maps and committed ledgers stay
  unchanged.
- Two runtimes alternate, one falls behind floor, one prepares before the other
  commits at the same input, and callbacks publish R2/mutate e3. Verify exact
  floor and retained records at every point, including immediately in callbacks.
- Diagnostic callback observes ACK already complete; later callback mutations
  survive after the outer API returns. Outer result remains R1 if current is R2.

Loss and boundedness:
- More than128 ordinary mutations before prepare -> FULL recovery -> next proven
  local/no-op may increment. Complete batches at capacity behave consistently.
- Unknown loss -> authentic FULL recovery. Overflow -> wrap/gap -> FULL never
  clears poison; floor greater than input never decreases. Gaps hidden behind
  existing loss remain poison. A new document starts a separate valid history.
- At least10000 sequential no-op and local publishes with old revisions retained:
  no periodic JOURNAL_INCOMPLETE FULL when coverage stays valid, floor equals
  each successful committed epoch, bounded memory/records, original generations
  and ledger preserved. FULL shadow audits occur outside measured operations.
- Work counters distinguish removed overflow FULL work from unchanged O(depth)
  planner work and persistent path allocation. No zero-DOM or global latency
  claim follows from ACK alone.

## 7. Sources and scope of inference

This is a narrow extension of the already reviewed journal protocol and the
private C3b/C3c certificate boundaries. Source anchors in60ee3b7:
- state_reconciliation.cpp::reconcile_state_inputs;
- style_runtime_core.cpp::StateRevisionPreparation::prepare_local;
- style_revision.cpp::freeze_certified_revision;
- aether_style_transaction_s::acknowledge_full_publication and
  aether_style_runtime_commit_revision;
- dom/mutation_journal.h acknowledgment floor/loss/poison logic.

Chromium's pinned element.cc::ApplyComputedStyleDiff and
PropagateInheritedProperties informed the semantic producer, as documented in
state-local-certification.md. Blink is not attributed an equivalent Aether journal
receipt: ACK ordering and multi-runtime safety are proved from Aether's own
transaction model and the previously approved floor/poison protocol.
