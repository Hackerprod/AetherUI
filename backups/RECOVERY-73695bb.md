# Recover the callback and teardown composition checkpoint

This branch stores an incremental Git bundle, not a main integration. Its ordinary source tree is the published main baseline 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126. The original combined checkpoint inside the bundle is 73695bb69bfb791143151282d2af273dadbdddbf, tree 3fee2ebd87760f80862f31fdf149cf43b8f140c2. Combined validation remains pending.

File: backups/aetherui-checkpoint-73695bb.bundle
SHA-256: 4c4d8e4ebf9570236fd4f189472104223f002903727d24afc9577781458115be
Bytes: 22773
Prerequisite commit: 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126
Bundle ref: refs/heads/checkpoint/73695bb
Original commits preserved: fc05f54c67aa262ceaa8074b5187a6dddf478f02, 73695bb69bfb791143151282d2af273dadbdddbf

## Recovery

Download the bundle without text conversion. In a separate clone of Hackerprod/AetherUI, fetch the prerequisite commit, then run:

```sh
git fetch origin 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126
git bundle verify aetherui-checkpoint-73695bb.bundle
git fetch ./aetherui-checkpoint-73695bb.bundle refs/heads/checkpoint/73695bb:refs/heads/recovered/73695bb
git rev-parse recovered/73695bb
git rev-parse recovered/73695bb^{tree}
```

Expected commit and tree are listed above. The archive preserves original commit identifiers and history. It excludes worktree-only files, credentials and private logs. The checkpoint includes the published gradient change but predates the separate gradient-oracle correction branch; future integration must preserve any newer main changes. Main is unchanged by this backup.
