# DOM mutation journal foundation (C1)

The journal observes existing input epoch boundaries. It does not authorize an
incremental resolve, acknowledge a revision, provide a committed state ledger,
certify stability, or change the existing full-resolution path. The resolver,
revision/map factories, generated-node publication and style-tree epochs are
unchanged. Sheet/registry/environment invalidation remains outside this journal.

Each document embeds 128 fixed records. Recording copies authentic shared
identity tokens without retaining nodes or allocating. Tokens remain safe to
inspect for alive/attached after detach/destruction; their `owner` is not safe to
dereference after death. Pointer addresses are not substituted for identity.
State records retain every transition's old/new bits, including A-B-A, with no
coalescing of observable history. They are not the last committed style state.

Successful legacy mutation/structure epoch counts are preserved:

- `set_state`: one state record only when bits change.
- `set_attribute`: one attribute record even for equal values. The touch now
  occurs before map/string writes, after attribute-name canonicalization. An
  allocation failure may leave a conservative record even if no visible value
  changed; this intentionally differs from the former failure behavior and
  covers partially completed map insertion/string assignment.
- `append_child`: structural record on the new root and, if previously parented,
  on the old root. Same-root moves retain both increments. The subject is the
  moved child, not merely its parent. Reserve/validation still precede touching.
- `remove_child`: structural record on the previous root before detachment.
- `parse_html`: one coarse structural replacement record before destruction or
  parsing. It covers all parser attribute/text/structure initialization, including
  partial replacement on OOM. It cannot be reconciled as a state-only mutation.
- Legacy `aether_node_touch`: unknown record, permanently lost typed coverage.
  There is no public text setter today; internal text writers can declare text
  using the typed helper or retain unknown/full fallback with the legacy helper.

Only actual document roots own coverage. Detached or other non-document roots
still advance their previous identity epochs but return no journal. Reattachment
is structural and must continue to resolve fully. Generated/anonymous staging
writes remain under existing style-tree publication guards, not this DOM journal.
Direct internal field writes must still use touch; the journal cannot discover
an unannounced write that changes neither an epoch nor a record. No C1 consumer
assumes it can.

Overflow, unknown writes, epoch discontinuity/wrap are sticky loss. The fixed
prefix is retained, epochs continue advancing, and `coverage_lost(current)` must
remain true without allocation. A mismatched current epoch also reports loss
before another record arrives. There is deliberately no reset/ack API: after
128 records (including initial parse) loss persists for the document lifetime.
A future safe committed-baseline protocol is needed to recycle capacity. This is
not yet a usable incremental journal. Even complete recording alone says nothing
about an authentic committed revision, same-tree provenance or input stability.

Tests compile the real DOM boundaries for deterministic operator-new injection.
They cover state A-B-A and unrelated mutations, attribute/text/unknown typing,
existing epoch counts, detach/reparent/destruction/token retention, overflow,
missing epochs/wrap and allocation failures. Journal append and loss handling
remain operational when all allocation attempts throw; attribute/parse failures
conservatively invalidate. There is no separate journal OOM branch because the
journal never allocates.
