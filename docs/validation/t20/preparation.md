# T2.0: accounted inputs are not typography support

This increment records bounded diagnostic evidence from the existing CSS compiler before discarded input disappears. It does not enable shaping, paragraph grouping, font lookup, rasterization or the normal text issuer. `coverage_complete` remains false. Context is unproven and font resolution is independently pending, even when all inspected inputs were accounted for.

## Source accounting

Every compiled stylesheet owns a scalar accounting record, including sheets producing zero rules. The real declaration/selector/at-rule branches record unknown, rejected and malformed declarations, discarded selector members (including nested forgiving lists), at-rules, conditional sources, incomplete parse and unresolved imports/font-face sources. No second CSS parser or cascade is added. Unknown at-rule contents are represented as an at-rule loss, not individually interpreted declarations.

Preparation accounts for all current sheets, irrespective of selector match or condition, and every actual inline compilation. A shared operational document budget admits at most 16 MiB compiler input and 65,536 declarations/rules. Adding a sheet seeds its budget with all live sheets; revision preparation starts with that aggregate and shares the remainder across all inline inputs. The compiler reserves source bytes before allocating the original/cleaned views, declaration slots in the real scanner before growing its spans or constructing declarations, and rule slots before constructing/parsing the next selector or at-rule. An exhausted budget aborts the whole compile/preparation with `AETHER_ERROR_INVALID_STATE` / `AETHER-CSS-LIMIT`; it does not publish styles with the excess silently ignored. Source accounting, rule IDs, caller output and shared usage remain unchanged on failure. The previous published revision survives.

Private work counters observe admitted original/cleaned source-view bytes, stored declaration spans and started rules, including failed attempts. They are not a count of every temporary allocation or lexer operation. Structural indexing and token internals are bounded by admitted source bytes; this does not claim instruction-by-instruction parser or peak-byte allocation accounting. Diagnostic evidence remains constant-size with no raw values or resource paths.

`inputs_accounted=true` permits known losses: it says input and losses were accounted, not supported. Unresolved imports/font-face, DOM link/style sources without a verified correspondence to registered sheet handles and incomplete parses make it false. Raw-text diagnostic budget exhaustion also makes it false; CSS operational budget exhaustion rejects the transaction instead. Treating all DOM link/style nodes as pending is deliberately conservative and may cause false negatives. Sheet removal changes the next aggregate; previous revision snapshots remain immutable.

## Frozen raw text diagnosis

A separate source-only type copies direct text-node bytes and one provenance range per node, without CSS whitespace normalization, shaping, layout grouping or a font seal. Runtime snapshots share the document accounting and retain the direct text diagnosis per element. The cumulative limits are 1 MiB raw text, 65,536 nodes/ranges/requests. Checks precede retained byte copies. On diagnostic budget exhaustion the global input state is incomplete; preceding diagnostics may remain readable but cannot become authority. Historical epoch labels do not prove currency.

No text metrics, font fallback or typography completeness is inferred. This is **not** a migration of the synthetic paragraph authority to production.

## Transactions and currency

All new records are staged before the existing revision publication boundary. Allocation failure, cancellation (destroyed preparation transaction), mutation and sheet-epoch changes cannot partially publish them. `typography_inputs_current` additionally checks runtime ownership thread, exact current revision, current sheet epoch, live document input/structure/style epochs. An operational caller must check before and after callbacks; historical `TypographyContext::valid()` still means ownership consistency, not liveness.

## Manual verification

```
sh tests/isolated_capture/build.sh NEW_BUILD_DIRECTORY
sh tests/isolated_capture/run_t20_runtime.sh BUILD_DIRECTORY
sh tests/isolated_capture/run_t20_paragraph_diagnostic.sh BUILD_DIRECTORY
sh tests/isolated_capture/run_t19_runtime.sh BUILD_DIRECTORY
sh tests/isolated_capture/run_t19_context.sh BUILD_DIRECTORY
```

No workflow or Actions setting is changed. Chromium remains NOT_RUN. The original Flexbox fixture is not modified and remains rejected without PNG output.

### Initial verification before R1 correction (Linux/GCC, 2026-11-13)

Clean manual build: 71 core objects, 9 support objects, normal and limit-probe CLIs. Final two accounting consumers were rebuilt and both CLIs relinked after an overflow-guard hardening.

- T2.0 runtime: 3,530 checks, 343 injected preparation OOM points; normal issuer absent.
- T2.0 raw diagnostic: PASS, 3 allocation-failure points; limits, cumulative budgets, cancellation and immutable ownership.
- T1.8 parser PASS; runtime 2,768 checks / 375 preparation OOM points / zero commit allocations.
- T1.9 runtime 4,496 checks / 420 preparation OOM points; context 115 checks.
- T1.7 synthetic contextual layout 1,510 checks; normal/test issuer isolation PASS.
- H21 style 1,491 checks / 226 OOM points; H20 style 1,263 checks / 226 OOM points.
- Original fixture: 474,495 bytes, SHA-256 `eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`; final normal CLI rejected `PREFLIGHT_UNSUPPORTED`, no PNG. Child-only rejection accounting: 50.8852 ms wall, 0.04327 s CPU, 10,200 KiB peak RSS. These are rejection-process metrics, **not drawing performance**. Chromium NOT_RUN.

Compiler warnings in unchanged measure-state and CLI code remain; this is not a warning-clean build claim. Independent code review is required before publication.


### R1 review correction

Independent review correctly rejected the initial diagnostic-only CSS counters: 65,537 custom declarations were retained despite a saturated 65,536 counter. That implementation did not meet the approved work-admission contract and was not published. R1 adds the operational budget described above, separate from loss evidence. Reproduction now must reject at 65,536 admitted spans with no published declarations, while the exact boundary accepts. Tests also cover cross-sheet and sheet-plus-inline exhaustion, exact/exceeded source/rule limits, zero source copying on source rejection, shared-budget rollback under OOM, and unchanged previous revisions.

R1 clean rebuild: 71 core + 9 support objects + 2 CLIs. Final results:
- Compiler budget negatives PASS: both 65,537 stylesheet and inline custom declarations rejected, 65,536 spans admitted, no partial output; exact 65,536 accepted. Oversized-source rejection copied zero bytes. Rule and source exact/exceeded boundaries PASS. Compiler rollback: 19 OOM points.
- Runtime: 3,612 checks / 343 OOM points, including cross-sheet, sheet+inline and two-inline aggregate limits, previous revision retention and normal issuer absence.
- Raw paragraph diagnosis: PASS / 3 OOM points.
- Rebuilt T1.8, T1.9, T1.7, H20 and H21 regressions repeated successfully with the same counts listed above.
- Final original fixture rejection: 61.9661 ms child-process wall, 0.052651 s CPU, 10,216 KiB peak RSS; `PREFLIGHT_UNSUPPORTED`, no PNG. Hash unchanged; Chromium NOT_RUN. These remain rejection measurements, not rendering speed.


### R2 selector expansion correction

Independent re-review found a second gap: source-unit limits did not bound the selector-list product. A 257-selector rule with 257 declarations retained 66,049 declaration copies despite a single source-rule charge. R2 admits the **whole expansion before the copy loop** with checked multiplication/addition and document-wide quotas: at most 65,536 compiled rules, 65,536 expanded declaration entries, and 32 MiB logical copied declaration/rule payload (structures and string contents including terminators). A configured source-rule limit also caps compiled-rule count. These are logical retained/copy units, not a promise of exact heap consumption: allocator overhead, vector spare capacity and already-admitted parsing temporaries are not measured bytes.

All four declaration vectors participate (longhands, custom properties, content and deferred shorthands). Resolved shorthand output is counted by actual compiled longhands. Quotas are retained per sheet, seeded across live sheets and into inline preparation; inline output charges one retained set. Keyframe rule records charge their retained rule structure without replicating their shared parsed payload. Source limits still bound their parsing. Overflow or exhausted expansion quota rejects the entire staged operation. The copy observer is incremented only after admitted declaration-vector copies; rejection before the loop must leave it zero. No rendering authority is added.

R2 verification: clean build 71 core + 9 support + 2 CLIs; the compiler and both CLIs were subsequently rebuilt/relinked after factoring the checked product helper for overflow tests. Runtime 3,731 checks / 343 OOM points. Expansion tests: 257×257 rejected with zero declaration copies; 256×256 accepted with 65,536 copies; cross-sheet/inline exhaustion, two-inline shorthand expansion, compiled-rule cap, payload-byte exact/exceeded limits, integer-product overflow and 30 expansion OOM rollback points PASS. R1 negatives still PASS / 19 compiler OOM points. Raw diagnostics and T1.7/T1.8/T1.9/H20/H21 regressions repeated PASS with prior counts. Normal issuer absent. Original remains unchanged/rejected without PNG: child rejection 61.0262 ms wall, 0.051891 s CPU, 10,444 KiB peak RSS; Chromium NOT_RUN. Independent re-review is still required.
