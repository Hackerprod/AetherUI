# T1.7: private synthetic contextual layout

This is a test-only integration milestone, **not real font shaping or text rasterization**.
No SDK is added. The public ABI and normal capture eligibility are unchanged.
The context-free `MeasurementSelection::native` path remains rejected (T1.6).

## Implemented boundary

`SyntheticTextTestIssuer` methods are declared internally but implemented and linked
only in `tests/isolated_capture/t17_test_issuer.cpp`. There is no C setter, CLI flag,
environment variable or varying class-definition macro. This prevents accidental
activation; it does not protect against hostile C++ linked into the same process.

`create_fixture_styles` first inspects the entire DOM (including ancestors) with a
strict literal declaration whitelist. Unknown properties, malformed declarations,
CSS comments/escapes/functions/custom properties, and `style`/`link` elements are
rejected without CSS recovery. It then creates a new built-in contextual runtime
with no external sheets, registry, resolver or hooks. Its private authority binds
the exact resulting MapVersion; `install` cannot accept arbitrary external maps.
This is a known synthetic-fixture factory, **not admission of arbitrary CSS**.
In particular missing effective font-style/letter-spacing/text-indent/vertical-align
metadata is not guessed: those source properties are prohibited by the factory,
including inherited declarations. Mutating/replacing the issued map invalidates
the exact-version/epoch authority.

The installed policy binds a unique session token **and engine address**, document
identity, contextual map revision, source/style epochs, issuing thread, permitted
frozen owner records, synthetic source seal, provider and cancellation token.
Layout constructs its own operation-local transaction from pinned actual inputs.
The issuer cannot supply an already-approved paragraph or a `complete=true` claim.
All permitted owners and all source bytes are checked before the first callback.
Epochs and identities are rechecked before and immediately after each callback.
The existing top-level layout reentrancy guard and rollback contract remain in use.

The transaction contains the provider/DOM/style/session state and per-owner cache.
It is cleared on every exit, before the temporary style overlay is destroyed.
Published `ShapedRun` retains only an immutable paragraph, source-map and synthetic
face seal (font bytes/digest/face/size/flags and fixture metadata). There are no
provider/session/DOM references or SDK handles in these published values.

## Deliberately restricted profile

- A block visual owner whose children are only text nodes and comments; no wrappers,
  atomic elements, `br`, pseudo elements or anonymous flex text owners.
- Contextual frozen measures prove horizontal LTR; effective style is `nowrap`,
  400 weight, start/left alignment, and no transform, text shadow/gradient or text transform.
- Literal family `SyntheticTest`, exact fixture font size. Language/script/features
  are **synthetic test configuration**, not a claim of CSS font-face resolution.
- Printable ASCII and U+0020 only. Other whitespace, controls, embedded NUL and
  non-ASCII are rejected before measurement.
- Every nonempty text node in the document must belong to an admitted owner.
  Unsupported siblings cannot trigger a partially measured prefix or legacy fallback.

The full logical sequence is normalized once: trim outer spaces, collapse inner
space sequences across node boundaries. The map stores each input byte and each
empty piece by ordinal, with many-to-one ranges for a collapsed space and zero-length
output ranges for stripped source. Comments are retained as empty pieces. Boundary
anchors inside collapsed spaces point to the start of the emitted space. Provenance
is not caret, selection or geometry for individual source nodes.

## Layout and retained output

The path branches before the legacy item/source-unit collector. Intrinsic width,
intrinsic height and final flow reuse one full-paragraph measurement per operation;
`max_width=0` is unbounded and viewport width does not reshape or wrap it. A normalized
empty paragraph has no provider call, run, placement or fabricated text geometry.

Nonempty output has one text placement owned by the paragraph's block owner.
Hit testing therefore returns that owner, not one of its source text nodes. No
proportional glyph splitting or rectangles are invented for ligature participants.

Result validation requires exact request identity, full range, source/font/paragraph
identity and coherent finite metrics. Synthetic LTR clusters start at zero and are
nondecreasing. Horizontal advances are nonnegative, vertical advances zero, and an
int64 checked sum must equal the logical width exactly in 26.6 units. Bounded offsets
are not counted as advances or claimed ink bounds. Snapshot/preparation revalidates
contextual runs. All labels and fixtures explicitly say synthetic.

## Validation commands

```
sh tests/isolated_capture/build.sh NEW_BUILD_DIRECTORY
sh tests/isolated_capture/run_t17_paragraph_values.sh NEW_BUILD_DIRECTORY
sh tests/isolated_capture/run_t17_context_layout.sh NEW_BUILD_DIRECTORY
```

Run T1/T1.2/T1.3/T1.4/T1.5/T1.6 and H20/H21 regression scripts against the same fresh
objects. `nm -C NEW_BUILD_DIRECTORY/aether_capture` must have no defined
`SyntheticTextTestIssuer::install`; the integration test executable must have it.
The original flexbox fixture remains byte-for-byte unchanged.

Chromium's full-context grouping is the architectural reference, not a code copy:
- https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/layout/inline/inline_node.cc
- https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/platform/fonts/shaping/harfbuzz_shaper.cc

Real shaping, real font parsing, original flexbox PNG, Chromium execution and real
text rasterization remain **NOT_RUN**. Wrapping/caret/bidi/fallback remain unsupported.
