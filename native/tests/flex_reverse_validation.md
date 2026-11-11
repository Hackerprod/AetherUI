# Flex direction geometry

The native layout supports `row-reverse` and `column-reverse` placement in
horizontal-tb/LTR, with `nowrap` and normal `wrap`. Source order, line membership,
flex sizing and descendant contents remain unchanged. Reverse changes the
physical placement of each item's **margin box**, not the DOM or paint/hit order.

## Representation and compatibility

The public C style struct and exported signatures are unchanged. Direction
values remain `ROW=0`, `COLUMN=1`, with `ROW_REVERSE=2` and `COLUMN_REVERSE=3`.
The .NET enum follows those explicit values; JavaScript retains numeric values.

Historical `AETHER_JUSTIFY_START=0` and `AETHER_JUSTIFY_END=2` mean flex-start
and flex-end; the new aliases `FLEX_START` and `FLEX_END` make that explicit.
CSS `start` and `end` now produce `LOGICAL_START=4` and `LOGICAL_END=5`.
This is an intentional change in their computed numeric values. Forward geometry
is equivalent; reverse logical start/end differ from flex-start/flex-end.
The initial value stays flex-start, including initial/unset. Explicit inherit
copies the parent's computed direction or alignment without making either
property implicitly inherited.

No field, offset, capability bit or ABI version is added. Linux x86-64 checks
must not be presented as binary validation of the Windows ABI.

## Completion contract

`layout_flex_children` forms and sizes lines in source order, completes stretch,
and returns the natural height together with a private placement plan.
The plan moves the existing box/line vectors and holds one cross-size per line.
It owns no DOM, snapshot or layout box. The regular element completion computes
the final content box using the existing explicit/min/max-height rules and then
places the items before positioned descendants, scroll metrics and the parent's
relative offset. The same continuation can be moved through table-cell
completion; it never survives publication of a layout result.

For a distance U from flex main-start and an outer item size O in final content
extent M, reverse placement is `content_origin + M - U - O`. Signed physical
margins are retained. Auto margins consume positive free space before alignment.
Negative free space remains negative, except distributed space-between gaps.
Column auto/min/max-height uses this final numeric extent for placement, never
as proof that percentage heights or flex-basis became definite. Flex sizing
under an indefinite main size is not rerun after a min/max-height clamp.

## Tests

Build and run the release-active regression tests:

```sh
cmake -S native -B build -DCMAKE_BUILD_TYPE=Release -DAETHERUI_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`aether.flex-reverse` checks public legacy and prepared paths with independent
numeric expectations, overflow, single-item fallbacks, normal wrap grouping,
physical/auto margins, final column height, parser/default/inheritance values,
positioned children, paint/hit precedence, retained results and ABI constants.
The related flex resolver, sizing, stretch, lifetime, contextual and ABI tests
remain regression gates. JavaScript's real-native `flex-reverse-values` scenario
checks exported values and retained maps/copies; .NET has a corresponding test.
Use `AETHERUI_TEST_NATIVE=1` and `AETHERUI_TEST_LIBRARY` to select the actual
candidate library for the JavaScript tests.

The external validator's unchanged 403-article HTML can also be run at widths
390, 768 and 1440. Its five selected semantic cases are only a smoke test: a pass
is not conformance of all articles, a browser comparison or a raster/Direct2D
render. Reverse cases must check exact direction values and their anchor against
the final content end, not just decreasing coordinates.

## Remaining work

- `wrap-reverse` still shares the legacy boolean wrap representation. It needs a
  separate versioned state/ABI design, not an extra truthy boolean value.
- Reverse scrolling needs signed ranges and a consistent origin across layout,
  paint, hit testing, input and scrollbar APIs. This patch does not claim
  conforming reverse scrolling for auto/scroll/hidden. Visible/clip geometry is
  independently testable.
- RTL and vertical writing modes, `order`, complete `flex-flow`, baseline,
  align-content and space-around/space-evenly distribution remain outside this
  change. Existing fallback parsing is not evidence of conformance.
- No Chromium execution or user-computer validation is part of these checks.
- A pre-existing sizing limitation remains: a column with internal forced height
  100 marked indefinite can resolve a child's `height:50%` as 50 rather than its
  natural 20. Forward baseline and reverse candidate agree on that size. The
  reverse placement correctly uses the final 100 extent, but does not repair the
  existing percentage/flex-sizing definiteness path. The forced-height tests
  separately cover row cross-axis percentages and fixed-size column children.
