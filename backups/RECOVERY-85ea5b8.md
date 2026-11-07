# AetherUI recovery checkpoint: 85ea5b8

This branch preserves unmerged local work. It is a recovery backup, not an integration or a final validation result.

Original checkpoint: 85ea5b8af0c6ff09bb20ca67fa12791274d8c6ca
Original parent: 685dbf753216a6300172b8032212520ab52767d2
Original source tree: b2195154bc2e917027edc8f1bf7b1d4bb1c95a5b
Bundle prerequisite already on GitHub: 95cb2d07f876344b8e52a6765125dbb10246046a
Bundle SHA-256: 88c2f90f21bf47778847315a7fcf5fccc7bfdf80fa86cd1ba09531b44821de31

This cumulative bundle contains all 13 local commits after the prerequisite, including the eight measure-optimization commits through the contextual transition sampler checkpoint. It preserves their exact original hashes, source trees and history.

The branch's ordinary source files are the published main baseline. The unmerged checkpoint is inside the bundle. Recover it with Git before reviewing or integrating it.

## Restore the original commits

Use a fresh AetherUI checkout on this backup branch if another checkout has ongoing uncommitted work.

git fetch origin
git bundle verify backups/aetherui-checkpoint-85ea5b8.bundle
git fetch backups/aetherui-checkpoint-85ea5b8.bundle refs/heads/checkpoint/85ea5b8:refs/heads/recovered/measures-85ea5b8
git rev-parse recovered/measures-85ea5b8
git rev-parse recovered/measures-85ea5b8^{tree}

The last two outputs must match the checkpoint and source tree above.

The sampler's focused checks passed according to the implementation task; comprehensive integration and cross-platform validation are still pending. Only committed files at this exact checkpoint are included. The root README remains absent.
