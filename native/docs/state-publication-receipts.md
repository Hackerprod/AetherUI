# Certified publication receipts and journal acknowledgment

C3d implements the separately approved revision 2 ACK protocol on top of reviewed
C3c60ee3b7. Implementation is experimental and awaits independent gates. The
build option AETHERUI_ENABLE_STATE_INCREMENTAL still defaults OFF. This does not
activate managed D1 projection, hit-test or drawing changes.

## Authority is not schema validity

certify_full_payload_revision validates supplied leaves and their header. It does
not prove that the list covers the DOM or that old styles were recomputed. Its
certificates always have immutable PublicationOrigin::none, even when contextual.
No caller argument can select another origin. Its ordinary schema-freeze API
remains compatible, but receipt issuance rejects it atomically.

FullRevisionPreparation is defined only in style_runtime_core.cpp and owns the
complete prepare_tree traversal, prepared order, generated links and outputs.
It shares only mechanical schema/delta construction with the generic factory;
that helper grants no authority. The private producer constructs a new FULL
certificate only from its own exhaustive staging. It never promotes a generic
certificate or accepts a caller entry list/claimed-complete flag. The reviewed
StateRevisionPreparation producer constructs state authority only after its
journal, AST, identity and semantic agenda proof succeeds.

The receipt-producing freeze overload checks the certificate self-owner seal,
authoritative origin and contextual result. It first builds the exact revision,
then allocates an immutable CertifiedPublicationReceipt bound to that output's
authentic shared owner, document token, generation and input/structure/style-tree
epoch tuple. A receipt also has its own self-owner seal. Failure preserves both
out parameters; imports, aliases, status bits or a different same-generation
candidate cannot gain authority.

## Ownership and publication

Only the transaction retains the receipt. The receipt pins its output revision;
the revision never retains the receipt, certificate or historical base revision.
Existing transaction pins retain base/compiled/registry owners until retirement.
This prevents new revision chains/cycles while preserving prepare/commit lifetime.

Before publication, existing stale/thread/base/registry/input guards plus receipt
binding, compiled-owner and structure checks must pass. An incremental receipt
also requires current journal coverage with a floor no newer than its base.
These are bounded identity/epoch checks, not a replay or complete style scan.
Noncontextual legacy transactions remain non-acknowledging.

After native swaps publish the candidate and mark the transaction committed, ACK
runs before diagnostics. It requires receipt.input_epoch == current document
input_epoch == journal.last_epoch. It never lowers floor or clears independent
monotonicity poison. FULL may publish with last/current disagreement or poison,
but ACK preserves records/loss/floor exactly. A current FULL can still recover
ordinary overflow/unknown loss when all three epochs agree and poison is absent.

Diagnostic replay can mutate inputs, publish R2 and mutate again. Those records
occur after the recognized floor. The outer operation returns R1 and performs no
ACK or runtime write after replay. D1 must independently check its post-callback
token; native ACK never authorizes managed recognition of a superseded result.

## Tests and cost limits

White-box tests use the actual runtime sources, not test-only setters in the ABI.
They exercise generic complete/omitted/stale payload certificates; absent, aliased
or mismatched receipts; status flips; candidate replacement; wrong-thread/stale/
double commits; receipt lifetime with retained maps/handles; journal last/current
disagreement with and without prior loss; two runtimes sharing one document; and
real local/empty state authority under the experimental flag.

Allocation sweeps cover FULL, empty agenda, local color and local inherited/scoped
work, including receipt allocation. Failure must retain revision and every
journal record, return explicit OOM and avoid automatic larger FULL retry. Commit
with no diagnostics is tested with the next C++ allocation forced to fail; it
must not allocate. Callback tests separately verify ACK before replay and R1/R2
with a later surviving mutation.

Ten thousand no-op and ten thousand local snapshots preserve old generations and
ledgers without periodic journal-overflow FULL. They intentionally retain memory
proportional to snapshots and persistent differences. After releasing them, weak
revision owners must expire except the current one; releasing runtime/current
snapshot expires that final owner. Obsolete local payloads are checked separately.
This is expected-owner reclamation evidence, not a total leak-free measurement.

ACK clears at most128 token records and allocates nothing. That bounded step does
NOT bound the cost of diagnostic callbacks, prior snapshot destruction, or final
release of compiled sheets/large FULL revisions. Those owners are kept outside
the publication boundary by transaction pins, but retirement can still take time.
No latency claim follows from ACK or zero compute counters. The existing planner
still scans compiled programs and within() walks ancestors O(depth); persistent
path copies and evaluated-node cascade/scope work remain.

The approved protocol remains recorded verbatim in
state-incremental-ack-proposal.md, revision2 SHA-256
48143e1ef5d946904a8802975d9cb83ca0303bfdf9394975cf471ff0531e8e22.
Independent implementation review, exact platform CI and macOS reconciliation
remain separate gates. No default activation is authorized by this document.
