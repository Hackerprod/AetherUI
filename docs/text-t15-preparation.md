# T1.5 preparation only

No native provider is shipped. No real font shaping, HTML PNG, SDK installation,
or Chromium execution is claimed. The original fixture remains byte-identical.
Run `python3 tests/isolated_capture/t15_inventory.py` for reproducible raw-source
inventory. Missing declarations are not CSS defaults or evidence of support.
The inventory cannot turn G1/G1.2 eligibility into Eligible.

The T1.4 layout boundary cannot prove family, weight/style, language, direction,
features, or contextual grouping. It now rejects native selection **before** a
provider call. This intentionally closes the internal synthetic-layout path;
legacy layout stays unchanged. Direct native_measure transport remains an
internal unit contract, not a layout/capture entry point.

`context_contract.h` is a preparation contract, not integrated shaping. It
requires complete identical context, rejects RTL, vertical, fallback and
variations; the SDK diagnostic returns Unknown when absent. Its helper retains
source byte offsets across adjacent nodes, including empty pieces. Whitespace
is already-resolved input and is preserved; forced breaks require separate
groups. It is not a CSS whitespace implementation. AV/To/ffi spies verify only
input grouping, not typographic equivalence. Actual intrinsic tokenization and
final per-item measurement still require replacement before enabling native.
Different contexts must be separate runs, never joined by this helper.

max_width=0 means unbounded measurement, not a zero wrap width. Nonfinite and
negative widths are rejected by native_measure. Font bytes/face/size identity
remains the T1.4 transport contract; this preparation does not establish that
claimed font family matches parsed font metadata.

Consulted Blink `harfbuzz_shaper.cc`, RangeContext and ShapeRange (2026-11-13):
https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/platform/fonts/shaping/harfbuzz_shaper.cc
Blink carries font/features, script, language and direction into shaping; it
does not justify adding isolated token widths as contextual shaping.

Validation commands:
- `c++ -std=c++20 -Wall -Wextra -Inative/include tests/isolated_capture/t15_context_tests.cpp -o /tmp/t15_context && /tmp/t15_context`
- `sh tests/isolated_capture/build.sh NEW_BUILD_DIR`
- `sh tests/isolated_capture/run_t14_provider.sh NEW_BUILD_DIR`

Real shaping / PNG / Chromium: NOT_RUN. Their timings: NOT_MEASURED.

## Executed evidence

Clean manual build completed. Tests: context27, transport/provider32,
source-hash23, spaces30, fragment16, snapshot237, synthetic A8 680
(11 OOM / 78 cancellation), T1 transport37, H20 style1247 (222 OOM),
H21 style1475 (222 OOM). These are focused suites, not all repository tests.
`pkg-config --exists harfbuzz freetype2` returned 1; no install attempted.
Original `--css` capture returned REJECTED / REQUESTED_CSS_DOMAIN_UNAVAILABLE;
worker exit3, no HTML PNG. Measured rejection-process timing is not rendering
performance. Native layout's former synthetic success cases intentionally now
expect INVALID_STATE with zero provider calls, because context cannot be proven.
