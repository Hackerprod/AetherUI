# Recover the layout teardown checkpoint

This branch stores an incremental Git bundle, not a main integration. Its ordinary source tree is the published main baseline c413cc99f225a36288dedfdb8cec589bc21e2bb8. The original checkpoint inside the bundle is 62c3200393bc980c1e01b347160ae2e1ac7f3114, tree 1330c299033009efd99b43bf0238457f5eaf5256. Final validation and performance acceptance remain pending.

File: backups/aetherui-checkpoint-62c3200.bundle
SHA-256: e82b74e785a0cad124711291545861b3534f84a635799f323e53d16371860976
Bytes: 4746
Prerequisite commit: c413cc99f225a36288dedfdb8cec589bc21e2bb8
Bundle ref: refs/heads/checkpoint/62c3200

## Recovery

Download the bundle without text conversion. In a separate clone of Hackerprod/AetherUI, fetch the prerequisite commit, then run:

```sh
git fetch origin c413cc99f225a36288dedfdb8cec589bc21e2bb8
git bundle verify aetherui-checkpoint-62c3200.bundle
git fetch ./aetherui-checkpoint-62c3200.bundle refs/heads/checkpoint/62c3200:refs/heads/recovered/62c3200
git rev-parse recovered/62c3200
git rev-parse recovered/62c3200^{tree}
```

Expected commit and tree are listed above. The archive preserves the original commit identifier and history. It does not include worktree-only files, credentials, private logs or later gradient work. Main is unchanged by this backup.
