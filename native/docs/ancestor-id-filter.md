# Bounded negative ancestor-ID filter

Experimental checkpoint. Semantic, allocation, sanitizer, independent review and
fixed two-campaign performance gates must all pass before accepting an optimization.
The experiment concerns Linux native style resolution with synthetic text metrics;
it makes no browser, Windows, Direct2D or full CSS-conformance claim.

## Admission and lifetime

The reviewed experimental activation opted in only from the preparation-local
matching context when the captured original diagnostic callback was null. Current
normal preparation is disabled as recorded below. Private opt-in tests preserve
the design; default contexts and generic matching retain the existing behavior.
All experimental scratch dies before freeze, commit and callback delivery.
There are no persistent node maps, borrowed ID strings or revision epochs.

Admission inspects the original subject before generated-subject normalization:
an ordinary DOM element, no selector pseudo-element, exactly two steps with
`descendant` then `none`, a left compound containing exactly one nonempty decoded
ID and no other conditions, and a right compound with no pseudos. All other forms
use the original path. The right compound is evaluated once. Failure does not
change the current inventory subject or perform any inventory/ancestor work.

After a successful right compound, a subject change releases the previous copied
IDs and resets state to unseen. A → B → A does not reuse A's first inventory.

- Unseen: perform the original ancestor continuation once. Positive results keep
  unseen state and never collect/copy/allocate new scratch. After a complete
  negative search, attempt one bounded collection and return the already-proved
  false result without a second continuation, including collection cap/OOM.
- Complete: an absent required ID proves false. A present ID still invokes the
  original continuation once, without reevaluating the right compound.
- Unavailable: always use the original continuation; never retry collection for
  this subject.

Collection follows the exact existing `dom_parent` boundary and accepts IDs only
from `is_selector_dom_element` nodes. It excludes the subject itself. Document
links count but provide no IDs. Duplicates remain occurrences, IDs are compared
bytewise, and copies own their storage. ID and ordinary parent mutations between
preparations are observed by a fresh context.

## Bounds and failure behavior

Collection visits at most 128 parent links, copies at most 64 nonempty ID
occurrences and at most 4096 ID content bytes. Exactly reaching a cap is complete
only when the permitted walk also ends. Capacity is at most 64 vector slots and
at most 8192 conservative string-capacity bytes (sum of capacity plus one).
Incomplete state cannot reject. On a cap or physical-capacity failure, all copied
scratch is discarded and state becomes unavailable. Only scratch allocation is
inside the `std::bad_alloc` catch; ordinary matcher errors are not converted to
false.

The reviewed allocation gates require context growth at most 128 bytes, requested
retained scratch at most R = 128 + 64 × sizeof(std::string) + 8192 bytes, and
transient requested-live scratch at most 2R. Allocator overhead is separate.
Counted fixture builds, not RSS or instrumented timings, verify these gates.

## Evaluation contract

The original full-validator HTML and harness are unchanged. Equivalence must
cover complete public computed styles (including nested strings/arrays), active
public display commands and existing assertions/counts/geometry at 1440, 768 and
390 pixel widths. Field values are compared, never struct padding or pointers.
Focused matcher tests compare against the original generic matcher. Callback and
ordinary mutation controls must preserve behavior. Scratch-only allocation fault
injection uses small fixtures, not a global validator sweep.

The frozen v3 protocol and its fixed S/Q inputs are under
`tests/ancestor_id_filter/`. It separates cold public pipeline, warm resolver,
small public controls and private fresh-context/single-negative operations.
There are exactly two campaigns, 1080 measured processes and 192 excluded,
retained process warmups. Required gates are evaluated per campaign, with no
pooling, trimming, adaptive extension or replacement attempts. A failed, missing
or inconclusive gate is NO PASS. No speedup is implied by the implementation.

## Recorded checkpoint, 2026-11-11

The ordinary focused matcher, bounded allocation-failure controls, public
ID/reparent tests and nonmutating callback guard passed. Context size on the
measured 64-bit GCC/libstdc++ ABI is 184 bytes (56 bytes above baseline).
Complete public A/B style/display outputs matched at all three viewports;
headers and 192 defined dynamic symbols remained identical, and installed C/C++
consumers passed. These are limited regression results, not conformance claims.

A separate counted run of the unchanged 1440 fixture observed 652080 admitted
queries and exactly 652080 right-compound evaluations, 4173 original ancestor
searches / 20712 original links, 1865 collection attempts / 14518 collection links
and 3514 copied ID occurrences. Output geometry and existing checks matched the
reference. These operation counts are diagnostic and establish no elapsed-time
improvement.

The frozen A/B campaigns are **NOT RUN**. The required sanitizer gate remains
pending independent verification because the reported remote evidence could not
be materialized within the allowed access boundary. Neither that gate nor global
acceptance is PASS. The admission guard remains unchanged; no readiness flag is
asserted merely to run the campaigns. This checkpoint preserves the experiment
and reproducible tooling without accepting it as a validated optimization.

## Activation restored to baseline

Normal public preparation no longer enables the experimental filter, regardless
of callback presence. Its context is default-constructed with the filter disabled,
so every public selector call takes the original normalization/matching path.
No ancestor inventory is constructed, no ID is copied and no negative lookup is
short-circuited by this experiment in normal runtime use.

The implementation and focused tests remain as a private opt-in experiment for
reproducibility. On the measured ABI its dormant context still has a 56-byte
additional stack footprint and the matcher checks a disabled boolean. There is
no new ancestor scratch allocation. This restoration is of activation/behavior,
not a claim of byte-identical binaries or zero residual instruction/stack cost.
The experiment remains globally NO PASS, its performance campaigns NOT RUN and
its remote sanitizer result unaudited independently. Historical results above
refer to the explicitly enabled experimental checkpoint, not this restored state.
