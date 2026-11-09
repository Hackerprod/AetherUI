# Experimental local state revision certification

C3c extends the independently reviewed C3b empty-agenda category. It remains
behind AETHERUI_ENABLE_STATE_INCREMENTAL, default OFF. This document describes
implementation, not independent approval. Incremental journal ACK is prohibited.
No .NET, hit-test or drawing behavior is enabled by this native option.

## Admitted category and fallbacks

The private StateRevisionPreparation producer independently reconciles the
complete bounded journal against the committed revision ledger and pins the
same authentic base, document, compiled AST/index/programs and sealed registry.
The public preflight is not its proof. Current tree, sheet, environment and
registry requirements remain those of the reviewed state API.

Relevant AST occurrences must have proven coverage and only self route hops.
Necessary static source filters run before rejection, so unrelated relational
rules do not disable a local rule. Relevant descendant, child, sibling or :has
routes still return SELECTOR and resolve FULL; no unindexed search is hidden in
that category. The complete C2 planner remains the candidate superset producer.

If a nonempty agenda could coexist with before/after rules, use PSEUDO/FULL.
Generated or anonymous input subjects also use PSEUDO/FULL. Local generation,
content editing and retirement are not implemented here. An observed root-font
change discards staging and returns ROOT_FONT/FULL. Unknown inputs, lost journal,
structural changes, registry/sheet/environment replacement keep their explicit
preexisting fallbacks. OOM returns failure, never automatic larger FULL retry.

## Agenda and propagation

Manifest positions order the deduplicated agenda parent before child. Every
candidate/read must exist in the authenticated base with a live attached token,
matching owner pointer, identity and shared ownership. A current prepared parent
is used when available. Otherwise a pinned frozen parent supplies complete style,
measures, ordinary/backdrop scopes and symbolic color metadata. Its previous
operation's inherited impact is deliberately not replayed.

Each node runs the existing cascade/compute_node. The complete payload comparator
separates semantic change, parent propagation and visual impact. A semantic or
visual change is exposed through the compact delta, while the legacy revision
still enumerates every logical entry. An independently scheduled child remains
in the agenda even if its parent's output compares equal.

Concrete background/border colors and their currentColor reference flags have
separate propagation bits. Only their explicit inherit hooks consume those
parent fields. Unknown property handlers default conservatively to both color
bits; all other parent inputs always remain a dependency. Text/box shadow flags,
scopes, symbolic measures and nonlocal CSS changes conservatively propagate all
bits. A semantic metadata change can therefore schedule descendants with zero
visual impact; equal RGBA does not terminate it.

FULL and local payload construction use the approved canonical zero for frozen
inherited_change_impact. PreparedNodeResult keeps the real operation-local value
for current descendant impact calculation. declared_* metadata, imports and
strict comparison are unchanged. No existing payload is normalized in place.

## Publication, ownership and failure

Only this private producer constructs the incremental certificate. It contains
new frozen leaves, complete base subtree owners, compact ledger patches and
impacts. No public factory accepts a trusted/unchanged claim. Persistent directory
and manifest updates copy modified paths only; the complete base is retained.
The document context is newly frozen for the new generation, and exported views
bind to that exact revision without rebinding every untouched node.

All allocation, evaluation, comparison, diagnostics collection and output handle
construction precede commit. Commit rechecks base/epochs/current registry and
uses the existing non-fallible publication boundary. Base compiled/registry pins
prevent their last-owner cleanup from being introduced into that boundary.
Diagnostic replay occurs after publication and can publish a newer revision;
the outer result remains its original snapshot and never rewrites the newer one.
There is no incremental ACK before or after replay. FULL ACK remains unchanged.

## Cost model and limits of counters

The planner scans compiled programs/occurrences against changed journal subjects.
Its within() helper walks ancestors O(depth), including on the self category.
These ancestor steps and selector matching work are NOT represented by
compute_nodes/state_entries_visited/revision_entries_created. Zero values in those
counters never mean zero DOM traversal, constant total work or zero allocations.

For A evaluated subjects, C examined direct logical children and P distinct
unchanged parents, agenda storage is O(A), parent staging O(P), and the ordered
queue adds O(log A) operations. Direct child metadata reads are counted even when
a child is not recomputed. Conservative inherited/scoped changes can legitimately
visit an entire affected subtree. Directory/manifest work copies changed paths,
not N entries. Each evaluated node still performs its ordinary cascade, property
and scope work; costs depend on CSS/depth as well as A. Reclamation cost outside
prepare is not established by warmed compute counters.

The bounded journal still overflows after enough unacknowledged incremental
inputs and explicitly resolves FULL. No sustained zero-global-work claim is
possible until a separate incremental ACK protocol and certificate review pass.

## Differential oracle and pseudo identity

The local differential test uses two independent documents/runtimes and prepares
both operations from equivalent previous revisions. It compares compact deltas,
per-entry impacts and complete semantic payloads, not only a later settled FULL.
Only cross-document identity/token/storage bindings are mapped for comparison;
style, scopes, measures, currentColor/dependencies and historical fields remain
strict. Old generations/payload owners are retained and checked. Expanded cases
assert actual text/box-shadow reference metadata, invalid custom-property cycles,
line-height symbolic form with equal resolved values, distinct ordinary/backdrop
scopes and a later dependent change. Background var-to-inherit is covered. The
existing FULL registry does not implement border-color inherit callbacks; tests
preserve that observed behavior and do not claim new CSS support.

Pseudo tests in this cut exercise FULL fallback on both sides. Nodes are mapped
by authentic host plus pseudo slot, and generated content is compared recursively.
Physical node pointers across documents are intentionally different. The existing
empty-agenda category may retain a pseudo identity while legacy FULL regenerates
it; logical style/content equivalence does not assert equality of raw pointers,
retirement counts or generated token lifetimes. No local affected-pseudo gate is
claimed and no pointer/retirement difference is removed from the public API.

## Chromium reference applied

Pinned Chromium revision 2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9:
- element.cc::PropagateInheritedProperties (around5746) excludes pseudo elements,
  variable references/declarations, derived color and other cases that cannot
  use independent inheritance. C3c similarly does not use equal visual color as
  permission to reuse a dependent node: it recomputes with complete metadata,
  conservatively propagates scopes/shadows and rejects affected pseudo coverage.
- element.cc::ApplyComputedStyleDiff (around5805) distinguishes descendant,
  inherited, independent-inherited and pseudo changes. C3c keeps semantic parent
  propagation distinct from paint/layout impact and preserves independently
  scheduled subjects. Aether uses its own complete comparator and masks rather
  than transplanting Blink's property classifications.

The pinned files live in the shared hover research directory. These references
explain the separation and conservative exclusions, not proof of Aether's masks;
the native reader audit and real FULL differential tests supply that evidence.
