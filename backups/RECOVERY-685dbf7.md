# AetherUI recovery checkpoint

This branch preserves unmerged local work. It is a recovery backup, not an integration or a validation result.

Original checkpoint: 685dbf753216a6300172b8032212520ab52767d2
Original source tree: 204d1f7b39b717fdb0333259f59a4c61a6da20db
Bundle prerequisite already on GitHub: 95cb2d07f876344b8e52a6765125dbb10246046a
Bundle SHA-256: d24e44493981309c8011639fd535d2b781703cc0475c2d38525d56a7ab654621

The bundle contains the 12 local commits after the prerequisite, including the seven measure-optimization commits ending at the checkpoint. It preserves their exact original hashes, trees and history.

The branch's ordinary source files are the published main baseline. The unmerged checkpoint is preserved inside the bundle; recover it with Git before reviewing or integrating it.

## Restore the original commits

Run these commands in an AetherUI checkout after fetching this backup branch. Use a fresh checkout if you have ongoing uncommitted work.

git fetch origin
git bundle verify backups/aetherui-checkpoint-685dbf7.bundle
git fetch backups/aetherui-checkpoint-685dbf7.bundle refs/heads/checkpoint/685dbf7:refs/heads/recovered/measures-685dbf7
git rev-parse recovered/measures-685dbf7
git rev-parse recovered/measures-685dbf7^{tree}

The last two outputs must match the checkpoint and source tree above. A recovery rehearsal fetched the prerequisite from GitHub into an empty repository, imported the bundle and verified both hashes.

Uncommitted sampler edits are not included. No final integration or cross-platform validation is claimed. The root README remains absent.
