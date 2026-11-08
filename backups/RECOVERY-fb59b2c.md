# Recover the no-cache native stacking integration

This is a source backup only. Main is unchanged. The seven original commits are preserved exactly in the incremental bundle. Functional validation was reported complete; performance of this integrated variant has not been measured. This backup does not claim a performance PASS and does not include the subsequent cache experiment.

Prerequisite: c63075540565a42b929e9c86f2c11030da6155c8
Checkpoint: fb59b2cabc486917e478885e69225efd4af0483f
Source tree: 1847f8cc1009d815aa52e065dd00ef51e4d87861
Bundle: backups/aetherui-integration-fb59b2c.bundle
Bytes: 41501
SHA-256: 7f0b6521d7edc65f370c63f643f5384a2fc8ee4dc9a48ef5b35fe87093255312
Bundle ref: refs/heads/checkpoint/fb59b2c

## Recovery

Download the binary bundle without text conversion. In a separate clone of Hackerprod/AetherUI:

```sh
git fetch origin c63075540565a42b929e9c86f2c11030da6155c8
git bundle verify aetherui-integration-fb59b2c.bundle
git fetch ./aetherui-integration-fb59b2c.bundle refs/heads/checkpoint/fb59b2c:refs/heads/recovered/fb59b2c
git switch recovered/fb59b2c
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
