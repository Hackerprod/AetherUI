# CMake validation recovery checkpoint (not release)

Snapshot preserves 63 source/fixture overlay files, 16 plan/review/acta files, custody and small structured validation results and reproduction scripts. The archive MANIFEST.json verifies 100 entries; SHA-256: 703414e8f3cc96748ddd2fe8699c4f9dbc5c47f557a32abd83cf9114b3d33fc7.

Restore by checking out parent 2fac785f83b9abdeed866361f6ec1cfaea82ab3e and copying overlay/ paths to the repository root. Preserve the previous checkpoint archives. The overlay is complete for the 63 changed paths; the included tracked-only patch alone is NOT a complete restore. Original local candidate base: 22fba13df2dc7775d4e091dac432c3e4b2960c46. Existing source README is archived unchanged, not recreated at repository root.

Status at snapshot: CMake available; G0 and the includes/lookup deltas reviewed. Validation -03 configure and static consumer gate passed; default build interrupted around 47% without final exit code, so G1 is NOT passed. G2 and subsequent gates remain unexecuted/closed. Earlier failed builds and narrow PASS_DELTA_ONLY reviews do not establish renderer/product acceptance. Subsequent continuation is not automatically covered by this snapshot.

No build outputs, stdout/stderr logs, credentials, private records, or workflow changes included. Reproduction scripts preserve original workspace paths and may require path adjustment after restore. Archive extraction and manifest hashes verified locally before publication. No main integration.
