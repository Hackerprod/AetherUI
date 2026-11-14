# AetherUI recovery checkpoint

This branch is a recovery archive, not an implementation merge or a validation PASS.
Main remains unchanged. No Actions are requested.

Base commit: fc9927475e7cbbde701b769c67a7cd1118b2fbe8
Recovered tip: 441ddfbacc5ea475beab2d377b400781c60717d8
Original commits preserved: 24
Bundle bytes: 769176
Bundle SHA256: 5bfbd78e1f032b7403bd39c1dcf0afe131f7e35dc376ab30b576a974fd8cd56a

Download aetherui-441ddfb.bundle from this directory, then in a clone containing the base:

```sh
sha256sum aetherui-441ddfb.bundle
git bundle verify aetherui-441ddfb.bundle
git fetch ./aetherui-441ddfb.bundle HEAD:refs/heads/recovered/native-render-441ddfb
git rev-parse recovered/native-render-441ddfb
git rev-list --count fc9927475e7cbbde701b769c67a7cd1118b2fbe8..recovered/native-render-441ddfb
```

Expected tip and commit count are above. This preserves original commit identities and evidence, not merely a squashed snapshot. The work includes diagnostic selected-HTML capture and retains its documented limitations; it is not general HTML/CSS completeness.
