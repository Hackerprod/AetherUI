# D2: certified deltas and effective context owners

## Candidate and scope

Product source checkpoint: `0d715cfde53e03b84c08ae75ed56eec39608a2f6`, based on D1 partial `c577a1a58484d3e1c355b2281e79eadc7be56561`. Isolated checkout only. No C3c/C3d code was imported; any combined-tree measurements belong to the separate combined-test artifact, not this candidate. No main/PR/default activation.

Both CMake options `AETHERUI_ENABLE_STATE_INCREMENTAL` and `AETHERUI_ENABLE_CONTEXT_OWNERS` are OFF by default. Managed `UiDocumentHost.ExperimentalStyleDeltas` is internal and false by default. Diagnostic counters are also OFF by default. The ON build enables all three explicitly for gates.

Implemented the approved §§9/9.1 consumption/context work. D1's pending DOM synchronization, joint native Observe/Tick transaction, replacement-handler and mixed native/managed R2 reconciliation contracts remain separate. This is not a claim that the complete D1–F pipeline is finished.

## Implementation

### Compact semantic projection

- `style_revision.{h,cpp}` retain a bounded compact list of semantic/visual positions produced by `CertifiedRevisionDelta`, plus an authentic revision identity marker for the exact predecessor. No old-revision chain is retained. Full logical enumeration APIs are unchanged.
- `MapVersion` stores a private factory-certified projection source. `create_publication_map` constructs its own complete or compact patch and does not accept an arbitrary backing/count as proof. Compact coverage requires the transaction's exact authentic base source, stable order, no retirement and equal complete measure/environment context. Document/viewport/DPI/root-font/environment changes fall back to complete staging.
- Dedicated additive D2 exports in `aether_state_style.h` separate coverage, semantic count and impact. Failure is never zero changes. `get_publication_delta` omits the flat projection handle only when full frozen-style comparison proves it equal; the native semantic map patch still publishes metadata changes.
- `StylePublicationInterop` and the host use persistent immutable dictionaries. A no-op preserves managed projections. Changed identities stage only their indexed primary/paint boxes. D1's publication guard and post-callback checks remain in force; mutable managed boxes are assigned only in the existing publication segment. Their public live-object contract is not changed.
- Visibility, stacking, layout/text invalidation and the context-triggered pending-target reset remain. Metadata-only deltas do not start false transitions. The host still uses legacy partial maps; contextual override acceptance remains exact-revision.

### Persistent owner causes and membership

- `context_owners.h` defines immutable Patricia-index snapshots, authentic node tokens, retained lineage and membership serial. Static transform/backdrop bits and active-transform membership are independent. Effective membership has a separate snapshot; changing a cause without changing the final union reuses it exactly.
- Both `StyleRevision` and legacy backing factories maintain source causes from certified changes. These indexes contain declared causes, including generated nodes still detached during staging. Actual attachment/liveness/type/anonymous-owner eligibility is checked by the effective union and full helper. This distinction fixes the independently reproduced staged-generated defect.
- The engine certificate covers actual document token/structure epoch, map topology source, current style-tree epoch, static lineage/root/serial and independent active-transition lineage/root/serial. Generation alone never authorizes reuse. No token-wide validation scan is substituted for the certificate.
- The transition runtime snapshots active TRANSFORM only, including delay. Matrix-only ticks/opacity/retarget with unchanged membership reuse the source. Finished endpoints are excluded. Cancellation/finish/dead-owner retirement update source membership. Tokens, not recycled addresses, decide identity. This membership serial is not the future general `TransitionMutationSerial`.
- Legacy Observe/Tick vector operations are still not jointly transactional. If an allocation prevents certified owner maintenance, the source is explicitly uncertified and context preparation falls back to the full helper. This does not claim rollback of earlier legacy vector mutations.
- Nullable-token private legacy records remain supported. An incremental unbound-entry count marks their backing coverage unknown. The complete helper captures the genuine token from live DOM for that operation; no raw address grants reuse. Historical fallback queries compare retained immutable entries without dereferencing DOM and count their linear visits. A known dead document is skipped. The nullable metadata/API contract is unchanged. The unchanged external legacy reproducer reports owner count 0 before the fix and 1 after it.
- Source index diff walks shared persistent paths and updates the final union by identity. It does not copy an N-element set, nor retain a chain of prior snapshots. An effective owner with multiple causes is removed only after the last cause disappears.
- An engine one-shot pin carries its selected map/source and immutable membership. The next layout consumes it even on an invalid argument path; wrong/unknown source takes the full fallback. Clones/old frames retain old snapshots. Historical membership lookup resolves identity through retained immutable assets, without dereferencing released raw DOM.
- With context optimization OFF, full preparation/layout recomputation remains enabled and the cache/union/one-shot reuse fast paths are disabled. Immutable representation and lifetime safety are still compiled.

### Counters and explicit limits

`aether_context_work_counters_get_v1` is additive; the original counter ABI is unchanged. It records calls, backing/active/static visits, updates, builds/reuses, fallbacks, persistent-node diff visits and attempted context allocations, including temporary full-build vectors and failed attempts. Source work performed during backing/revision/transition mutation is counted there, not moved outside measurement.

Fallback reasons: 1 first use, 2 unknown/stale coverage, 3 document/topology/lineage change, 4 disabled option, 5 incompatible one-shot pin. OOM returns failure, never no-op success.

The zero-box claim is only D2 projection staging/publication. The unchanged hit-test still visited 1,194 / 5,994 / 11,994 boxes over twelve moves in the 100 / 500 / 1000 fixtures, respectively. E is not implemented here. Paint chunk lookup still uses a linear search and composition/renderer flattening may be global. No O(K) render or Windows CPU improvement is claimed.

## Gates and exact source attribution

The current product source is `0d715cf`. It adds the legacy fallback and its regression/OOM coverage. The prior checkpoint `0e4c781` separately completed ON 244/244 and OFF 242/242; those old binaries are not evidence for the current commit.

ON native library SHA-256: `3a6a0fe518f7f9a181a86358f6447fe9c4df70fac21d7e7e8fdd16cf6fc8e939`.

- Full ON CMake build and CTest: **244/244 PASS**, `ctest-on-legacy-final.log`. Full OFF: **242/242 PASS**, `ctest-off-legacy-final.log`. OFF native SHA-256: `42f84dd7e1dd483454af80ac8270e7de3155e7e1a3117bbde979d13c6c68c638`.
- Native D2: **2327 ON / 2325 OFF checks PASS**, including randomized persistent diffs, compact provenance, metadata-only/context fallback, combined causes, delay/active/retarget/finish/cancel, contextual pin, generated staging, document destruction and retained native clones.
- D2 registered OOM gate: **596 ON / 721 OFF checks PASS**. Compact staging rejected 15 injected allocation points in each build; context preparation rejected 2 ON / 7 OFF; full legacy fallback rejected 5 in each. Disabled optimization deliberately takes the complete context path. Base maps/old frames survive, same-operation retry works.
- .NET D2 CTest: nine real PointerMoved→Render→EnsureVisualTree no-op fixtures (100/500/1000 × 0/1/N static owners) plus transition lifecycle, paint/metadata/backdrop/multiple-cause and document-replacement cases PASS. After warmup each no-op fixture had 12 context calls, 0 backing/static/active visits, 0 snapshot builds/allocations/fallbacks, 0 resolver/map copies and 0 projection box visits/layout/display-list/chunks/paint. The full helper oracle runs outside each measured interval.
- Exact external generated repro was copied unchanged and registered as `aether.generated-context-staging`. File SHA-256 `4fc16150b6a94fd91bd74b69300d1e7bfba5e2e11858234747e5b35b67066d3b`. Old combined checkpoint failed with staged static owners=0 and FULL oracle equal=0; corrected candidate passes with 1 and 1. The second value is a boolean agreement, not a count.

Current-source managed D1 host **22 PASS** and input traces **16 PASS**, in both ON and OFF (`managed-d1-traces-legacy-final-on.log` and `managed-d1-traces-legacy-final-off.log`). Current-source full managed suite **96/100** in both builds, exactly the four inherited Linux failures (`managed-suite-legacy-final-on.log` and `managed-suite-legacy-final-off.log`): Flex alignment preserves signed residual space; Calc retains dimensional types in length and line-height contexts; File image loader decodes a file PNG; Composite image loader falls through schemes. Windows-targeted managed Validator cross-build PASS (`windows-crossbuild-legacy-final.log`), no Windows execution. Native library bytes were explicitly copied and SHA-256 verified in each managed output. Dedicated current D2 output is `managed-d2-legacy-final-on.log`.

Do not reinterpret the earlier stale `d2-ctest.log` as evidence; its build failed. `d2-ctest-current.log` was a later focal PASS; `ctest-on-final.log`/`ctest-off-final.log` belong to `0e4c781`. The current complete gates are `ctest-on-legacy-final.log` and `ctest-off-legacy-final.log`.

## Chromium evidence

The directly inspected files, functions, pinned URLs and decisions are documented in `native/docs/hover-d2-chromium-comparison.md`; exact SHA-256 values are in `native/docs/chromium-d2-sources.json`. Additional containing-block/style sources were fetched from the official repository at `2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9`, not inferred from API names. Chromium is not claimed to use AetherUI's persistent-index representation or its narrower supported CSS semantics.

## Remaining boundaries

Independent review of the final legacy correction remains required; these are implementation-run gates, not an independent PASS or permission to activate/integrate. No real Windows execution, input-to-present/CPU ms/s evidence, renderer modularization or hit-test optimization is included. The final renderer may still traverse visible commands. D1 synchronization/transaction blockers are not hidden by this no-op result.

## Reproduction and artifact manifest

`REPRODUCE.md` supplies commands. `GATES.json` records source hashes, native build options, binary hashes, test result counts and evidence file hashes. `CHANGED-FILES.txt` and `COMMITS.txt` describe the complete candidate relative to D1 partial. `legacy-before-after.log` records exit 1 / exit 0 for the unchanged external null-token legacy reproducer. The product source tested is `0d715cf`; a later documentation-only commit does not change those binaries.
