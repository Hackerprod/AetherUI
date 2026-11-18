# B2 source margin translation

B2 already admits nonnegative block margins. Previously, two paragraphs with
`white-space:normal;margin-bottom:16px` failed source creation with
`INTERNAL / LAYOUT / INVARIANT`: the second inline paragraph was validated at
`y=24`, then sibling margin collapse moved its boxes to `y=40`, while the
provenance receipt still expected `y=24`. This is a provenance/composition fix,
not new CSS admission, a margin-collapse algorithm change, or B3 support.

## Construction contract

`offset_box` starts a monotonically numbered translation operation. Its existing
subtree traversal notifies the private B2 sink for each final, non-control text
box, before moving it, using the effective per-node delta. Provisional layouts
(`records_boxes == false`) and zero moves do not emit receipts. The sink is
installed only during private final layout construction; an RAII scope clears it
before publication, hit-order caching, and on error. Snapshots never inherit it.

The session retains each independently validated inline rectangle, occurrence,
owner, frozen record, context, dimensions and baseline. It accepts only the
current layout/session/operation, finalized contexts, matching owner/style/run,
finite data and an exact previous rectangle/baseline. Each run may appear only
once per operation. Expected x/y use the same ordered float additions as the
boxes, not the final rectangle or a recomputed owner anchor. Overflow, reordered
or repeated receipts and unnotified movements remain invariant failures.

The original ledger's lines, ranges, wrapping, glyphs, font and placements stay
unchanged. Final validation still demands exactly one matching box per run with
exact x/y, width/height and baseline, correct owner/style/text. No epsilon,
clamping, nowrap fallback or weakened comparison was added. Controls retain
existing atomic presentation, clipping and password ownership.

## Cost and failure semantics

Translation uses the existing run map, not a scan over all runs or sources.
The hot path checks document/style epochs and the participating retained owner
in constant time; it deliberately does **not** call the full source-scanning
`valid()`. Complete validation remains at collection/finalization boundaries.
Each operation, each visited node and each text receipt charges the existing
65,536 `token_visits` quota; repeated ancestor traversals are charged each time.
There is no new growing translation history or allocation on a receipt.

The larger `FinalRun` records are charged through their existing PMR map.
Fixed session/layout linkage, operation counter and sink vptr reserve 32 bytes
(on this 64-bit build) within the same 4 MiB B2 budget. B2-disabled layouts have
no sink, provenance allocation or additional traversal. The one result pointer
is always default-null. This does not claim a bound for historical layout/DOM
storage outside the B2 budget.

Failure abandons the private build, not a partly published handle/frame. OOM
and budget exhaustion retain their distinct statuses. Private tests exercise
failures before and after partial receipt application and the actual quota
boundary, then successful fresh sessions. The public allocator-interposition
suite exhausts real C++ allocation sites and checks live-allocation balance.

## Reproduce and test

Configure `native` with `AETHERUI_BUILD_TESTS=ON`. When testing optimized builds,
keep assertions enabled (`-DCMAKE_CXX_FLAGS_RELEASE='-O3 -UNDEBUG'` and the
corresponding C flag). Build the targets before running CTest:

- `aether_b2_margin_geometry_test`: independent box/text coordinate oracles,
  fractions including .1/.2/.3, parent/sibling collapse and barriers, repeated
  occurrences, split inline contexts, controls, snapshot sink lifetime, both
  viewports, and the original complete page.
- `aether_b2_margin_failures_test`: receipt identity/order/geometry corruption,
  late events, absent/duplicate runs, OOM/real quota failures with partial
  progress, and clean-session recovery.
- `aether_b0_source_oom_test` with CTest `aether.b2-margin-oom`: exhaustive real
  allocation failures in the new margin fixture, plus existing B0/B1/B2 tests.

`native/tests/fixtures/b0/source_margin_boxes.html` is the exact original full
margin page, retaining its radii and real inputs/button. The previous
`source_rounded_boxes.html` remains unchanged. Use installed `b0-source` capture
at 320x640 and 480x640 with the versioned regular/bold DejaVu fonts. Independent
C11 create/render/copy/destroy and PNG byte comparison test transport; they do
not replace the coordinate oracle or establish browser parity.

This fix does not close the broader audit, implement historical login behavior,
or claim general HTML, interactive source, additional DPI coverage or B3.
