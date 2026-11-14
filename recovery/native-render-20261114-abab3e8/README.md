# Recovery checkpoint abab3e8

This incremental Git bundle preserves original commit abab3e8bfa42f5d7108c8c7413e16c6ad8a5695d and its tree. It requires checkpoint 441ddfbacc5ea475beab2d377b400781c60717d8 from the previous bundle retained in recovery/native-render-20261114/aetherui-441ddfb.bundle on this branch.

SHA256: 706f78653c04774b4ff3d24d17beea61d143ec49fa88760e9fa094a572eb1fa1
Size: 385447 bytes.

First restore the previous bundle following its README (base main fc9927475e7cbbde701b769c67a7cd1118b2fbe8). Then, in that repository:

```sh
git bundle verify /path/to/aetherui-abab3e8.bundle
git fetch /path/to/aetherui-abab3e8.bundle refs/heads/recovery-abab3e8:refs/heads/recovered-abab3e8
git switch recovered-abab3e8
git rev-parse HEAD
```

Expected HEAD: abab3e8bfa42f5d7108c8c7413e16c6ad8a5695d.

This is a recovery archive, not promotion to main or general HTML/CSS completion. Diagnostic document text capture and audited outer shadows are included; nested overflow:auto is still unsupported. Production changes and local evidence are preserved as committed. No Actions settings or workflows changed. The backup branch is excluded by existing workflow filters.
