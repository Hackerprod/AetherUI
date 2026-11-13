# R0c: preparation-effect evidence (experimental, OFF)

`AETHER_EFFECT_CAPTURE` defaults to **0** in the runtime. CMake exposes an
OFF-by-default option of the same name. It is independent of the existing
`AETHER_REUSABLE_PAYLOAD_CAPTURE` R0b compile macro. Neither option enables reuse.
Production OFF contains no R0c storage, event construction, or new allocation
failure points. All compute calls, synchronous property callbacks and audits,
source accounting, publication and ordered diagnostic replay still run.

## What is captured

* Each `compute_node` attempt, in execution order, **including computed but
  discarded pseudo-elements**. Identity is an owned host identity token plus
  pseudo kind and sequence, never a synthetic-node pointer. A content-presence
  flag records why a pseudo attempt may subsequently be discarded.
* Inline source accounting **before** aggregate merge, together with complete
  operational budget input/output (counters and limits). No saturated aggregate
  subtraction is used. Each event also records aggregate and budget boundaries.
* Separate global sheet-aggregate, DOM link, DOM style, and text-budget-loss
  events. Sheet-aggregate is an observation of preparation's existing sheet
  merge, not a reconstruction of earlier add-stylesheet callbacks.
* Diagnostic ranges into one owned `shared_ptr<const vector<...>>`. Strings
  preserve null versus empty and duplicates. Freeze moves the existing log;
  there is no second copy of message strings. No callback means no diagnostic
  backing allocation; event `diagnostics_captured` is false, not proof of zero
  diagnostic effects.
* A conservative **possible-effects taxonomy**, not invocation telemetry:
  parse, parse_failure, css_wide, inherit_explicit, inherit_context, shorthand,
  apply, apply_checked, apply_contextual, apply_contextual_checked, value_audit
  are all UNKNOWN, including built-ins. No purity certification exists.

Snapshots also retain document/root identity tokens and source/publication generation,
input, sheet, structure and style-tree epochs for correlation with the revision.

`capture_finished` means only that this preparation's evidence was frozen.
`effects_complete` is always false. This is not a complete effect/reuse
certificate and not an effect replay implementation. There is no public API,
consumer, skip-compute branch, callback/user_data retention, or borrowed argument
in the published evidence. Identity tokens retain lifecycle state as in R0a;
that state is not a historical DOM snapshot.

All fallible capture/freeze operations precede publication. Any OOM aborts the
transaction and leaves prior revision/effect snapshot intact. The snapshot swaps
in the existing no-allocation commit boundary before diagnostics replay. No
runtime access is added after replay, so nested publication or runtime destruction
from that callback keeps the existing semantics. Aborted attempts are not
published as a partial snapshot.

## Reproduction

No packages, network, Actions, SDK changes or Chromium required:

```
sh tests/native_hover_baseline/build.sh /absolute/new-build
sh tests/native_effect_capture/run.sh /absolute/new-build
sh tests/native_effect_capture/verify.sh /absolute/new-build
```

`measure_cost.py BUILD EVIDENCE` expects prebuilt drivers `base-0` (R0b OFF),
`base-1` (R0b ON), `off` (R0c OFF), `effect` (R0c only ON), and `both` (both
captures ON). Build each with `test_reusable_payload_cost.cpp`, the matching
source tree, `-O1`, and `AETHER_HOVER_PROBE=1`. The captures use their respective
macros with values 0/1. Nine alternating-order samples per size; median and
nearest-rank p95. Requested allocation bytes are **not process RSS**. `live_delta`
is measured while retaining the preceding style revision, not a retained-effects
history stress test. This is native-only diagnostic work, not host/.NET or Chrome
performance. Timings do not establish a speedup; compute counts must remain equal.

Evidence files record the executed checks; a focused PASS is not a full renderer,
CSS completeness, sanitizer, or Chromium validation.

## Executed validation

* Clean manual build: 70 non-runtime objects and fresh runtime OFF/ON.
* Atomic revision suite: OFF 5389 / ON 5435 checks, 22 aligned failures each.
* Provenance OOM sweep: OFF 289 / ON 296 failure points.
* Canonical OFF/ON equality: 21 fixtures, each cold/transition/nochange plus
  retained snapshots, ordered diagnostics, source accounting and budget rejection.
* Focused capture suite: owned diagnostics (null/empty/duplicates), discarded and
  retained pseudo attempts, hidden nodes, sheet/link/style effects, saturated
  aggregate with nonzero local inline contribution and all 12 budget fields,
  real text limit exact/+1 boundary, all 11 callback families and synchronous
  trace equality, callback removal/install, nested publication, runtime destruction
  during replay, snapshot lifetime, ordinary allocation failure sweep.
* Existing R0b payload suite passes with both capture macros ON.
* Public headers unchanged; 27 existing `aether_` runtime symbols unchanged.

The ordinary sweep tests capture vector growth and final frozen allocations;
existing atomic tests additionally cover aligned allocation failures. No ASan,
TSan, external host, Chromium, or CMake configure run was performed. Costs are
experimental overhead, not optimization evidence. Default OFF memory counters
match R0b OFF exactly for all measured samples.

For 1800 cards / 3603 computed original nodes, R0c capture adds 16 allocations,
5,046,008 requested bytes and 2,523,320 retained bytes on cold publication over
the corresponding R0b mode (payload OFF or ON). Repeated publication frees the
old runtime-owned effects snapshot, so its net live delta is not that retained
size. Peak-above-start rises 2,523,304 bytes. Timings are noisy: inspect the raw
nine-sample CSV/p95 instead of interpreting small median differences as gains.
The capture stays OFF; this is an observability cost, not a hover optimization.
