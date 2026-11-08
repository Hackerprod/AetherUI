# VALIDATION PENDING: recover captured decoration view checkpoint

This is a source backup only. Main is unchanged. The thirteen original commits are preserved exactly. This WIP checkpoint reuses captured full style views for decoration helpers. Focused tests were reported passing by the implementation worker; full gates and independent review remain pending for this exact checkpoint. No correctness or performance PASS is claimed, and this checkpoint is not approved for main. This is a fixed checkpoint, not a claim to be the final or latest experiment.

Prerequisite: c63075540565a42b929e9c86f2c11030da6155c8
Checkpoint: 127f0816a6c5a5cd3215ed8645ff4e1648f00d05
Source tree: 4c8479f7ad4da51803d09cda3fc57f76f6e6c7c1
Bundle: backups/aetherui-decoration-view-127f081.bundle
Bytes: 49730
SHA-256: 57828c01d1e09e1d3d0b55d163fbc8897bd35ea40978c39124df81f78181cf8d
Bundle ref: refs/heads/checkpoint/127f081

## Recovery

Download the binary bundle without text conversion. In a separate clone of Hackerprod/AetherUI:

```sh
git fetch origin c63075540565a42b929e9c86f2c11030da6155c8
git bundle verify aetherui-decoration-view-127f081.bundle
git fetch ./aetherui-decoration-view-127f081.bundle refs/heads/checkpoint/127f081:refs/heads/recovered/127f081
git switch recovered/127f081
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
- b7715b35bdec77f6c57362dc45b42bad47aa74d6
- 4708f289fce1bfeb4f4f4589e05a39b522729759
- 0c6ccc8b275afc255d9b269a8e95e32e664c318c
- d8ca8da253dae3ebb831b76e1ca257c9a51db201
- 4ddc041af6ea563b4c7f42ceb45943b19b65547d
- 127f0816a6c5a5cd3215ed8645ff4e1648f00d05
