# VALIDATION PENDING: recover flat planner adjacency checkpoint

This is a source backup only. Main is unchanged. The twelve original commits are preserved exactly. This WIP checkpoint stores planner adjacency in two checked operation-owned buffers. Focused tests were reported passing by the implementation worker. Full validation, performance gates and independent review remain pending. This checkpoint is NOT fully validated. All prior candidates remain HOLD. No correctness or performance PASS is claimed, and this checkpoint is not approved for main. This is a fixed checkpoint, not a claim to be the final or latest experiment.

Prerequisite: c63075540565a42b929e9c86f2c11030da6155c8
Checkpoint: d8215a297ac6b447be67eefd02c4dcd2ae655bf8
Source tree: 8f7f655310548b0682d65a30a7080c73abf458ae
Bundle: backups/aetherui-flat-adjacency-d8215a2.bundle
Bytes: 52549
SHA-256: cd97bd4aa0aac78db52302471b620b9d4ed4a92420f62003f3570abaf6c71bd4
Bundle ref: refs/heads/checkpoint/d8215a2

## Recovery

Download the binary bundle without text conversion. In a separate clone of Hackerprod/AetherUI:

```sh
git fetch origin c63075540565a42b929e9c86f2c11030da6155c8
git bundle verify aetherui-flat-adjacency-d8215a2.bundle
git fetch ./aetherui-flat-adjacency-d8215a2.bundle refs/heads/checkpoint/d8215a2:refs/heads/recovered/d8215a2
git switch recovered/d8215a2
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
- 8f37927fb7b89a31d1d1a5d4771e4e5571e27e9d
- ac3b9cbc0b7702339fd37c4417e7615c806abc48
- 694cbca15b055f69a1c3c667243b8021c90cdf36
- a5482ff1a2edb1ac73c3135a1dc3b01c41b54c22
- d8215a297ac6b447be67eefd02c4dcd2ae655bf8
