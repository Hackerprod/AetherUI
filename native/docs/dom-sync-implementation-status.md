# DOMsync implementation checkpoints

The design in `managed-dom-sync-proposal.md` is approved as version 6, exact SHA-256
`6bf2f8f3937f422e4c1da6c01081a9588460f1c57149f267d9f8913587d30885`.
Its historical draft heading is preserved to keep that reviewed artifact unchanged.
Design approval is not implementation approval. Experimental flags remain off.

## Implemented foundations, not full D1

- Editable per-node/root storage, detached ownership observation and persistent disconnect/stamps.
- Legacy no-throw strong create, parse, attribute and structural mutations.
- Immutable typed snapshots with retained storage and prepared typed edits.
- Style/revision/layout pins independent from logical liveness.
- Managed compound input staging, ASCII name identity, snapshot collection views and coherence barriers.
- Native legacy transition candidates and strong Tick, distinct mutation/status revisions,
  sticky TickError acknowledgment by complete sampling only, managed joint publication and derived-frame gates.
- Isolated component render candidates and pinned handler registries.

Chronology, OOM, counter exhaustion and lifetime changes are intentional v6 contracts.
Old tests requiring unsigned epoch wrap, an exception crossing C ABI, or partial
parse destruction on OOM are replaced with stronger no-write/status checks. Separate
journal injection tests retain irreversible poison/gap and overflow coverage.

## Gates still open

Full native/managed DOM bridge activation, source CSS slots and component replacement,
interaction claims and loss-route freezing, every mutation/legacy route participating
in those transactions, cross-document/materialization coverage, complete allocation
failure matrix, independent review, clean-hover work budgets and platform gates.
No serializer/rebind recovery or experimental default activation is authorized.

## Executed evidence

- Foundation d5a70ba: full native build; CTest 237/240. Failures were the private raw-node
  compatibility regression and the two superseded contracts above. Fixes require a new exact run.
- 7d6f8f1: native focused 5/5 (storage, snapshot, typed edit, transition sync and legacy transition).
  Managed build and runner pass: component/DOM staging and transition native/host groups,
  plus the 22 prior D1 publication tests. Existing tests of rejected unsupported direct
  managed changes remain only an interim gate, not successful DOMsync recovery.

Builds use immutable commit archives and isolated native/managed outputs. Loaded shared
libraries are hashed and never overwritten during a runner. Linux results do not establish
Windows performance or macOS locale behavior.
