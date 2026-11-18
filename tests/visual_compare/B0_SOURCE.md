# Installed B0 source capture

This opt-in adapter captures the existing static B0 stylesheet-controls, B1
semantic-page and B2 inline-wrap public ABI. It does not add layout/CSS support,
interaction, images, effects, browser comparison or general HTML support.
It is not LTR profile 3. No engine admission rules are changed.

Build/install `native` with CMake, then configure this directory with
`-DAETHER_VISUAL_COMPARE_NATIVE_HTML_LTR=ON -DCMAKE_PREFIX_PATH=<installed-prefix>`.
The existing switch enables the installed native capture adapters; neither uses
private renderer objects. Without it the requested B0 backend exits 11, without
fallback. CONTRACT.md defines report/3 and artifacts/3.

```
aether_visual_compare capture --html /absolute/inputs/page.html \
  --resource-root /absolute/inputs --manifest /absolute/inputs/resources.json \
  --out /absolute/new-output --viewport 480x640 --scale 1 \
  --backend native-b0-source \
  --regular-font fonts/DejaVuSans.ttf --bold-font fonts/DejaVuSans-Bold.ttf
```

Do not pass `--profile`. Supply exactly the two inventoried TTF fonts. HTML and
fonts are rendered from verified snapshots, not reopened after hashing. This is
not a filesystem race-resistant sandbox. Caps: scale 1; dimensions <=2048;
HTML <=64 KiB; combined fonts <=16 MiB. Source ABI capabilities are separate
from LTR limitations. Diagnostics contain only validated numeric values;
unavailable indices, statuses and diagnostics are explicitly null. Synchronous
ABI work is not internally interruptible; cancellation is sampled around calls,
and cleanup always releases the session.

## Reproducible local checks (not automatic CI)

1. Run `native/cmake/VerifyInstalledPackage.cmake` with SOURCE_DIR, new external
   WORK_DIR and CONFIG=Release. Repeat with INSTALL_LIBDIR=lib64 and a path with
   spaces. Preserve loader resolution and SHA256 of the installed DSO.
2. Build this directory with native capture, contract tests and preparation
   tests enabled. Run six historical contract executables, four preparation
   executables, `native_capture`, `b0_faults` and `report_b0`.
3. Copy `tests/b0_source_oracle.c` and `tests/b0_oracle/CMakeLists.txt` to an
   external source directory preserving their relative layout. Configure that
   CMake project with the same installed prefix. It is a C11 find_package
   consumer with no private headers.
4. Run `scripts/check_b0_source.py CLI REPOSITORY NEW_OUTPUT ORACLE`, then
   `aether_visual_compare_test_b0_integration REPOSITORY NEW_OUTPUT`.
   Python requires Pillow for independent PNG decoding. This stages the original
   fixtures and fonts byte-for-byte, records SHA256 provenance, captures full
   480x640 pages, narrower/clipped views and 240x640 B2, and compares every RGBA
   byte to the C11 consumer. Both consumers use the same renderer: pixel
   equality is a transport/encoder oracle, not independent semantic proof.
5. Open the full PNGs, including B2 over a contrasting background (it is
   transparent). B1's outer panel is 432px; clipped views are not full-page
   evidence. B2's body stays fixed at 240px at every viewport.
6. Execute existing native B0/B1/B2 tests including inline-wrap, inline-failures,
   ledger, repertoire, parsed-dom and semantics. Run historical
   `check_native_cli.py` and `check_native_png.py` too.

`b0_faults` substitutes a deterministic ABI solely to exercise malformed/untouched
diagnostics and frame metadata. It is not renderer evidence. `report_b0` tests
publication failures, cancellation, canonical identity and corruption rejection.
The negative capture matrix preserves authentic engine status/stage/reason and
must never be represented as support for the rejected feature.

Independent review/rebuild is required before publication. None of these checks
closes the historical login, browser parity, general HTML or full audit scope.
