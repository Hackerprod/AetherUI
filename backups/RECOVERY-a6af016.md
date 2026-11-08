# WIP / HOLD: recover experimental compact operation snapshot

This is a source backup only. Main is unchanged. The ten original commits are preserved exactly: seven integration commits and three compact snapshot / test commits. Core Release compiled at d354ba3; full build, functional tests and performance validation were pending when this checkpoint was selected. WIP / HOLD: no functional or performance PASS is claimed. This is a fixed checkpoint, not a claim to be the final or latest experiment.

Prerequisite: c63075540565a42b929e9c86f2c11030da6155c8
Checkpoint: a6af01647330c27d8f8488bc36b47ee40fcf6706
Source tree: 3c4369ea9004d46c456cf2c851bcadb19fcd94a6
Bundle: backups/aetherui-compact-a6af016.bundle
Bytes: 59754
SHA-256: 80f283af08272a0c39de88076a358b9a6bd7494a5c07c5a28c1c0b8eac60e4ee
Bundle ref: refs/heads/checkpoint/a6af016

## Recovery

Download the binary bundle without text conversion. In a separate clone of Hackerprod/AetherUI:

```sh
git fetch origin c63075540565a42b929e9c86f2c11030da6155c8
git bundle verify aetherui-compact-a6af016.bundle
git fetch ./aetherui-compact-a6af016.bundle refs/heads/checkpoint/a6af016:refs/heads/recovered/a6af016
git switch recovered/a6af016
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
