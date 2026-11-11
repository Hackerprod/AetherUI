# Host phase metrics v2

This is observability, not a speedup. `host-phases-v2` adds `layoutInteropMs`,
`layoutProjectionMs` and `layoutProjections` to each Validator performance snapshot
and final event. Existing field names and calculations remain available.

## Boundaries

All durations are cumulative elapsed wall time, not thread CPU time. They are
partial regions, not an exhaustive decomposition: do not sum them into engine
or application latency, or subtract them from process CPU.

- `layoutInteropMs`: the `CreateNativeLayout` call, including its wrapper,
  P/Invoke, native algorithm and managed text/scroll callbacks. It excludes the
  preceding style-map creation, projection, reporting and drawing. It is not
  pure native-engine time.
- `layoutProjectionMs`: the `Project` call, including native reads, marshaling,
  conversion and .NET allocation. It excludes the subsequent scroll-index
  rebuild and drawing. Projection is required host work, not optional export.
- `styleMs`: `ResolveStyles`, including preparation, native calls, managed style
  conversion and map updates; not only the matcher.
- `displayListMs`: `RenderingInterop.Build` with managed readback/decoding and
  `AttachResolvedImages`; not only native command construction.
- `renderMs`: the backend `TryRenderIncremental` or `Render` call, after
  `EnsureVisualTree`. It excludes parse/style/layout and does not measure first
  presentation or GPU execution.

DOM parsing/projection, CSS loading, layout/style-map preparation, the scroll
index, scheduling, first presentation, callback subphases, reporting and captures
are not measured separately. The reporter runs after the backend timer. Its
resource reads, serialization and AutoFlush can affect the process and next
frame, but are outside the new regions. Native harness `invariants_export` and
`display_list_readback_checks` remain harness measurements, not .NET host times.

## Counting, failures and resets

Four clock reads surround the two existing calls. Each duration is accumulated
only after that call returns without propagating an exception. `LayoutBuilds`
counts completed interop calls; `LayoutProjections` counts completed projections.
A projection exception can leave one build and no completed projection for that
attempt. No new catches or cleanup/lifetime changes are introduced. Existing
native text/scroll callbacks catch exceptions and return default, so a completed
call is not proof that all callbacks were error-free.

Use Render, ResetMetrics and sampling serially on the host's owning thread.
Individual Interlocked reads and exchanges do not make the full snapshot/reset
atomic. The reporter emits `metrics-reset` when it observes a decrement,
including any new tick counter or LayoutProjections. It emits that event before
a subsequent snapshot/final with the new sample, without changing throttling.
A reset followed by enough work to reach every previous counter is invisible.
No event does not guarantee continuity; never subtract across a known reset.

`metrics-final.status=measured` means only that samples arrived. It is not
application success or a visual/conformance verdict. Read the entire JSONL and
process exit status: an `error` can occur after the final event. Reject a capture
as complete on any error, shutdown-timeout, nonzero exit, timeout, truncation or
incomplete output. Preserve failures instead of silently retrying. Synthetic
error-log tests verify this consumption contract, not actual Windows error
capture or absence of swallowed callback errors.

## Managed API

The original positional constructor and Deconstruct remain source-compatible.
The added init properties default to zero and participate in record equality.
The value type's size and equality change: binary compatibility is not promised.
Rebuild affected assemblies. The native ABI is unchanged. Milliseconds use
Stopwatch.Frequency; there are no timing thresholds in the tests.

## Validation and acceptance

Design v2 was independently approved. Preparation P passed at commit
`667c4441a299d5dd6c2c313697a5154389978ed7`, tree
`07c6767ef77ca3e8ee899668f2fe61498643d51a`, in the
[headless Windows run](https://github.com/Hackerprod/AetherUI/actions/runs/43041106442).
That run compiled real Core/Interop/Validator with isolated SDK 10.0.302 and a
real AMD64 native DLL, then passed all 19 preflight checks. It did not execute
Validator UI. Four previous failed preparation attempts remain failures.

The focal workflow subsequently builds the candidate and runs the dedicated
console tests, not AetherUI.Tests. T1 checks conversion, source constructor and
Deconstruct and equality. T2 checks the real reporter and reset heuristic. T3
uses a real host and in-memory renderer for initial render, unchanged render,
viewport rebuild and reset. T4 builds exactly the same old-API exporter source
against isolated baseline and candidate Core builds, verifies identical native
sources and loaded DLL hashes, and compares complete canonical output and old
counters (not raw handles or timing equality). T5 requires independent source
review of timer boundaries. T6 checks whole-log and exit-status semantics.
Each console process has a 30-second limit; tests have no sleep or speed threshold.

Until real C/T results for the exact published candidate and independent code
and evidence review pass, these metrics are provisional. Preparation P and native
CI alone do not satisfy C/T/S. Acceptance permits only the statement that the
validator reports these two phases separately. It does not establish faster
rendering, Chrome equivalence, visual parity or 403-case conformance. No 1+5-process
full-fixture diagnostic is included in this checkpoint; that needs gate S first.
