# VALIDATION PENDING: recover flat planner adjacency checkpoint

This is a source backup only. Main is unchanged. The eight original commits are preserved exactly. This WIP checkpoint stores planner adjacency in two checked operation-owned buffers. This exact checkpoint is NOT validated; tests, performance gates and independent review remain pending. All prior candidates remain HOLD. No correctness or performance PASS is claimed, and this checkpoint is not approved for main. This is a fixed checkpoint, not a claim to be the final or latest experiment.

Prerequisite: c63075540565a42b929e9c86f2c11030da6155c8
Checkpoint: 8f37927fb7b89a31d1d1a5d4771e4e5571e27e9d
Source tree: aac2c407fceb2f70f5e07b4afa18b9169a24d5ce
Bundle: backups/aetherui-flat-adjacency-8f37927.bundle
Bytes: 43427
SHA-256: cd9af4a1277e7175be2ad3ceacba40bacda856934f52e74ca67104f72aaf5ce3
Bundle ref: refs/heads/checkpoint/8f37927

## Recovery

Download the binary bundle without text conversion. In a separate clone of Hackerprod/AetherUI:

```sh
git fetch origin c63075540565a42b929e9c86f2c11030da6155c8
git bundle verify aetherui-flat-adjacency-8f37927.bundle
git fetch ./aetherui-flat-adjacency-8f37927.bundle refs/heads/checkpoint/8f37927:refs/heads/recovered/8f37927
git switch recovered/8f37927
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
