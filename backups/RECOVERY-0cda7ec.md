# Recover the callback frame checkpoint

This branch stores an incremental Git bundle, not a main integration. Its ordinary source tree is the published main baseline 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126. The original checkpoint inside the bundle is 0cda7ec0ac8fec56f245d8267313fecc82d45d8b, tree e64a8c22866bde7a7202134fe0dd1ee4d7a0a5f8. Final validation remains pending.

File: backups/aetherui-checkpoint-0cda7ec.bundle
SHA-256: 31272c2f72107f3a60b3eaa8dd832bce280c8e909aaa32aa8865775cfc2cf9e6
Bytes: 18671
Prerequisite commit: c413cc99f225a36288dedfdb8cec589bc21e2bb8
Bundle ref: refs/heads/checkpoint/0cda7ec
Original commits preserved: b2cf1509d1d4e449524a40cdb5c8eb71019fdee5, 0cda7ec0ac8fec56f245d8267313fecc82d45d8b

## Recovery

Download the bundle without text conversion. In a separate clone of Hackerprod/AetherUI, fetch the prerequisite commit, then run:

```sh
git fetch origin c413cc99f225a36288dedfdb8cec589bc21e2bb8
git bundle verify aetherui-checkpoint-0cda7ec.bundle
git fetch ./aetherui-checkpoint-0cda7ec.bundle refs/heads/checkpoint/0cda7ec:refs/heads/recovered/0cda7ec
git rev-parse recovered/0cda7ec
git rev-parse recovered/0cda7ec^{tree}
```

Expected commit and tree are listed above. The archive preserves original commit identifiers and history. It excludes worktree-only files, credentials and private logs. The checkpoint predates the published gradient change; future integration must preserve that change rather than replacing main with this archived tree. Main is unchanged by this backup.
