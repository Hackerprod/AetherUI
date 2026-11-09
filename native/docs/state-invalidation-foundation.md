# C2: dormant AST state invalidation foundation

This implements DESIGN.md §5's compiler/planner foundation, **not a production
incremental resolver or the completion of phase C**. No resolver, matcher, parser,
DOM, state journal, style revision, .NET host or public ABI behavior is changed.
The new private module is linked but has no production caller. Full matching and
full resolution remain authoritative. No performance improvement is claimed.

## Private contract

`compile(rule_id, selector)` consumes the authentic compiled selector AST and owns
its output. It never reads selector source strings, selector anchors, specificity
or SELECTOR_DEP flags. Each state occurrence records its complete bit mask, direct
required/forbidden polarity, enclosing logical-not parity, safe source filter,
route, coverage and fallback reason. Logical selector lists become separate
occurrences; no dependency is derived from the current match result.

`plan(root, programs, changes)` is a synchronous **reference** planner over a stable,
consistent logical DOM. The root must contain the relevant changed DOM elements
and candidate subjects (normally the document). Each change supplies bits changed
between committed and input states; it does not derive or certify that delta.
Programs, nodes and topology must remain valid during the call. Returned candidates
retain authentic identity tokens but their host pointers are borrowed: future
consumers must validate liveness/ownership, tree membership, epochs and revision
provenance before dereferencing or committing. This is not a certificate or a
thread-safe snapshot API.

Generated and layout-only nodes, and their entire subtrees, are excluded from input
and traversal. Before/after selectors return the real host plus the output slot,
even if that slot currently has no generated storage. Slots are never DOM siblings.
Candidate deduplication uses token + slot and unions affected rule IDs across
occurrences and input changes. This is a selector agenda only; it does not perform
inheritance propagation or guarantee parent-before-child scheduling.

No current state, full matcher, dynamic predicate, media condition or prior winner
is consulted to filter candidates. Media activation and cascade remain final
resolver concerns. Allocation errors propagate without returning a successful
partial agenda or mutating DOM/runtime; no publishing API is involved.

## Covered AST and proof of overapproximation

For a state-only input change, a selector truth change must contain a required or
forbidden state occurrence whose truth changed. Its mask therefore intersects the
changed bits. At least one old/new successful compound evaluation witnesses every
static conjunct along the affected matching path. The compiler retains only direct
tag, all IDs and all classes from each such compound. These are necessary tests.
Attributes and structural pseudos are conservatively ignored, as are all dynamic
pseudos. Tag comparison and class token semantics use the matcher's shared helpers.

AST steps are right-to-left. From an occurrence at step i, reverse each matching
edge down to step zero: child -> children; descendant -> descendants; previous
adjacent -> next logical sibling; previous general -> following logical siblings.
Then append the enclosing continuation. Self is a zero-distance filtered hop.
This covers local, child/descendant, adjacent/general siblings and arbitrary tails.

For :is/:where/:not, a truth change of the enclosing logical expression implies a
truth change in at least one nested selector at the same subject. Recursively
compose its occurrence-to-subject route with that subject's outer continuation.
Negation changes boolean polarity, **not** navigation direction or necessary
conjuncts within the changed nested selector. A negated branch's classes are never
hoisted as positive constraints onto its outer subject. Other branches never
contribute filters to the changed branch. Empty forgiving lists have no occurrences.

For :has, the existing matcher searches nested-selector **subjects** among the
anchor's strict logical descendants and separately enforces the leftmost compound's
relative-anchor condition. First find possible nested subjects using the complete
nested occurrence route, then invert the search edge to their strict ancestors,
then append the anchor's outer continuation. This deliberately ignores the extra
leftmost-anchor restriction, so it can overinclude but cannot miss an anchor.
Crucially, this is not merely ancestors of the changed element: nested :is/:not
can refer to a changed ancestor outside the anchor's subtree. Recursive route
composition preserves these cases, all combinators in current :has arguments,
nested :has and all logical list/polarity combinations. Leading relative `>`, `+`
and `~` syntax is not added; the parser's current acceptance remains unchanged.

`proven_superset` means the navigation composition is justified, **not** that the
candidate set is exact or that every candidate's style changes. Static filters are
necessary only, left-of-occurrence constraints are ignored, structural/attribute
conditions are ignored, and :has's extra anchor constraint is ignored. The existing
full matcher/cascade decides the final truth and winning declarations.

## Explicit per-occurrence fallbacks

- `route_budget`: more than 128 route hops, including nested continuations.
  Compilation continues walking the AST iteratively to retain every occurrence,
  mask and direct safe source filter. It does not silently drop deeper state bits.
- `unsupported_ast`: a nonrepresentable combinator on a needed route, or a future
  unknown pseudo kind. Unknown pseudo semantics conservatively get all state bits
  and a universal source filter; known state occurrences retain their own masks
  and safe local filters. Unsupported AST tests use internal AST seams, not new CSS.

Fallback candidates are all logical DOM elements in the supplied root, but **only
for the affected occurrence/rule/output slot**, after mask and proven source-filter
checks. A complex rule does not convert unrelated rules/states into full fallback.
The fallback is broader than relational navigation, not a claim that the entire
stylesheet or resolver should use this scope. The reference planner lazily collects
that full list once per call only when a fallback actually fires.

## Deliberately deferred work

No candidate index, maintained sibling/depth links, stylesheet integration,
state-ledger reconciliation, phase-C transaction certificate, propagation scheduler,
or runtime feature flag is introduced. Reference sibling navigation can scan a
parent's children; ancestor/root-membership checks and per-occurrence vectors may
repeat work. :has inversion may visit nested subjects before walking ancestors.
Those costs must be addressed and measured before production integration. The
compiled metadata must be cached with stylesheet identity rather than rebuilt on
each hover. Production incremental work still requires the rest of the approved
pipeline and its differential/lifetime/rollback gates.

## Validation

`aether.state-invalidation` is a standalone test of the real parser, matcher and
new compiler/planner. Checks remain active under NDEBUG. It includes structural
small-agenda assertions (self only, child miss, children vs descendants, next
logical sibling, general-sibling tails and unrelated-subtree exclusion), both
polarity kinds, every supported state name, lists/nested logicals, :has composition,
generated host/slot handling, generated/anonymous subtree pruning, duplicate
batches/rules, authentic token retention, absent-current-match rules and unrelated
state/rule filters. Long routes/deep logical nesting and unknown internal AST cases
exercise both fallback reasons.

The differential matrix toggles every relevant bit in both directions, also a
multi-bit mask, and checks every old/new truth change against the agenda. It verifies
that planning before and after mutation yields the same set. The test overwrites
AST source strings and clears legacy dependency flags before compiling. Fixed-seed
fuzz (0xC2A37) builds a 32-element varied DOM and 180 selectors with nested lists,
all current combinators, states and periodic generated slots. It compares against
the unchanged matcher. Synthetic descendants under generated/layout-only nodes are
not logical DOM subjects, even though invoking the matcher directly on arbitrary
storage can match a local compound; the differential domain excludes those nodes.

See the external implementation report for actual run counts, build configuration,
sanitizer limitations and the full-suite result. This finite corpus is evidence,
not a proof covering arbitrary malformed input or a performance benchmark.
