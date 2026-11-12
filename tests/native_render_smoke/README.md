# Native rendering smoke acceptance (R5)

Two separate tests, NOT text integration into R2:
- geometry-v1:64x48, DPI1, white background, red rectangle and clipped translated
  blue rectangle. R1 preparation -> R2 Cairo -> R0 confirmed frame.
- text-v1:160x48, ASCII Aether123 with space, sans-serif16, nowrap physical LEFT.
  R3 request -> R4 Pango owner -> metrics/draw of same layout in separate surface.
  Font family is requested, resolved font is NOT verified. This is pipeline
  sanity, not a deterministic font golden or fidelity/performance comparison.

## Current verification boundary

Only fixture/report contracts and existing pure helpers were compiled/executed.
main.cpp is real Cairo/Pango SDK source but was NOT compiled, linked or run:
SDK headers absent and graphics validation restricted in this environment.
No PNG or real report.log has been generated. No fake SDK, runtime loading,
installation, core execution, workflow, or CI invocation occurs in this change.
CMake is absent and only reviewed statically. Existing C# remains unchanged.

## Future authorized SDK build/run

These commands are instructions for an environment with SDK and permission,
NOT commands executed here. No automated tests or workflows are configured.

```sh
cmake -S tests/native_render_smoke -B build/native-smoke \
 -DAETHER_BUILD_NATIVE_RENDER_SMOKE=ON \
 -DAETHER_BUILD_CAIRO_ADAPTER=ON -DAETHER_BUILD_PANGO_PROVIDER=ON
cmake --build build/native-smoke
build/native-smoke/aether_native_render_smoke NEW_OUTPUT_DIRECTORY RUN_ID
```

Parent directory must already exist; output directory must NOT exist. A new
exclusive directory prevents prior artifacts being treated as this execution.
RUN_ID is caller-supplied ASCII letters/digits/underscore/hyphen,1..64 chars;
it is an identifier, not a cryptographic or globally unique nonce. Paths are
trusted local paths; concurrent hostile modification of the output directory is
not addressed. All artifact basenames are fixed: geometry.png,text.png,
report.tmp,report.log. The runner never overwrites a preexisting output directory.

PNG files are decoded again with Cairo. Accept only ARGB32/RGB24, matching
width/height, valid stride/data/status after flush. Compare numeric RGB channels,
not padding: opaque PNG may decode RGB24 with undefined high byte. If decoded
ARGB32, require alpha255 throughout these opaque fixtures; this prevents
transparency artifacts from passing the text-change sanity check. Geometry checks interior
probes away from antialias edges. Text checks changed pixels IN THE DECODED PNG
and a white corner. Tofu/missing-font glyphs could satisfy sanity; no font or
shaping fidelity is inferred. Error surfaces/contexts are released with RAII.

## Report contract

Version1 key=value with escaping for control characters,equals and backslash.
Every phase PASS/SKIP/FAIL; skip/fail reasons explicit. Later dependent phases
skip on failure; the independent other fixture may still execute.
BuildPASS means only that this executable started, not aggregate compilation,
CI success or known compiler/revision. Unknown provenance is reported unknown.

Durations are steady-clock wall nanoseconds for executed phases; FAIL can have
observed duration. SKIP and build have no duration. Missing is not_measured,
never a zero placeholder. CPU/RSS not_collected. Text metrics are native DIP
values from this provider, not synthetic estimates. Baseline may be negative.
metrics.provenance distinguishes measured/not_measured/synthetic_test. Synthetic
reports never satisfy accepts_real_smoke, even if phase states exercise PASS.

Global runPASS requires all phases including PNG re-decode verification. Real
acceptance additionally requires measured provenance and successful final report
installation. report.tmp is NOT evidence of completed acceptance. Write/flush/
close then rename to report.log; a finalized report.log is the consumable file.
There is no fsync/crash-durability guarantee. If writing/renaming fails, stderr
and nonzero exit are authoritative. Report creation is not guaranteed on IO/OOM
failure. Exit0 is only returned after coherent real acceptance AND final report
write;1 signals tested failure;2 usage/output precondition;3 reporting/exception.

## Pure tests actually executable without SDK

From repository root, bash:

```bash
N=native/src/rendering/preparation
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG \
 -I native/include -I "$N/include" \
 tests/native_render_smoke/report.cpp tests/native_render_smoke/test_report.cpp \
 -o /tmp/aether-smoke-report-test
/tmp/aether-smoke-report-test
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG -pthread \
 -I native/include -I "$N/include" "$N/paint_snapshot.cpp" \
 native/src/rendering/cairo/cairo_geometry.cpp \
 native/src/rendering/text/text_request.cpp \
 native/src/rendering/text/pango_request_conversion.cpp \
 tests/native_render_smoke/fixture.cpp tests/native_render_smoke/test_fixture.cpp \
 -o /tmp/aether-smoke-fixture-test
/tmp/aether-smoke-fixture-test
```

Tests write no report files, especially no synthetic report.log. Report test
constructs synthetic states and a model of measured-state acceptance in memory;
this is test data, never evidence of rendering. Fixture tests prove input
compatibility with existing subset/helpers, not actual pixels. IO exclusivity,
PNG encode/decode, SDK linkage and renderer behavior await real smoke execution.
