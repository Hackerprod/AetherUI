# T1.9 scope-walk benchmark

This benchmark isolates the scope-walk optimization. Baseline is **not the complete T1.8 executable**: it is T1.9 with the old eager full traversal substituted into the marked scope region. All other source, object files, compiler (`c++ -std=c++20 -O1`), fixtures and benchmark code are identical. This avoids conflating six-field evidence work with scope optimization.

Run from repository root after a manual build:

```sh
tests/isolated_capture/run_t19_scope_benchmark.sh BUILD_DIRECTORY EVIDENCE_DIRECTORY
```

The script builds both executables, runs seven independent fixtures per case, and checks result/diagnostic hashes. Cases are wide (128 elements), deep (64 elements), 0/1/3/6 deferred typography winners, and valid/local-invalid/root-invalid scopes. Each fixture measures initial resolution, class mutation and no-change separately. Construction and output hashing are outside measured time. No-change `prepare_revision`/`commit_revision` **still recomputes nodes in this runtime**; it is not presented as a cached fast path.

CPU is process `std::clock`; wall is `steady_clock`. RSS is Linux `getrusage(RUSAGE_SELF).ru_maxrss`, KiB high-water mark of the whole process, including fixture setup. It is not incremental operation memory. Summary CSV reports median/min/max timings and cumulative peak RSS. The recorded final run used GCC 14.2.0 on x86_64, baseline then candidate, after build/regression jobs completed; dispersion and scheduling noise preclude a universal latency claim.

## Observed results

504 matching baseline/candidate result and diagnostic records. Output hashes include all six evidence states/values, line-height kind, used size/height, dependencies, declared/resolved source and provenance, plus legacy computed size/weight/height. Document/revision identities are intentionally not compared between separately created fixtures.

- No deferred winner: candidate zero scope queries and visits.
- Multiple deferred winners: no more than one query per computed node.
- Local invalid terminal: exactly one scope per query, ending at that first invalid terminal.
- Same unknown conservatism and diagnostics under local and ancestor uncertainty.

Selected **initial resolution** observations (baseline → candidate):

| Case | Terminal visits | Wall median ms |
|---|---:|---:|
| Deep, 0 deferred, valid | 112320 → 0 | 5.833 → 6.040 |
| Deep, 6 deferred, valid | 112320 → 37440 | 8.731 → 8.205 |
| Deep, 6 deferred, local invalid | 118560 → 640 | 8.743 → 7.705 |
| Deep, 6 deferred, root invalid | 112512 → 36928 | 7.917 → 7.784 |
| Wide, 6 deferred, valid | 6912 → 2304 | 15.188 → 16.018 |

Whole-process peak RSS: baseline 5116 KiB, candidate 5468 KiB. These numbers do **not** establish a per-operation memory regression or improvement. Timings are mixed despite consistently reduced traversal; this is an operation-count optimization, not evidence of a universal speedup. All phases, CPU results and dispersion are retained in `scope-summary.csv`.

This benchmark neither enables grouping/rendering nor substitutes synthetic text for real shaping. Original document render and Chromium comparison remain outside this benchmark.
