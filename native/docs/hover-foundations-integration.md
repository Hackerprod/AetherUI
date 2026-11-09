# Hover foundation integration checkpoint

Frozen product commit: `45c964741b86db1277bfeb4ad87ee4c4405c08a7`.
This record is documentation-only after the frozen product. Independent integration
review remains required. No production incremental path or C3 implementation is enabled.

## Inputs and preservation

- Existing baseline `32bb502debaf540d14f55a9a27c89dbfc67f0931`: managed A1/A2,
  B1/B2, C1 journal and expanded full-pipeline work counters.
- Reviewed B3 `e1c04f627c074bd1ac77428df2b88541698088ac` merged by `7601b58`.
- Reviewed C2 `f0f238e92fe74f03cbdf6f33d0e6f6f64e7f4999` merged by `45c9647`.

Both merges were clean. CMake retains C1's journal target, B3's persistent index,
payload delta and backing-link fix, and C2's module/test. Managed sources, DOM
sources and README were not modified. No product correction was needed.

## Local verification

- Linux/GNU C++ 14.2 Release, work counters ON: complete build PASS;
  full CTest **233/233 PASS**, no failures or skips.
- Counters OFF: core and relevant targets built; focal CTest **12/12 PASS**.
  This is not a complete OFF-suite claim.
- Real .NET 10.0.201 Core input traces: **16/16 PASS** with native ON and
  **16/16 PASS** with native OFF. Nine managed baseline scenarios completed.
- Native atomic revision: 4,381 checks, 22 aligned allocation failures covered.
  Payload delta covers bound generations, full order, K-path allocation count 24,
  OOM, provenance and long-history lifetime.
- C2 differential: 73,898 checks, 54,274 transitions, 1,548,080 matcher comparisons,
  3,142 changed matches covered, no observed false negatives (seed 0xC2A37).
- The 196 exported dynamic symbol names match baseline; contextual ABI tests pass.

Native libraries were copied explicitly into managed output and checked using
`cmp` and SHA-256. This avoids MSBuild `PreserveNewest` leaving a stale `.so`.
The ON library was restored after OFF testing.

- ON SHA-256: `ef9ba853ee481b0a5038df5f5094f2a78d5e230aaff2b175fba5aed727450b94`.
- OFF SHA-256: `dde2520e39726dc5acb14863ecb8a1decccae744293dfadf0f507bc6813a03fc`.

## Full managed work remains visible

Compared with the saved `expanded-managed-baseline.csv` at 32bb502, all 135
structural comparisons (9 scenarios x 15 fields) are identical. The scenarios use
100/500/1000 cards, each with none/static transform/static backdrop owners.

For ten measured resolves, compute, state, transition-context visits, full
preparation entries and bound record exports remain 1020/5020/10020 at those
sizes in every owner mode. Each row still reports ten revision publications,
ten context builds and ten complete visual-map builds. Style updates, layout
builds, legacy patch copies, visual-map patches, display-list builds and paint
chunks remain zero. Hit-box visits remain 1005/5005/10005.

B3 avoids bound exports inside an isolated native no-op resolve. The current
managed zero-update path still calls `CreateResolvedStyleMap` and materializes
N legacy records. Thus integrated exports remain N. There is no end-to-end O(K)
claim, no zero-scan transition context and no demonstrated global CPU improvement.
Timing columns are observational: samples are small and builds ran concurrently.

## Existing managed failures stay visible

`AetherUI.Tests` cross-build for `net10.0-windows10.0.19041.0` passed. Running that
executable on Linux returned exit 1 and **96/100**, with the same four failures as
the recorded baseline:

1. `Flex alignment preserves signed residual space`: G8-row-center-100-27,
   G8-row-flex-end-100-28, G8-column-center-100-50 and
   G8-column-flex-end-100-51 cross offsets actual 35, expected 30.
2. `Calc retains dimensional types in length and line-height contexts`:
   `line-height:calc(2)`, expected 7, actual 2.
3. `File image loader decodes a file PNG`: Windows-style path separators in
   the Linux temporary file path.
4. `Composite image loader falls through schemes`: the same path issue.

No assertions were removed or tests skipped. This is not full managed-suite PASS
and not Windows execution. Integration sanitizers were not rerun; the separate
B3/C2 input reviews retain their own sanitizer evidence.

## Remaining scope

C2 is linked but has no production caller. Full resolution remains authoritative.
The approved design/review and pinned Chromium `Element::SetHovered` and
`StyleEngine::PseudoStateChangedForElement` were checked again: unchanged-state
and local checks do not replace descendant/sibling/:has invalidation. The baseline
event contract remains unchanged, including pending-hover/discrete-event ordering.

C3 state/transaction proof and scheduling, D managed publication/context snapshots,
E input/hit and real Windows correctness/performance gates remain. Renderer
modularization is a later cycle. This checkpoint was local only, with no remote
publication by the integration worker.
