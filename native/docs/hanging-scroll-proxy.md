# Hanging text scroll contributions

The admitted ASCII/PRE_WRAP inline path keeps physical boxes and paint/hit regions intact. On a line with hanging width H > 0, each text fragment contributes a private closed rectangle to scroll accumulation. Its horizontal edges are clamped to [L, L + A + actual justification advance]; the interval is not clamped to the containing width. Atomic fragments and fallback lines retain their prior contributions.

A fully hanging fragment contributes a vertical segment, not an empty rectangle. Both legacy and contextual accumulators read the same own-contribution helper. Owner clipping precedes affine projection; equality at a clip edge remains nonempty. Existing fixed-container handling and conservative unions of transformed and untransformed bounds remain unchanged.

## Numeric representation

Each box has a 48-byte inline proxy containing an original float content origin, four double edges, and validity. The reconstructed edges add the observable movement of the current float content origin relative to that original origin. Readers and contextual finalization never reanchor or mutate it. This avoids losing a small retained edge by storing a difference from a huge physical fragment position. No extra allocation or callback is introduced by the proxy.

The approved numeric amendment computes alignment width A from the retained prefix instead of subtracting H from P. At a soft wrap, A is the prefix advance before the contiguous PRE_WRAP whitespace suffix. At a forced or paragraph end, A is max(prefix, min(P, available width)). Without that suffix, A remains P. Physical P and wrapping/measurement order are unchanged. This fixes extreme-tail alignment (for example, word width20 followed by a tail of1e38), so physical alignment in those previously inaccurate extreme cases is intentionally corrected. Ordinary layout, text metrics and painting remain covered by unchanged geometry expectations.

This is not arbitrary precision layout: accumulated positions and observable translations still use float geometry; sub-ULP movement may be lost there. Overflow projection validates finite, float-representable edges and retains transaction rollback on rejection.

## Lifetime and mutation audit

New boxes start invalid. Snapshot cloning explicitly copies the proxy. offset_box moves the physical content origin without modifying the reference. Text width expansion in the legacy justification helper explicitly invalidates any proxy; the admitted hanging path computes justification before proxy emission and never calls that mutator. Other content-shape assignments in layout completion and deferred table placement operate on element boxes, not emitted text. Table/flex/relative placement translates text through offset_box. Rendering and hit testing continue to read the original content/border boxes.

## Validation

The scroll regression exercises soft/forced/EOF endings, whole and partial hanging spans, baseline heights, zero-size closed segments, clipped and disjoint bounds, independent four-corner affine oracles, nested transforms, relative/flex/table placement, scroll input clamping, snapshot clones, numeric rollback, OOM clone failures and repeated animation finalization on the same snapshot. Real callback-driven huge tails cover 1e9 and1e38, one/two spans, three alignments and both layout domains. Resolver tests cover width0, exact fit, one-pixel excess, genuine word overflow, and PRE/nowrap/atomic barriers.

On x86-64 GCC13 Release, sizeof(aether_layout_box_s) changes from1616 to1664 bytes (+48, approximately2.97%); every box pays this inline cost, including boxes without a valid proxy. Runtime timing and allocation comparisons must use matching compiler flags and an idle host.

References: CSS Text3 hanging whitespace (https://www.w3.org/TR/css-text-3/#hanging), CSS Overflow3 scrollable overflow (https://www.w3.org/TR/css-overflow-3/#scrollable), and Blink ScrollableOverflowCalculator::AdjustOverflowForHanging/AddItemsInternal. This restricted path does not claim general Chromium/RTL equivalence or minimal polygonal unions.
