# G2-R2 independent validation evidence

Date: 2026-11-13. Baseline: `10459ec`. Candidate renderer SHA256:
`bbda737378e0d7ac3c6e9916d43987146fbe11a67c655d2007dbe2f5a2834e7e`.

An independent reviewer accepted the bounded raster ABI proposal before code
implementation. A separate implementation review found no blocking issue within
that contract. This is not a claim of complete rendering or CSS conformance.

## Executed results

- Independent explicit-geometry corpus: **160 checks PASS**. The same corpus on
  baseline extracted with `git archive HEAD` fails check 7: first translation.
  The fractional oracle also differs from zero displacement, preventing an
  8-bit quantization false positive.
- Author focal corpus, independently re-executed: **479 checks PASS**, including
  **9 allocation-failure sites** and **118 cancellation points**, checking
  retained confirmed frame and sequence. Includes A8 phase and expanded bounds.
- Author focal corpus with UBSan and nonrecovering undefined-behavior checks:
  **479 checks PASS**, no diagnostic observed.
- G1 gradient corpus: **27,072 checks PASS**. G1 fault injection: 3 preparation
  and 3 render allocation sites, atomic rollback PASS.
- Existing raster tests: renderer, stroke, rounded, rounded_stroke, curved_clip,
  shadow, border_group, opacity and scroll all PASS.
- Existing synthetic A8 suite: **679 checks PASS**, 11 allocation-failure
  points and 78 cancellation points. Real font provider/shaping not run.
- `git diff --check`: clean.

Reproduction from repository root:

```sh
sh tests/isolated_capture/g2_independent_run.sh OUTPUT_DIR
sh tests/isolated_capture/run_g2_focal.sh OUTPUT_DIR
sh tests/isolated_capture/run_g1_focal.sh OUTPUT_DIR
sh tests/isolated_capture/run_g2_regressions.sh OUTPUT_DIR
```

The regression runner was independently reviewed: assertions are enabled and
`set -eu` stops on a failed build or test. Three preexisting fixtures rejected
translation as unsupported; those fixtures now reject scale instead, retaining
unsupported-operation rollback assertions under the changed contract.

## Limits

Only native raster ABI translations are admitted. The HTML authored-transform
gate is unchanged; HTML translation end-to-end and Chromium are **NOT_RUN**.
A8 uses synthetic immutable masks, not real font loading or rasterization.
Original `flexbox_runtime_validation.html` remains unchanged and is not reported
as supported. No full suite, performance improvement, cross-OS equivalence or
complete HTML renderer is claimed. GitHub Actions were not invoked or enabled.
