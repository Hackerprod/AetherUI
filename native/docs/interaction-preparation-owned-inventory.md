# Interaction-owned snapshot and preparation storage

This is an infrastructure tranche, not the interactive ABI or a G1 acceptance claim. No legacy ABI layouts, authored HTML/CSS grammar, or static profiles are changed.

## Ownership and allocation inventory

The interaction path uses the common `OwnedAllocator<T>` and session domain. Allocation requests, including allocator bookkeeping and alignment storage, are reserved before `operator new`. Rebound allocations for hash nodes and buckets retain their domain until deallocation. No platform-specific STL capacity formulas, compiler exclusions, global production allocation hooks, or PMR conversion of the engine are used.

| Site | Storage/category | Lifetime |
|---|---|---|
| `InteractionSnapshot::reserve/append` | `OwnedVector<InteractionCommand>`, paint | Snapshot; old/new vector allocations coexist in the same domain during growth. |
| `InteractionText` construction/copy | `OwnedString` text and family, paint | Each independently owned command copy. SSO remains inside the charged enclosing storage; heap allocations use the same domain. |
| `interaction_prepare` output | `OwnedVector<InteractionPreparedEvent>`, paint | Prepared frame; snapshot remains charged while copied strings and events are built. |
| Shared prepare kernel scratch | Owned vectors of transforms, clip depths, chunks, scroll scopes and opacity groups, temporary | Preparation call, released on success or any error. |
| Native-text aggregate validator | `unordered_set<const owned_text::Font*, ..., OwnedAllocator<...>>`, temporary | Capture/preparation call; node and bucket rebind requests are counted individually. |
| Provider run/font/source references | `shared_ptr<const owned_text::Run>` copied without allocating a new control block | Storage remains charged to provider until its last owner/weak owner as applicable; not charged again per command. |
| `InteractionCommandView` inputs | Borrowed strings and existing immutable run owner | Caller must retain inputs during capture and account any owned input-view array. Capture copies text into owned storage; no borrowed string survives success. |
| Result/errors/wrappers | By-value structs/variants and allocation-free error metadata | No heap wrapper/control block is created by the preparation entry points. |

The `InteractionGradient` alternative has owned stop storage for type completeness, but it is rejected by this profile-specific validator; the capture view does not admit a gradient alternative. Box shadows are also rejected. This does not change legacy preparation or authored admission. BorderGroup and all existing scope alternatives use the shared primitive validators and scope kernel. The native producer is still responsible for only emitting the subset admitted for the document profile.

The snapshot and provider transport allocators must refer to the same domain. Preparation rejects a snapshot from another domain. Native run string/glyph allocators and font-source domain are checked as well. No intermediate legacy `std::string`, `std::vector`, legacy shaped run or C `get_command` text duplication is created.

## Shared algorithms and preserved legacy order

`prepare_kernel.h` contains the previous preparation scope/matrix/event loop once. Legacy `prepare` instantiates it with its unchanged std containers/types; interaction uses owned containers/types. Gradient stop-count preflight precedes output reserve; scratch/native validator state is lazily constructed only after reserve, preserving the previous order. Each command keeps the same validation, native aggregate checks, scope transition, event copy and error index/tag ordering. Old/new strings/vectors are charged by actual allocation lifetime, not by a reset of counters.

`text_fields_view.h` adds a private nonowning field view. The same implementation checks text length, UTF-8, geometry, family, style flags, native validation and aggregate string-byte limits in their original order. The internal native-check function pointer is a fixed implementation helper, not an application callback. Other primitive alternatives invoke the existing validator with POD values, which requires no allocation.

The capture entry point accepts native `InteractionCommandView` objects. It does not replace the HTML parser, style runtime, layout or painter. Connecting these views to the retained document pipeline and software raster is a separate integration step. Public callers do not construct views or draw commands.

## Validation in this tranche

`test_interaction_preparation.cpp` uses an executable-only allocation observer/failure injector, including aligned allocation. It checks that every observed preparation allocation request already has a domain charge, sweeps failures in native-text capture and preparation, checks quota rejection/recovery and preservation of the confirmed frame, and verifies lifetime after destroying the budget handle. It tests BorderGroup, scopes, error metadata, depth exhaustion, cancellation, invalid UTF-8 and cross-domain rejection. POD event/matrix results and scope errors are compared with the legacy kernel.

The allocation counts in logs are observations of that build/fixture, not a portable bound or a promise that every future input has those counts. Other platforms remain NOT_VALIDATED until run; they are not excluded at compile time.

Focused legacy preparation and native text snapshot suites must pass after the refactor. The 116 public R3 checks, nine image comparisons, full session fault matrix and 10,000 interactive edit/render cycles remain separate integration gates. A preparation unit pass is not any of those gates.
