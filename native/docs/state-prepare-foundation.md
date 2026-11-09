# C3a state transaction foundation

Status: implementation checkpoint, not completion of phase C. The state API always
uses FULL. No flag or caller trust bit enables incremental reuse in this checkpoint.

The additive `aether_state_style.h` API returns status independently from compact
semantic/visual delta counts. It accepts a retained base revision and observations
of already-applied state mutations. It never applies stale old/new values to DOM.
Caller declarations are optional and cannot hide journal mutations. A declaration
uses the node identity token and committed old state; mismatches select explicit
FULL fallback. Legacy contextual ABI version and complete change enumeration stay
unchanged. Preview handles retain the candidate's authentic generation, but do not
authorize layout/hit-test before commit.

The B3 persistent directory now stores committed input state separately from style
payloads. An input-only change can replace its directory path while sharing the
style payload and manifest. Legacy imports have no committed-state certificate.
The FULL factory captures authentic node state during its existing complete walk.

Compiled stylesheets, selector AST, rule index and C2 dependency programs have a
single immutable shared owner. Transactions pin that owner in O(1), including after
a stylesheet is removed from the runtime. The pinned state programs remain dormant.

Reconciliation checks contiguous bounded journal coverage from the base, authentic
live token membership, committed old states, intermediate old/new continuity and
final DOM states. It consolidates A→B→A against the committed input. All values and
token pins in the transaction are owned; no iterator into journal storage escapes.
No successful reconciliation alone authorizes a delta certificate.

Journal acknowledgment follows the separately reviewed protocol in
`state-journal-ack-proposal.md`, revision 2. Only an authentic contextual FULL commit
acknowledges, after runtime publication and before diagnostic replay. Prepare,
rejected commit and OOM leave coverage unchanged. A document-wide monotonic floor
bounds storage; older serialized runtimes must use FULL. Independent permanent
monotonicity poison survives ordinary overflow and every FULL. Native document
access still requires external serialization.

Chromium reference (pinned revision 2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9):
`third_party/blink/renderer/core/css/style_engine.cc::PseudoStateChangedForElement`
separates relational invalidation from local state recalculation. The AST programs
are therefore pinned alongside rules, not inferred from the old matching result.
`third_party/blink/renderer/core/dom/element.cc::PropagateInheritedProperties` rejects
blind propagation for variable references/declarations and derived currentColor;
`ApplyComputedStyleDiff` separates descendant/inherited/pseudo changes. C3a does not
pretend that visual impact proves semantic stability: it keeps FULL until the
semantic dependency scheduler and private incremental certificate are complete.

Still required: metadata dependency masks, parent-first agenda, genuine incremental
certification, persistent runtime-state updates, generated identity reconciliation,
indexed planner work accounting, differential shadow/fuzz matrix and independent
validation. No .NET D, hit E, Windows CPU or latency claim is made here.
