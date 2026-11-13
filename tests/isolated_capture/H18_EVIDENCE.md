# H18: bounded HTML display:contents in flex

Local candidate based on H17. No Actions or remote publication was run.

The native display enum appends Contents=9 after TableCell=8; existing numbers
and public struct layouts do not change. The .NET enum is extended likewise.
The native display parser owns the keyword. The capture gate does not rewrite
DOM or flatten source CSS.

Flex collection expands box-less wrappers in document order, then applies the
existing stable child order sort. Generated box children are the same tree used
by painting, preparation and hit testing. DOM parents, selectors, inheritance,
and structural pseudo-classes remain unchanged. Wrapper decoration, dimensions,
margins and order generate no box and have no effect on the promoted items.
The public primary-box query returns null for an admitted contents wrapper;
there is no synthetic rectangular wrapper hit target.

This is a deliberately bounded subset, not complete display:contents support.
HTML div/span/section/article/main/aside/nav/header/footer/figure/figcaption
wrappers are admitted under an effective flex parent, including nested/empty
wrappers. Non-whitespace direct text, generated content, replaced/table/SVG
contexts, positioned/effect/clipping interactions are rejected. Capture children
must be block/flex/contents or none. Native layout additionally accepts inline
and inline-block HTML children as flex items; capture does not claim these cases.
Native limits are ancestry/expansion depth64 and 50000 inspected links/nodes;
capture is stricter at depth32, 512 wrappers and 50000 expansion visits. These
limits may reject larger valid documents rather than silently render them wrong.
Native hidden contents subtrees are suppressed; the capture profile can reject
more conservatively. Native validation checks resolved styles before layout.

## Verification

- Clean C++20/O1 rebuild: 71 core translation units and 8 support units.
- H18 native: **713 checks**, **55 layout allocation-failure points**, PASS.
  Nested/empty promotion, stable order, row/column/reverse/wrap, original DOM
  selectors/inheritance, geometry/hits and absence of wrapper boxes tested.
  block/contents/none changes, moves across parents, insertion/removal compare
  retained runtime with newly created full resolution. Unsupported layout
  contexts preserve an existing result on failure. Owning style clone and box
  tree remain usable after runtime/map destruction; enum values asserted.
- Gate allocation failures: **3**, PASS.
- H18 CLI: **53 checks**, PASS: 26 captures in 13 exact native-control PNG pairs
  and 14 negative cases, including depth33 and 513 wrappers. Negative cases
  publish no PNG. Basic image inspected: promoted blue boxes separated by gaps,
  no wrapper decoration. Visibility and DOM-child/first-of-type selectors tested.
- Native H3–H17 regression scripts PASS, including lifetime/OOM suites.
- CLI H3,H5–H11,H13–H17 PASS; H4 exact pixel-boundary oracle PASS.
- git diff --check PASS.

Commands:
```
sh tests/isolated_capture/build.sh /tmp/h18-clean-20261113
sh tests/isolated_capture/run_h18_core.sh /tmp/h18-clean-20261113
python3 tests/isolated_capture/test_h18.py /tmp/h18-clean-20261113/aether_capture /tmp/h18-cli-clean-gate-limits
```

Local logs: /tmp/h18-final-core.log, /tmp/h18-native-regression.log,
/tmp/h18-cli-regression.log. Basic capture process wall10.34ms, child CPU5.98ms,
peak RSS3692KiB; single local sample, not a comparative performance benchmark.

The original fixture remains unchanged, SHA256
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
It remains REJECTED, no PNG: first global function diagnostic [21716,21728),
canvas analysis failure [6936,61791). Diagnostics are partial, not a complete
blocker inventory. Chromium was not executed; no full visual/performance parity
is claimed. Independent code review is required before publication.
