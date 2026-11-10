# Homogeneous inline source units

## Admission

Final inline layout, intrinsic height, and intrinsic min/max width use one
side-effect-free admission predicate before text measurement. The containing
inline formatting context and every retained inline ancestor must share one
white-space mode: normal, pre-line, nowrap, or pre. Inline ancestors are retained
as admission evidence even when empty or without direct text.

The initial domain is horizontal LTR text containing only ASCII letters,
digits, U+0020, CR, and LF. Transparent nested spans may vary font, font size,
weight, text transform, color, and measured baseline. BR remains a forced-break
marker. Inline decorations, positioning, transforms, interior blocks, atomic
items, mixed modes, punctuation, non-ASCII text, and tabs/VT/FF select the
complete previous path. No measured prefix can later switch paths.

Pre-wrap remains on its existing hanging-space path, without broadening that
path's admission. Its existing intrinsic min-content shortcomings are not
addressed by this change.

## Source and measurement contract

A word can own several source fragments. Span boundaries, comments, and empty
spans never create soft-break opportunities. Each fragment retains its owner,
style snapshot, and original callback slice. Fragment widths are added to decide
whether the whole unit fits; text from different source items is never joined
for measurement, and closing or moving a line never remeasures its fragments.

Normal and pre-line collapse whitespace continuously across source items.
A pending collapsed space retains the first contributing owner and is discarded
at a line edge. A soft break is permitted only at an actual collapsed space,
before the next entire word. Pre-line source newlines force a line break.
Nowrap uses the same collapse without soft wrapping. Pre preserves all spaces,
including leading, trailing, and whitespace-only text, without soft wrapping.
CRLF is one source newline even across transparent or empty inline spans.
BR explicitly interrupts that source-newline pairing.

Previously discarded whitespace-only direct text items remain available to the
new path. They and new admission-only markers are skipped by all old consumers;
they cannot independently create a legacy paragraph or consume a pending block
margin.

## Intrinsic sizing and failures

All four consumers use the same prepared source units. Normal/pre-line
min-content is the widest complete word. Their max-content is the widest forced
line including internal collapsed spaces. Nowrap and pre have equal min/max
contributions: the widest forced segment, with pre counting every preserved
space. Intrinsic widths do not depend on available layout width.

Prepared units, pending separators, and measured fragments belong to the current
operation. Width and height sums are checked for finiteness. Allocation failure
uses the existing layout transaction: no partial result is published, retained
geometry/paint remain valid, and an uninjected retry is permitted.

## Verification

The inline-units ABI, private intrinsic, and allocation-failure tests cover
split words, actual separating text nodes, transparent topology, CR/LF/CRLF,
forced breaks, all four modes, owner/paint/hit geometry, callbacks, baseline
envelopes, flex/table consumers, map producers, unsupported-domain fallback, and
retained-result/retry behavior. The pre-existing capitalization intrinsic tests
now expect whole-word widths when their formatting context is eligible.
