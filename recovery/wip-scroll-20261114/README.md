# WIP NOT VALIDATED: scroll source recovery

Snapshot captured at a coordinated stable point from 21 modified/new source files on base abab3e8bfa42f5d7108c8c7413e16c6ad8a5695d. Double read and staging hashes matched. This is loss prevention only, not acceptance, PASS, or promotion to main.

Recovery commit: 911a025d3531634b672d33a2a28bd4d2448f89d2
Tree: 8f7627aa6f210892ca56a85ea8d16b8d2285bc3d
Bundle size: 28119 bytes
SHA256: 9f1f3075b4c2c72ac56df3a0762d7a94de5b943be82c03ef32ec8e8a12eb9fa4

Restore the two previous bundles retained on this branch to obtain abab3e8 (follow their READMEs). Then:

```sh
git bundle verify /path/to/wip-scroll.bundle
git fetch /path/to/wip-scroll.bundle refs/heads/recovery-wip-scroll:refs/heads/recovered-wip-scroll
git switch recovered-wip-scroll
```

All 21 restored file hashes were checked against manifest.json. No production checkout index/branch changed during capture. Workflows and Actions settings were not changed. This branch uses the existing backup/** workflow exclusions.
