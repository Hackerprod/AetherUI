# Recover the selective z-index cache experiment

This branch archives an experimental checkpoint, not a main integration or an accepted optimization. **Performance status: HOLD. Two of 32 performance controls fail; there is no global PASS.** Semantic/differential/focused checks were reported green. Preserve this source for future evaluation without adopting it by default.

The ordinary branch source tree is published main baseline c63075540565a42b929e9c86f2c11030da6155c8. The original checkpoint inside the bundle is 61b6a6f3254f73e439e4ef12daea5d1b55c02b7b, tree 0c11dd318355ca84d4a166beec35d3135cf946ae. The prepared integrated variant is not archived here or represented as validated.

File: backups/aetherui-checkpoint-61b6a6f.bundle
SHA-256: 3000d3ba4278f12eefcd49eb33a2f56116c57235b812c4fca4b351480dd6658d
Bytes: 69429
Prerequisite commit: 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126
Bundle ref: refs/heads/checkpoint/61b6a6f
Original commits preserved (13): fc05f54c67aa262ceaa8074b5187a6dddf478f02, 0216906360c48b236963dbd205ee7edcb4d2fd80, eb24e7e189d8953d7282e96bc5e7aa4fe4d24829, 7fd13a92c154f4ba85bc8bcd5e3b793de967d9d0, d6f81f6051f42ee3eef3b11f42aefb63808e628c, 80fe1567b1d9daa786644971395a44b06d9a2d13, b042f65193c393c96027ac2afa877518d5fdb624, 71232e0a706dfdfe48ed88a518016b23938d098d, 56414347e0ca26cf0f8fd188e2a0da4c5ab73d81, d1fa83c4f0b4b1a65a1e7d8c9b018fe51750724c, bf722525d7cd5528b8746858b8046071ab19b06d, d1041337f4dba736123162e03da72623425cff18, 61b6a6f3254f73e439e4ef12daea5d1b55c02b7b

## Recovery

Download the bundle without text conversion. In a separate clone of Hackerprod/AetherUI, fetch the prerequisite commit, then run:

```sh
git fetch origin 3418183e2f1fb1651ee3ab9cf302e70ae9ef5126
git bundle verify aetherui-checkpoint-61b6a6f.bundle
git fetch ./aetherui-checkpoint-61b6a6f.bundle refs/heads/checkpoint/61b6a6f:refs/heads/recovered/61b6a6f
git rev-parse recovered/61b6a6f
git rev-parse recovered/61b6a6f^{tree}
```

Expected commit and tree are listed above. This cumulative bundle preserves original source commits including the earlier d1fa83c checkpoint and the subsequent retirement, selective-admission and coverage commits. It excludes uncommitted/untracked work, builds and private logs. Root README deletion remains unchanged. This historical source predates newer main changes: any future integration must preserve current main rather than replace it with this archived tree. Main is unchanged by this backup.
