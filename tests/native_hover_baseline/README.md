# Native hover diagnostic baseline (manual, opt-in)

This is NOT an optimization or a performance acceptance gate. No Actions, public
ABI, host, C#, rendering, Chromium comparison or speedup claim is introduced.
It exercises state -> resolve_state_changes -> consume/free. A repeated same state
still invokes the native resolver; this is not the host's same-target shortcut.

```
tests/native_hover_baseline/build.sh /absolute/new/build
python3 tests/native_hover_baseline/pilot.py /absolute/new/build /absolute/new/results
```

Build once from clean sources using C++20/O1, linking identical common objects and
compiling the runtime/harness translation unit for modes 0 (compiled out), 1
(counters), 2 (counters + phase clocks). A baseline binary is never reused as a
mutable library. This white-box test follows existing source-order tests. No
allocator overrides, logging or synchronization are placed in measured runtime.
Harness clocks exist identically in all modes. Compiler/toolchain and machine
identity should accompany exported results; container quotas are **unknown** until
actually measured. Unknown quotas allow only diagnostic interpretation.

## Corpus and meanings

18, 180 and 1800 cards, each with one child; deterministic order, no randomness.
Scenarios: 0 local background hover alternating two cards; 1 no hover rules;
2 repeated same state; 3 ancestor hover descendants; 4 adjacent-sibling selector;
5 explicit background inheritance plus currentColor; 6 currentColor metadata
change preserving RGBA. Each fixture measures initial, transition and nochange
separately. Two warmup resolutions precede transition. Nochange inherits the final
transition state. Raw counters include state calls and actual changes.

One global monotonic 85 s execution deadline (5 s reserved for teardown/reporting,
90 s hard ceiling) covers canonical correctness, calibration,
and measurement subprocesses. Five alternating-order blocks are an objective, not
permission to exceed that deadline. Two transitions per phase are frozen after the
single calibration call. Incomplete cells/blocks remain **partial**, never filled
or extrapolated. Compilation is outside the pilot deadline. A terminated partial
subprocess has no completed row; completed prior rows are retained and flushed.

Canonical checks serialize scalar styles field-by-field (never padding/address),
impacts/update order, custom scopes (sorted), color metadata, measures,
typography/provenance, source accounting, diagnostics in order, relative epoch
relations, and retained revision contents. The corpus has no text or effects;
nonempty effect payloads, contextual trees or text diagnostics fail closed rather
than being silently ignored. Inline invalid/unknown declarations and real inline source-byte-limit overflow rejection are tested; rejected resolution must not publish. This is
corpus equivalence, not an exhaustive proof for every possible CSS document.
Canonical checks are outside measured regions and their complete output must be
byte-equal across configurations. Legacy zero-update ambiguity is guarded by a
required generation increment on every successful measured resolution.

## Timing attribution

* wall/user/system are per phase including state calls, API resolve and consume.
* `state_api_ns`, `resolve_api_ns` and `consume_free_ns` distinguish API work from retrieval/free.
* `process_peak_rss_kib` is getrusage(RUSAGE_SELF) process lifetime high-water RSS,
  NOT per-operation allocation, growth, or peak delta.
* preparation is prepare_tree only, inclusive of its compute children. Earlier staged index/state copying is outside this phase. `compute_inclusive_ns` includes matching, cascade,
  inline compilation and early freezing. It is a **subset**, not additive.
* `matching_unavailable_ns` is NA: matching time is unavailable, not zero
  work. Candidate/matcher counters provide work evidence without per-selector
  clock overhead.
* freeze covers post-traversal records/document/paragraph work; backing is next;
  updates/finalization also includes prepared clearing and output allocations.
* commit_total includes checks, publication and diagnostics replay. Publication
  and replay are distinct subsets. Do not sum nested timings.
* cleanup is successful legacy transaction destruction only; error cleanup and revision handle release are
  not attributed. Consume/free is independently measured in the harness.

Summary uses median/min/max wall per resolve and raw blocks remain authoritative.
Instrumentation overhead versus off is absolute and relative; never subtract it
to invent corrected timings. Counters/timers alter work and timed mode can distort
small cases. No CPU-time/RSS causal interpretation under unknown quotas.
