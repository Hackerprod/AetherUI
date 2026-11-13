# H16: bounded, source-preserving preflight diagnostics

The new additive `diagnostic_v1` records observe actual rejected checks, separately
from the existing inventory. Existing blocker/reason/status keys and admission
rules are unchanged. Coverage is always explicitly partial: the first failure
of each executed check is not an exhaustive blocker inventory.

Coordinates are half-open byte ranges in the original DOM-extracted CSS source,
not HTML offsets or Unicode character indexes. Inline source IDs are DOM
ordinals; style blocks use their existing ordinal IDs. H11 returns an optional
fixed-capacity map of retained segments, retaining byte provenance even when an
earlier identical rule was omitted. No substring search recovers locations.
Unmappable locations remain UNKNOWN. H5 declaration spans preserve selector
ranges through nested conditions/keyframes, and failed analysis retains its
current structural/declaration range rather than inventing a property.

Records distinguish profile rejection, unclassified analysis failure, known
lexer failure and known work exhaustion. A profile failure does not prove the
engine lacks an implementation. No incomplete syntax analysis is called a
success or exhaustive coverage. Unsupported lexical tokens have token ranges;
property is UNKNOWN unless an identifier+colon is directly evidenced at the
failure start. Selector identity is its original range and SHA-256, deliberately
not raw attribute contents. Values, resource URLs and filesystem paths are not
added. A FLEX identifier is not inferred from hashes or partial selector text.

Diagnostic output is capped at 256 records, source IDs at 64 bytes and property
names at 80 bytes. Custom property names are redacted. Control/non-ASCII bytes
are escaped. Each record is prepared before writing: allocation failure emits
an explicit incomplete marker, without replacing the original capture failure.
The parent capture catch marks memory failures separately. The fixed H11 map
adds no heap allocations. New diagnostic source bookkeeping and failure spans
are observation only; no partial image can become confirmed because of them.

Tests:
```
sh tests/isolated_capture/build.sh /tmp/h16-clean-20261112
sh tests/isolated_capture/run_h16_core.sh /tmp/h16-clean-20261112
python3 tests/isolated_capture/test_h16.py /tmp/h16-clean-20261112/aether_capture /tmp/h16-verified2-pixels
```

Clean build: 71 core + 8 extra translation units. CLI entrypoint rebuilt after
last metadata adjustment. H16 core: 12 checks and 64 allocation probes;
integration: 33 checks across nine captures, covering UTF-8, repeated sources,
H11 omission before failure, nested rules, lists, inline, unsupported functions,
malformed declarations and unchanged admission of absent rules. Diagnostics
allocation failures are explicitly marked; private CSS values are absent from
new records. All native H3-H15 and prior CLI suites pass. A separate differential run against the H15 binary preserves rejection/status/blocker lists and positive PNG bytes for all nine H16 fixtures. Record-cap and keyframe-selector provenance checks also pass. Regression logs:
`/tmp/h16-native-final.log`, `/tmp/h16-cli-final.log`.

The original SHA-256 remains
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
It remains rejected. First recorded global unsupported function is in byte range
[21716,21728); the canvas scan identifies structural range [4295,4353), the
`section.test-section:first-of-type` rule, rather than implying all inventoried
functions caused this specific failure. These are extracted-CSS coordinates.
The engine's actual CSS support remains unverified; Chromium was not executed.
