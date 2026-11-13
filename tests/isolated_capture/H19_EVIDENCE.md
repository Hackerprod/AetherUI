# H19: explicit first-letter syntax and bounded inapplicability proof

Local candidate based on H18. No Actions or remote publication was run.

The native selector parser now records `::first-letter` and the historical
`:first-letter` as a distinct pseudo-element, with type-selector specificity.
Escaped and ASCII case-insensitive spellings use the native identifier parser.
The pseudo-element is not added to the host cascade and cannot generate a
`::before` or `::after` node. Unsupported chains, functional syntax and nested
first-letter pseudo-elements in logical selector arguments are rejected by the
strict parser. Runtime forgiving selector lists retain their existing recovery.

This is **not a first-letter renderer**. The CLI only admits a first-letter rule
when every matching host has a final computed `display:flex`, making the
pseudo-element inapplicable. Matching uses the native parser/matcher with a
bounded audited selector subset and a work budget checked before matching.
A flex item that is itself block does not qualify. Block/contents/SVG/unknown
hosts fail closed; conditions are conservatively checked even when inactive.
The original stylesheet is passed unchanged to the cascade. Every capture makes
a fresh proof; no positive applicability result survives a DOM/style mutation.

The CLI requires transactional style preparation for this proof, so allocation
failure cannot look like an empty successful legacy resolution. Global authored
CSS/resource/function checks still apply, including inactive declarations. H11's
canvas absence exception is unchanged. Unsupported inline-flex is not advertised
as supported: it has no distinct native display value in this implementation.

## Verification

Results and exact commands are recorded below.
No Chromium execution or comparative benchmark is claimed. The original
`flexbox_runtime_validation.html` is unchanged and remains outside the supported
capture profile. No text-rendering, full CSS or full flexbox parity claim.

Final clean build: 71 native core translation units + 9 support units.
- Native H19: **3268 checks**, parser OOM **13**, transactional runtime OOM
  **226**, PASS. Explicit/legacy/escaped spelling, specificity, no host cascade
  leakage, no generated nodes, conditions and dynamic style changes covered.
- Gate: **14 checks** + **14 allocation-failure points**, PASS. Same retained
  selector proof rechecked against fresh maps after flex/block and class changes.
- CLI H19: **62 checks**, PASS. Exact PNG equality against native no-pseudo
  controls, global inactive/resources checks, mixed/block/contents hosts,
  selector profile/budget/depth failures, inline-flex and canvas cases. Negative
  cases publish no PNG. Basic image visually inspected: one blue box as control.
- H4 exact pixel-boundary oracle PASS. Original fixture hash preserved by
  regression scripts; original still rejected rather than falsely captured.
- Basic capture sample: process wall **10.264 ms**, child CPU **4.525 ms**,
  peak RSS **3312 KiB**. Single local run, not a performance comparison.

```sh
sh tests/isolated_capture/build.sh /tmp/h19-final-clean
sh tests/isolated_capture/run_h19_core.sh /tmp/h19-final-clean
sh tests/isolated_capture/run_h19_gate.sh /tmp/h19-final-clean
python3 tests/isolated_capture/test_h19.py /tmp/h19-final-clean/aether_capture /tmp/h19-final-cli-h19
```

Local logs: `/tmp/h19-final-build.log`, `/tmp/h19-final-core.log`,
`/tmp/h19-native-regression.log`, `/tmp/h19-cli-regression.log`.
- Native regression scripts H3–H18: PASS, including existing lifetime/OOM and
  geometry/negative-oracle suites.
- CLI regressions H3,H5–H11,H13–H18: PASS; H3 executed 134 real CLI cases.
- `git diff --check`: PASS.

## Independent review correction

Independent review caught a false admission when valid but unsupported
`display:flow-root` followed `display:flex`: native parsing retained the earlier
flex. The first-letter proof now requires every authored display declaration to
be a single keyword from the native-supported set (or initial/inherit/unset),
and rejects `all` shorthand. This additional audit is H19-scoped, includes
inactive declarations and inline styles, and preserves original source. Thirteen
new negatives cover flow-root, list-item, grid, multi-keyword display, escaped
keywords, important, inactive/inline declarations and all:initial. It may reject
otherwise harmless unsupported declarations, deliberately failing closed.

After this correction all CLI regressions were rerun; native H5/H15/H16 scanner
and diagnostics suites were rerun in addition to the complete prior H3–H18 run.
Native objects remain from the clean 71+9 build; both CLI variants were fully
recompiled/relinked after the header-only proof correction.

A repeated H18 run hit PNG_WRITE_FAILED when the shared /tmp filesystem filled.
Prior H19 PNG artifacts were preserved by moving them to
`/workspace/shared/h19-prior-artifacts/`. With space available, H18 (53 checks)
and corrected H19 (62 checks) passed at `/tmp/h19-fix3-cli-h18` and
`/tmp/h19-fix3-cli-h19`. H3–H17 corrected-CLI passes are in
`/tmp/h19-fix2-cli-regression.log`; scanner-suite reruns exit0 in
`/tmp/h19-fix2-core-regression.log`. This was an output-storage limitation,
not a rendering equivalence failure.
