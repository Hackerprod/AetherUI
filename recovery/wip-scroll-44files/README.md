# WIP NOT VALIDATED: 44-file source snapshot

Coordinated stable capture: 36 modified + 8 new files, 1039061 source bytes, based on abab3e8bfa42f5d7108c8c7413e16c6ad8a5695d. Scope: native CMake; CSS frozen measure/style resolution; document text context; layout/scroll bounds; rendering scope/scroll requests/resolution/shadows/viewport; document HTML/text provider; style runtime typography metadata; document shadow tests; capture tool CMake. This is recovery only, not PASS, independent validation, or promotion to main.

Recovery commit: d2d5893059d5625add52fe4bdcdaf12ddac287f2
Tree: ddc654c9846b10f480ad4d2561e1045a448b59b0
Bundle: 52161 bytes
SHA256: be3d7af514e9f1bf3e412f5aa8333ca6554661a32f1e4d3240b70bf4e24af769

Restore prior bundles retained on this branch to obtain abab3e8, then:

```sh
git bundle verify /path/to/wip-scroll-02.bundle
git fetch /path/to/wip-scroll-02.bundle refs/heads/recovery-wip-scroll-02:refs/heads/recovered-wip-02
git switch recovered-wip-02
```

The earlier 21-file WIP bundle is NOT a prerequisite. All 44 hashes restored independently match manifest.json. Double-read capture was stable; no production index, branch or files were changed. Secret-signature scan found no matches. Existing backup/** workflow exclusions remain unchanged. Later edits are not included.
