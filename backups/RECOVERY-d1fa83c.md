# Recover the z-index cache checkpoint

This branch stores an incremental Git bundle, not a main integration. Its ordinary source tree is published main baseline c63075540565a42b929e9c86f2c11030da6155c8. The original checkpoint inside the bundle is d1fa83c4f0b4b1a65a1e7d8c9b018fe51750724c, tree 3f81e3571a54a6220c219f8292bd16fef842f774. Performance acceptance and final integration remain pending.

File: backups/aetherui-checkpoint-d1fa83c.bundle
SHA-256: 30b04177d300bcec1ef6506c2697e0801e5ad1b316b25112955daa67e03a4df1
Bytes: 59611
Prerequisite commit: 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126
Bundle ref: refs/heads/checkpoint/d1fa83c
Original commits preserved: fc05f54c67aa262ceaa8074b5187a6dddf478f02, 0216906360c48b236963dbd205ee7edcb4d2fd80, eb24e7e189d8953d7282e96bc5e7aa4fe4d24829, 7fd13a92c154f4ba85bc8bcd5e3b793de967d9d0, d6f81f6051f42ee3eef3b11f42aefb63808e628c, 80fe1567b1d9daa786644971395a44b06d9a2d13, b042f65193c393c96027ac2afa877518d5fdb624, 71232e0a706dfdfe48ed88a518016b23938d098d, 56414347e0ca26cf0f8fd188e2a0da4c5ab73d81, d1fa83c4f0b4b1a65a1e7d8c9b018fe51750724c

## Recovery

Download the bundle without text conversion. In a separate clone of Hackerprod/AetherUI, fetch the prerequisite commit, then run:

```sh
git fetch origin 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126
git bundle verify aetherui-checkpoint-d1fa83c.bundle
git fetch ./aetherui-checkpoint-d1fa83c.bundle refs/heads/checkpoint/d1fa83c:refs/heads/recovered/d1fa83c
git rev-parse recovered/d1fa83c
git rev-parse recovered/d1fa83c^{tree}
```

Expected commit and tree are listed above. The archive preserves the ten original commit identifiers and history, including parser, stacking-plan, bounded-cache and documentation checkpoints. It excludes all uncommitted or untracked work, builds, credentials and private logs. The separate uncommitted eager-frame experiment is not included. This historical checkpoint predates published oracle/callback changes, so future integration must preserve newer main changes rather than replace main with the archived tree. Main is unchanged by this backup.
