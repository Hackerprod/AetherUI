# Recover the z-index parser checkpoint

This branch stores an incremental Git bundle, not a main integration. Its ordinary source tree is published main baseline 6ef97d895a0617d54c2b28af3be884b60e0c3cf7. The original checkpoint inside the bundle is 0216906360c48b236963dbd205ee7edcb4d2fd80, tree 5feb57d5ccb281a5aaa090f6addf3ba8abee8912. Final validation remains pending.

File: backups/aetherui-checkpoint-0216906.bundle
SHA-256: d8e3683411344d09d56f735c17f5c7776478db74b6eac8f52d66107131f56cc6
Bytes: 10753
Prerequisite commit: 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126
Bundle ref: refs/heads/checkpoint/0216906
Original commits preserved: fc05f54c67aa262ceaa8074b5187a6dddf478f02, 0216906360c48b236963dbd205ee7edcb4d2fd80

## Recovery

Download the bundle without text conversion. In a separate clone of Hackerprod/AetherUI, fetch the prerequisite commit, then run:

```sh
git fetch origin 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126
git bundle verify aetherui-checkpoint-0216906.bundle
git fetch ./aetherui-checkpoint-0216906.bundle refs/heads/checkpoint/0216906:refs/heads/recovered/0216906
git rev-parse recovered/0216906
git rev-parse recovered/0216906^{tree}
```

Expected commit and tree are listed above. The archive preserves original commit identifiers and history. It excludes all uncommitted or untracked work, credentials and private logs. In particular, active stacking changes outside the checkpoint are not included. The checkpoint predates the published gradient-oracle correction, so future integration must preserve newer main changes rather than replace main with this archived tree. Main is unchanged by this backup.
