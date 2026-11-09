# DOMsync diagnostic work counters

This implements the diagnostic portion of `managed-dom-sync-proposal.md` §12.
It does not change the approved contract or any experimental feature defaults.
The original `aether_work_counters_v1_t` layout remains thirteen `uint64_t` fields.
The additive `aether_dom_work_counters_v1_t` has its own header and entry point.

## Collection and meanings

Configure `AETHERUI_ENABLE_WORK_COUNTERS=ON` to compile collection. It remains
disabled on every thread until explicitly enabled. The original enable/reset API
controls both native snapshots. Reset does not enable collection. Counters saturate
and include abandoned work. They contain no node identities, positions or values.
`available` distinguishes a supported diagnostic library from a zeroed production
library; `enabled` reports collection state on the calling thread.

Managed diagnostics should use `NativeDomWorkCounters.TryEnableForCurrentThread()`
to enable the matching managed call-site hooks, then reset/read/disable on that
same thread. Hooks otherwise return before P/Invoke. Enabling the older native-only
counter API alone does not enable managed call-site reporting.

- `DomTokenReads` counts entry to the live DOM-token read, including rejected reads.
- `DomSnapshotBuilds` counts build attempts after the initial valid-token read.
  Snapshot node, attribute and child counters count actual iterations, including
  iterations of a snapshot later abandoned.
- `DomNodesImported` counts managed payloads prepared at the mapping/import sites,
  including a later abandoned import. It is not inferred from snapshot node count.
- `DomEditPrepareAttempts` counts entry into native staging after basic argument
  checks. `DomEditsPrepared` counts successfully returned handles.
  `DomEditsCommitted` counts a native commit. `DomEditsAborted` counts a failed
  staging attempt or destruction of a successfully prepared uncommitted handle.
  Enabling/resetting halfway through a handle's lifetime can split these counts.
- `TransitionCandidatesCloned` counts newly allocated transition storage in style
  candidate preparation or atomic Tick. `TransitionEntriesCopied` counts entries
  after each completed active/finished-vector copy. No-op candidates that share
  storage increment neither counter. Partial allocator work is not measured here.
- `ManagedDomParseCalls` and `ManagedDomRebindCalls` count calls at the managed
  bridge sites only. They are not global native parser or stylesheet counters.
- `ManagedDomAttributesVisited` and `ManagedDomChildrenVisited` describe imported
  payload traversal. Together with native snapshot traversal they expose the
  global snapshot/import route; they are not a counter for every traversal in
  style, layout, rendering or arbitrary application code.
- `DomImportInitialBinding` and `DomImportTokenChanged` record the two reasons
  for entering the binding's full import path, including unsuccessful attempts.

## Allocation coverage remains unavailable

`DomCommitAllocationCoverage` is zero in this implementation. Consequently
`DomCommitAllocations` is **unmeasured**, even if its numeric storage is zero.
The managed test prints `UNMEASURED` and does not assert measured zero. There is
no allocator callback, observer, logger, allocation hook or added user delegate
inside commit. A real allocator-observed gate remains separate and pending.
Existing allocation-failure tests are not silently relabeled as this measurement.

## Tests and claim limits

`DomSyncWorkCostTests.Run()` uses real P/Invoke and has positive controls for
snapshot/import traversal, initial binding, native R2 import, managed parse/rebind,
typed edit commit/abandon, transition cloning and actual vector copying. It also
checks disabled collection, reset semantics, thread isolation, saturation and the
original ABI size. An unsupported native library prints `SKIP`, never `PASS`.

After warmup it performs eight public hover State edits plus host Render on
100, 500 and 1000 cards without hover rules. It requires zero snapshots/imports,
managed parse/rebind calls, snapshot/import attribute-child traversals, transition
clones/copies and import fallbacks. It records token reads and edit counts, and
checks preserved handles/wrappers and a current projection. This is a structural
work gate, not a CPU benchmark. It does not establish Windows CPU improvement,
all-engine traversal complexity, structural incremental styling or allocator
coverage. Running and independently validating it on a frozen revision is required
before claiming any of these tests passed.

## Chromium source consulted

Inspected the local pinned `element.cc`, `Element::SetHovered` at line 13552:
[Chromium source](https://github.com/chromium/chromium/blob/2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9/third_party/blink/renderer/core/dom/element.cc#L13552).
It returns for unchanged hover, updates user-action state and declares style
invalidation based on hover dependency. It does not rebuild/import the document.
That separation motivates the Aether bridge work gate. Chromium was not executed,
and this does not claim Blink implements Aether's snapshot, rollback or ABI.
