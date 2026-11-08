# VALIDATION PENDING: recover frozen query metadata checkpoint

This is a source backup only. Main is unchanged. The fourteen original commits are preserved exactly. This WIP checkpoint splits construction metadata from stacking query boxes. Tests and independent review are pending for this exact checkpoint. No correctness or performance PASS is claimed, and this checkpoint is not approved for main. This is a fixed checkpoint, not a claim to be the final or latest experiment.

Prerequisite: c63075540565a42b929e9c86f2c11030da6155c8
Checkpoint: ddebff8c31e73811295993d7ead74924288df31e
Source tree: 160caac05812fd84c712fed6b97bc3a02a3962a7
Bundle: backups/aetherui-query-metadata-ddebff8.bundle
Bytes: 67512
SHA-256: 9e38dd65c14c388465cf1dd9914be566bf53ba98ccd9e84b7ba97f6dd053fddf
Bundle ref: refs/heads/checkpoint/ddebff8

## Recovery

Download the binary bundle without text conversion. In a separate clone of Hackerprod/AetherUI:

```sh
git fetch origin c63075540565a42b929e9c86f2c11030da6155c8
git bundle verify aetherui-query-metadata-ddebff8.bundle
git fetch ./aetherui-query-metadata-ddebff8.bundle refs/heads/checkpoint/ddebff8:refs/heads/recovered/ddebff8
git switch recovered/ddebff8
git rev-parse HEAD
git rev-parse HEAD^{tree}
git status --porcelain
```

The commit and tree must match the values above; status must be clean. Root README remains deleted. Only committed source, tests, and technical source documentation are included; no builds, private logs, untracked files, or later experimental changes are included. The archive branch ordinary source remains the main baseline, with only this recovery document and bundle added.

Original commits, oldest first:

- ba7f46d992fe0615e8aa029668deba16088a4dba
- 913cce5615d332d1379fcbc270b628127f1a0d9e
- 912395c4202e72740c6503382ce897b162f72a0d
- 91dae77ea075810e71ece9716d1c4f75f926c5c0
- cf5a5f81c7ed1275ae6a4ef58b18cedc849c1b1b
- 98000a4726d2ad24defc5643b6877ad6bcd36544
- fb59b2cabc486917e478885e69225efd4af0483f
- d354ba31ed8c708266818c08907c154e89dcda41
- 05414d420a778a07584e5cbf3c8dddba594444ac
- a6af01647330c27d8f8488bc36b47ee40fcf6706
- d4dac1a5a069f8e6747a266e7288eba7347727e6
- 7a00c958ff29082841e0aa067bfa7cf9ed7a0e25
- 9b13bab4afa579780f01b15cf3d8ea77f7ff47a4
- ddebff8c31e73811295993d7ead74924288df31e
