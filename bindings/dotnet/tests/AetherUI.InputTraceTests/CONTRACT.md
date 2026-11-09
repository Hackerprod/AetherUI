# Phase A: managed input observations

This is a portable `net10.0` executable exercising the real `UiDocumentHost`, native style/layout/hit-test library and DOM event dispatch. It does not emulate Windows or run the Windows renderer. It compiles the existing Validator `PerfReporter.cs` as well. No Windows project target was changed.

Run after installing the matching native library under `artifacts/native/<RID>/lib` (or the normal Windows bin location):

```
dotnet build bindings/dotnet/tests/AetherUI.InputTraceTests -m:1 -p:UseSharedCompilation=false
dotnet bindings/dotnet/tests/AetherUI.InputTraceTests/bin/Debug/net10.0/AetherUI.InputTraceTests.dll
```

The single-process compiler options avoid sandbox compiler-server/named-pipe limitations; native validation remains enabled. A writable `DOTNET_CLI_HOME` may be needed in a sandbox.

## Instrumentation contract

`UiHostOptions.EnableInputWorkCounters` is false by default. Validator enables it when `AETHERUI_WORK_COUNTERS=1`; its existing `--perf` reporter emits aggregate deltas at its existing reporting cadence. No new event callbacks, per-event logs, position histories or timers are installed. Counters are host-local, atomically incremented/read/reset consistently with the existing metrics. Resets are diagnostic operations, not an atomic multi-counter transaction.

- `PointerMovesReceived`: calls to PointerMoved, including scrollbar-captured moves.
- `PendingMovesProcessed`: positions consumed by ApplyPendingPointerMove, including internal visibility-driven settling. This is not necessarily one physical move or one event callback.
- `PendingMovesCoalesced`: a pending position superseded or canceled by a later PointerMoved. This measures existing behavior only. It excludes pending positions discarded by leave/reset/rebuild.
- `SameTargetMoves`: synchronous hit equals current hovered target.
- `PointerBarriers`: press, release and leave API calls, including their early-return paths. `PointerBarriersWithPendingMove` observes which arrive with pending hover. These counters do not claim that the pending queue was flushed, and do not count keyboard/wheel/capture transitions separately.
- `CursorQueries`: CursorAt calls, including early returns. `CursorResolutions`: CursorFor calls, including settling. Neither means an OS cursor actually changed.
- `HitQueries`: host HitTest calls, not native boxes visited or cache hits. No new cache was added.

The existing primary metrics constructor/deconstruction parameters are unchanged; counters are additive init properties. Metrics contain no pointer coordinates.

## Baseline traces, not a silently changed contract

The tests freeze these existing behaviors with instrumentation off and on:

1. Shared ancestors retain hover state; mouseout bubbles through the ancestor before mouseover bubbles through it. This is not a separate mouseenter/mouseleave implementation.
2. Moves replace pending targets; A→B→A before a frame may cancel a pending move.
3. **Known gap against the later design:** press/release do not flush pending hover. A move→down→move→up sequence can deliver down/up/click before a subsequent frame delivers the pending mouseover. The test deliberately asserts that baseline. Any correction needs its own reviewed change and deliberate test update.
4. StopPropagation suppresses ancestor handlers.
5. An outgoing callback can hide the incoming target; it is not delivered mouseover. The fixture uses the existing private state/invalidation methods by reflection solely to produce this mutation without rebuilding identities.
6. Cursor updates synchronously, the last pending move runs without another physical move, and repeated idle renders add no style resolutions or pending moves. The harness calls Render explicitly; it does not claim no backend paints happen when a caller forces Render.
7. Leave cancels pending hover. Oscillating `:hover { visibility:hidden }` remains bounded and then idle with no extra hit/style work.
8. Selected exact counter counts, disabled counters and reset behavior are asserted.

## Chromium comparison

Inspected pinned Chromium revision `2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9`:

- `third_party/blink/renderer/core/input/boundary_event_dispatcher.cc`, `BoundaryEventDispatcher::SendBoundaryEventsSameOrigin`: out before over, common-ancestor handling, and the separate non-bubbling leave/enter phases.
- `third_party/blink/renderer/platform/widget/input/main_thread_event_queue.cc`, `MainThreadEventQueue::ShouldFlushQueue` and `MainThreadEventQueue::IsRafAlignedEvent`: discrete events versus frame-aligned moves, with low-latency/unbuffered exceptions.

These references motivate later work; this phase does not import Chromium semantics or claim browser parity or a CPU improvement. Linux traces do not certify Windows input-to-present latency, full existing .NET Windows tests, or the Validator Windows host build/runtime.

## Optional full-pipeline baseline

Build Release and run the executable with `--baseline`. It requires a native library built with work counters enabled and emits CSV for 100/500/1000 cards, with no owner and with static identity-transform/backdrop owners. Every measured sample calls PointerMoved then Render, traversing EnsureVisualTree and PrepareTransitionContext. Four warmup moves precede ten samples per case. There are no hover-dependent declarations. It checks zero style updates/layout builds and that the current full native compute/context scans actually occurred; this is deliberately a baseline gate to revise when a reviewed optimization lands.

Output includes total native work, total managed thread allocations and process-wide GC deltas, plus observational wall p50/p95 and process CPU milliseconds. Ten samples are a small diagnostic sample, not a stable latency benchmark. Managed allocation bytes exclude native allocations; process CPU/GC may include runtime background work. There is no hard timing threshold.

Linux/.NET 10.0.201 observation on the phase-A implementation: all three owner modes had 1020/5020/10020 compute nodes and transition-context entries for ten moves at 100/500/1000 cards, respectively. Style updates and layout builds were zero in every case. The corresponding managed allocation totals were 90240/378240/738240 bytes; no GC collections occurred in those short samples. This measures existing linear work despite no visual effect, not an improvement.

## Integrated checkpoint validation limits

The A2 trace harness was also rebuilt and run with the B1/B2 persistent-backing
native library: all 16 traces passed. The nine baseline scenarios still report
N compute and transition-context visits despite zero style updates/layout.

The existing `AetherUI.Tests` suite was cross-built for its declared Windows
framework and executed on Linux using .NET 10.0.201. Both the baseline before A2
and the A2+B1/B2 integration passed 96/100 tests, with the same four failures:
flex G8 cross-offset expectations (35 versus 30), calc(2) line-height (2 versus 7),
and two file-image tests using Windows-style paths in Linux. No assertions were
removed and no tests were skipped. This result is a differential regression
check, not a full managed-suite PASS or Windows execution. These failures must
remain visible rather than becoming automatic exclusions.
