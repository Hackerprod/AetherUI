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

## Separate test-only allocation gates

The production counter above still reports unavailable. Real allocation measurement
is deliberately in test harnesses, not an observer API invoked by product commit.

### Native link-unit oracle

`aether_dom_commit_allocations_test`, registered as CTest
`aether.dom-commit-allocations`, is supported on Linux GCC 64-bit without LTO or
allocator sanitizer interposition and with static libstdc++ available. Other
configurations return 77 and print `UNAVAILABLE`. The dedicated Linux CI job
requires an actual test result; a skip is not an approved allocation gate.

The executable links every object of `aether_publication_test_core`, whose source
list is obtained from all `aether_core` product sources. It does not link the
product DSO. It statically links the C++ runtime. Build-time `nm`/`readelf` checks
require exactly one DOM commit implementation, all replacement-new/wrapper
symbols, and no `libaether_core`, `libstdc++` or `libgcc_s` DSO dependency. A generated
source/object manifest and symbol/dependency report are retained as CI artifacts.

The harness wraps malloc, calloc, realloc, aligned_alloc and posix_memalign. All
scalar/array, throwing/nothrow, aligned/unaligned new variants call precisely one
wrapped C primitive; matching delete forms free the allocation. Only the primitive
wrapper counts the request, so new plus malloc is not double-counted. TLS counters
contain scalar data only. Wrappers call `__real_*` and do not log, allocate metadata,
invoke an observer, or call user code. Recursion, imbalance or counter exhaustion
marks a sample invalid, never zero. Thread isolation, nesting, disabled collection,
all thirteen C/C++ positive-control families, and real product preparation are
checked independently. Setup and teardown have separate measurement boundaries.

The first typed commit is measured before commit warmup. Cold allocation is a test
failure, not an exemption. Subsequent cases exercise attributes, State, runtime
value, text/style slots, structure, adoption, forest/document materialization and
interaction claims. Invalid and repeated commits are checked separately for zero
requests and preserved inputs. The unit is allocator requests, including failed
requests, not net memory use.

Coverage is the linked product and static C++ runtime. It excludes private libc,
loader and other external-library allocations, the CLR and other threads. The
commit call-path audit covers `dom_edit.cpp` commit/validate, typed touch and journal
append in `dom_types.h`, interaction stage swaps, and style-source batch swaps in
`style_runtime_core.cpp`; these paths do not intentionally delegate allocation to
an unwrapped external allocator. A new such call requires extending or invalidating
coverage. This Linux test does not establish Windows/macOS or whole-process coverage.

### Managed publication intervals

Run `bindings/dotnet/tests/run-dom-commit-allocation-tests.sh DOTNET NATIVE_LIBRARY`.
It builds isolated artifacts with `AetherDomCommitAllocationTesting=true`, defining
`AETHER_DOM_COMMIT_ALLOCATION_TESTING` only for Core and StylePublicationTests. The
ordinary build omits both the ledger type and call sites, and its runner checks the
type is absent. Product feature defaults are unchanged.

The internal test-only ledger reads `GC.GetAllocatedBytesForCurrentThread` directly.
It has no delegate, observer, event, collection, formatted string, logging or throw
inside a measured interval. It measures the nine existing public Node/Element
mutation commit-plus-mirror/version segments, the certified import-publication
segment, and the interaction commit. It counts successful/failed scopes and their
bytes separately; nesting counts bytes once. Tests demand a nonzero, exact expected
scope count so missing instrumentation cannot pass as zero.

Staging, prepared-object disposal and notification are outside these publication
intervals; their whole-operation allocation is reported separately, never claimed
zero. Staging errors must enter no commit scope and preserve inputs. A rejected
commit remains a failed scope with measured exception bytes. A notification error
after publication leaves a successful commit and is not rollback. Positive GC
allocation, empty scope, disabled collection, imbalance, nesting and TLS controls
validate the measurement. Cold publication paths are measured first; every cold
allocation inside a successful commit fails just like a warm allocation.

The managed measurement covers GC-attributed allocation on the calling thread,
not native allocation, JIT internals or another thread. Component and transition
publication and any other segments outside the explicitly listed intervals remain
**unmeasured** by these gates. Full transactional, OOM, lifetime and sanitizer tests
remain independent requirements; allocator counts do not replace their oracles.

### Cold managed P/Invoke resolution

The first measured State publication exposed 96 GC bytes: 48 in the first native
validate import and 48 in the first commit import, then zero on the same stubs.
The generated SafeHandle marshaller was independently measured at zero. Selective
`Marshal.Prelink` before publication removes those first-resolution allocations
without invoking native commit. Reflection/linking itself is fallible staging and
has nonzero cost; it is not excluded from a whole-operation claim of zero.

DOM binding activation prepares small source-generated import groups before
capturing binding snapshots, links or versions. It rechecks disposal/binding after
linking because a loader resolver may run application code. Defensive low-level
preparation remains. A CAS state has unprepared/busy/ready values; only the winning
initializer can reset it after failure. Busy reentrant or concurrent calls fail
explicitly and may be retried; no lock/wait is held around a resolver. Ready is set
only after the full declared group links. There is no fallible static constructor,
SafeHandle lifetime change, dummy commit, or relaxation of the cold zero-byte gate.

The helper preserves reflected method metadata and checks exact import count.
Unsupported linking, missing metadata/exports and OOM fail preparation instead of
reporting ready or measured zero. Tests exercise retry after a failure between
imports, same-thread and other-thread busy attempts, one successful readiness and
missing metadata/export. Execution evidence is CoreCLR 10 Linux; no AOT, trimmed,
Windows or macOS execution certification is inferred from that evidence.
