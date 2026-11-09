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
- Isolated component render candidates and pinned handler registries, with prepared host replacement.
- Host in-place typed mutations/imports, source CSS slots, historical input-value cache and staged runtime element indexes.
- Native finalizable owner/release API, owner relays across adoption and retained generated/retired storage (global lifetime gate remains open).
- Native interaction claims, frozen first-disconnect routes and provisional restored claims (managed integration and complete gates remain open).

Chronology, OOM, counter exhaustion and lifetime changes are intentional v6 contracts.
Old tests requiring unsigned epoch wrap, an exception crossing C ABI, or partial
parse destruction on OOM are replaced with stronger no-write/status checks. Separate
journal injection tests retain irreversible poison/gap and overflow coverage.

## Gates still open

Complete managed interaction claims/safe-point reconciliation and implicit Document materialization,
all mutation/legacy routes participating in those transactions, full cross-document/lifetime
coverage, complete allocation-failure matrix, independent implementation review, clean-hover
work budgets and platform gates. Successful focused runs do not close these gates.
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

## Later exact checkpoints

- f64e411 native full build succeeded; CTest 244/246. The two failures were real regressions:
  retired style payload retention and swallowed temporary-buffer allocation failure in stylesheet
  ordering. Neither original oracle was removed.
- c6956be managed full publication runner passed: native source rebind, component registry/staging,
  host getter/import/typed inputs/value cache/source edits, managed/native DOM, transition groups
  and all 22 host protocol cases. Its native tree is byte-identical to f64e411; loaded shared-library
  SHA-256 was `443aa334560091f3c10684d83a21c23e16ed4487a11cb276d09821e1fd527624`.
- c0806cb fixed both native regressions; exact focused CTest 5/5 passed (immutable-style-revision,
  style_revisions_atomic, dom-edit including new-document kind 13, dom-style-binding and dom-owner).
  The managed runner stopped on its GC ownership assertion. Diagnosis found a disposed local stage
  retained the document reference; subsequent code releases it and the test scopes the stage in a
  NoInlining helper. That correction still requires a new exact run.
- 49d2a31 additionally pins retired pseudo origin/ancestor relays across later adoption and stages
  cross-document wrapper arrays before commit, eliminating lazy Dictionary.Values allocation.
- fe576cf stages public runtime element indexes for structural managed edits; source changes and
  in-place edits no longer rely on an incidental later import to refresh those indexes.
- Native claims initial b7fb71b core built; owner/edit CTest 2/2 passed. Its claim test had a harness
  compile error, corrected in 3942077 alongside provisional claims and connected-to-detached moves.
  No complete claim/lifetime implementation PASS is inferred from these partial results.

The former D1 tests that required direct managed input rejection were provisional boundaries.
Version 6 requires successful reconciliation, so those five cases now assert current values,
retained wrapper/handle identity, valid hit/cursor publication and native R2 preservation. Other
marshaling, OOM, serial, reentrancy and no-partial-frame failure oracles remain in place.
