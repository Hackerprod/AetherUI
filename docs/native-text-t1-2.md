# T1.2: retained metrics and private snapshot transport

This is transport infrastructure, not native typography or text raster support.
No provider, SDK, font authentication or successful text PNG is added.

## Layout

`finish_line` obtains ascent/descent and baseline alignment through
`layout_fragment_metrics`. An existing immutable run supplies its original
metrics, after source, relative baseline and font-instance size validation.
There is no callback on this branch. Null run preserves the legacy callback
and its original call sites. CSS line-height is still independent of run height.
The two fragment-building paths already retain `Placement`; no provider selector
is added, so full native layout remains NOT_RUN. Other measurement sites require
an audit before a real provider is enabled.

## Native snapshot and prepare

Sidecars are consumed using original raw command indices before border
compaction or opacity insertion. Each private Text owns its run strongly and
carries the absolute baseline established by the display-list builder. That
baseline is in pre-transform/pre-scroll coordinates; snapshot/prepare never add
those offsets to it. Native text entries must be ordered, unique, in bounds,
exactly match DRAW_TEXT, and cannot lie within compacted border ranges.
Mutable snapshots are revalidated in `prepare`, including source, size, baseline,
resource completeness and cumulative budgets. Font budget is 64 MiB of unique
FontInstance objects by pointer identity; glyph budget is 1M per frame. Repeated
font references do not duplicate their byte charge. `source_hash` remains
unverified identity metadata, not proof of font contents or family/weight.

Value-returning snapshot and preparation keep prior outputs untouched on OOM or
cancellation. Cancellation is checked after opacity insertion before delivery.
Legacy C snapshots carry no native resources and do not reconstruct identity.
Public C ABI is unchanged. Software text raster remains rejected.

## Validation scope

Transport tests use synthetic resource bytes and fabricated metrics; they prove
identity, ownership and bounds only. They do not validate real fonts, shaping,
kerning, line breaking, bitmap sizes or visual fidelity. The original flexbox
fixture is not edited. Chromium comparison and provider typography stay NOT_RUN.

Reference consulted for architectural direction only: Blink's
[text_fragment_painter.cc](https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/paint/text_fragment_painter.cc)
uses retained TextShapeResult/TextPaintInfo and a font-ascent-based origin.
No claims of matching Blink rounding/writing/selection behavior are made.

## Recorded acceptance

Manual clean C++ build and focused source recompilation passed. No Actions ran.

| Suite | Result |
|---|---|
| T1.2 fragment consumption | 16 checks; 2 callbacks, both legacy |
| T1.2 snapshot/preparation | 237 checks |
| Independent T1.2 adversarial review | 162 checks; PASS |
| T1.1 transport regression | 37 checks |
| H20 layout | 4,857 checks; OOM 45 |
| H20 CLI/PNG geometry gate | 1,219 checks |
| H21 layout | 28,802 checks; 1,200 coordinate scenes; OOM 96 |
| H21 style | 1,475 checks; OOM 222 |
| H21 capture | 46 checks |
| Flex text sequences | 90,378 checks |
| Legacy text alignment and inline tokenization | PASS |

Original `flexbox_runtime_validation.html` remains 474495 bytes, SHA-256
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
Fresh capture remained REJECTED/PREFLIGHT_UNSUPPORTED, no PNG. Its diagnostic
explicitly includes TEXT_RENDERER_UNAVAILABLE and chromium=NOT_RUN.
The 111.485 ms process wall / 0.108895 s CPU / 10256 KiB RSS recorded for that
run measure rejection/preflight, **not text rendering performance**.
