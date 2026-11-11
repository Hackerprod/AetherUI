# Real-page native validation

`aether.flexbox-page-390`, `aether.flexbox-page-768` and
`aether.flexbox-page-1440` load the unchanged HTML fixture shipped with
AetherUI.Validator. They parse the complete document, extract its actual style
nodes, resolve CSS for each viewport, lay out the whole page, and build/read the
native display list through the public C ABI.

Run from the repository root:

```sh
cmake -S native -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target aether_flexbox_page_test
ctest --test-dir build -R '^aether.flexbox-page-' --output-on-failure
```

Each run writes `build/flexbox-page/<width>/report.log` and `geometry.csv`.
The log includes wall-clock stage durations and node/rule/box/command counters.
These are single-run diagnostics, not stable performance thresholds or a
benchmark comparison. CSV rows contain the primary box of DOM nodes, with case
IDs, tags and class attributes, for downstream geometric visualizations.
Coordinates are engine border-box coordinates; no screenshot is produced.

Assertions cover:

- The fixture inventory contains 403 case articles, and every expected stable
  ID exists and has finite, nonnegative article geometry.
- All recursively visited layout boxes have finite, nonnegative dimensions.
- Styles resolve and the display list is nonempty and every command is readable.
- Five selected cases (FLEX-001, FLEX-022 through FLEX-025) have a 420px target
  border box, three items in the specified forward/reverse row/column order,
  no main-axis overlap, positive item minimum dimensions, computed `display:flex`,
  computed and used 8px gaps for 022–025, and a common cross-axis origin.

**A passing test does not mean all 403 Flexbox examples conform.** The other
398 cases have no case-specific semantic assertions. Even the five selected
cases cover only the assertions listed above, not their complete browser behavior.
The text callback deliberately uses approximate deterministic UTF-8 codepoint
metrics. It does not shape fonts or reproduce browser/native text measurement.
This test does not run JavaScript, fetch external resources, decode fixture
images, compare with a browser reference, or invoke Direct2D. Geometry plots
must be labeled as native layout diagnostics, never as reference screenshots.

The test exits nonzero on failed assertions or I/O/API errors. Release builds
retain all assertions (no dependency on C `assert` or `NDEBUG`).

`aether.validator-artifact-metrics` runs eight offline pixel-metric unit tests when Python 3 is available. These tests use synthetic RGBA buffers, not renderer output.
