# AetherUI pseudo-capability recovery checkpoint

This branch is a recovery backup, not an integration or a final performance acceptance.

Original checkpoint: cd8b29b832f1b90478fb40b76eb610500983ae5c
Original source tree: e7c32be9f24f125b34e5a9966db45492b31aaddb
Bundle prerequisite already in published Git history: 425c876f585473be3fef693444d6587749a8caa3
Bundle SHA-256: e9b9da1edafc36c4e485b6eea372ca19ed53f13057bdbf065fbe69fe4da8100e

The bundle contains these three original commits, preserving their exact hashes and history:
- 4f98ddf068b569f615a73727b39d1143e8023f32: legacy opacity cancellation
- 5fb076e8e4380124614ad0053f944380b3714c5c: anonymous flex text continuity
- cd8b29b832f1b90478fb40b76eb610500983ae5c: generated pseudo-kind capability pruning

The ordinary source files on this backup branch are the published main baseline at faff234ad90ac9f2c8cf042cf964d4659d311376. The original local checkpoint is inside the bundle. It was based on the earlier text-flex checkpoint and does not contain the later main flex cross-line changes. Restore to a separate branch and preserve later main changes when preparing a future integration.

## Recover original history

In a fresh AetherUI checkout of this backup branch:

git fetch origin
git bundle verify backups/aetherui-checkpoint-cd8b29b.bundle
git fetch backups/aetherui-checkpoint-cd8b29b.bundle refs/heads/checkpoint/cd8b29b:refs/heads/recovered/pseudo-capability-cd8b29b
git rev-parse recovered/pseudo-capability-cd8b29b
git rev-parse recovered/pseudo-capability-cd8b29b^{tree}

The final two outputs must equal the original checkpoint and source tree recorded above. The prerequisite remains available in published history.

Only committed source was archived. No pending worktree files, private logs or credentials were included. The root README remains absent. Performance acceptance is pending; this backup does not claim it passed.
