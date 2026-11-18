# Interaction font storage tranche

This is a private portable provider/storage implementation, not a completed
interaction API or an integrated G1–G4 result. It does not claim accounting for
legacy document/style/layout/paint allocations. The interaction memory contract
must state the instrumented boundary explicitly.

## Shared algorithms, separate storage

- `text/detail/font_kernels.h` owns the face, cmap, lookup and outline algorithms.
  `LegacyFontStorage` preserves the prior allocation patterns, checks, work
  counters and error translation. `owned_text::Storage` instantiates those same
  kernels with allocator-aware owned containers and factories. Legacy Face/Map/Outline
  fields/layouts are unchanged; friend declarations grant kernel access. The
  separately approved Source bridge extension is described below.
- Vertical metrics read an immutable byte span plus validated units-per-em. Both
  providers use the same allocation-free implementation.
- `font_product_kernel.h` shares shaping metrics, glyph transport and prefix
  arithmetic. Legacy admission and cumulative work/payload reservations remain
  in the legacy provider with their prior limits and order.
- SHA finalization writes to a supplied string. Legacy calls supply std::string;
  owned sources supply OwnedString. There is no temporary default string copy.

No STL-private ownership layouts, release guards, or platform exclusions are
used by the new production provider. The allocator observes actual requests via
standard allocator/rebind operations; capacities and growth decisions remain the
container implementation's choice.

## Allocation inventory

Every owned heap allocation uses the common OwnedAllocator domain. The allocator
reserves payload plus its allocation header/alignment padding before requesting
storage and releases after deallocation. Shared ownership blocks are allocated
through `allocate_shared`, so a surviving weak_ptr retains its block's charge.

| Owner | Instrumented storage |
|---|---|
| Provider | allocate_shared object/control block; fixed arrays for16 bindings,256 outlines,384 cmap keys |
| Source | object/control block, byte-vector capacity, digest-string capacity |
| Face | object/control block, locations-vector capacity |
| Map | object/control block, range-vector capacity |
| Outline | object/control block, segment/contour capacities, every recursive child; flags and decoded-point temporaries |
| Binding/font | both object/control blocks; immutable shared references to source/face/map |
| Product/run | object/control blocks, glyph/source/prefix capacities |

Fixed decoder arrays and recursion state remain bounded stack storage, not heap
allocations. Raster/mask and preparation storage are separate tranches, not
implicitly charged by this provider's work counters. The transport types expose
OwnedVector/OwnedString and immutable shared resource identities for those
tranches; the portable path does not convert through untracked legacy containers. The
explicitly excluded M1 compatibility bridge is described below.

## Work, retention and atomicity

An Operation scope resets only requests/scalar visits/outline work and raster/mask
work allowances. It does not erase live storage charges. The domain survives its
public budget handle as long as containers, allocator copies, or control blocks
remain. Input fonts are copied once at provider creation, not per operation.

Cmap has at most382 source/scalar keys:190 repertoire scalars plus generated bullet
for each of two sources. The array has384 slots. Outline cache256 uses cyclic
eviction; surviving users keep shared outlines and their charges. The16-binding
limit preserves the inherited instance bound. Failures may retain complete valid
cache entries but cannot retain abandoned temporaries or poison the next
operation's work allowance. Public state/frame transactions remain the session's
responsibility.

Product text must already be a presentation: password values must be masked by
the session before this provider is called. Provider output is not a password
getter. A product for empty presentation can carry metrics internally; consumers
must not emit a nonempty-text paint command for an empty run.

## Tests and limits

`test_persistent_font_provider.cpp` is a standalone Linux test allocator harness;
its POSIX test allocation hook is not part of production code. It checks10000
provider operations with256-scalar products, both sources and varied repertoire,
eviction, periodic owned retention after warmup, every observed construction and
product allocation failure, retry, weak-control-block lifetime, and380 scalar/font
shaping equivalence cases. Allocation probes check preceding ledger reservations.
This is NOT the required integrated edit/render/backspace/render test.

The portable provider was exercised normally and under ASan/UBSan with leak
detection disabled in the execution environment. Existing outline/cmap synthetic
suites and SHA/font-source tests were also rerun. These focused results do not
substitute public116-check and9-image regression, a clean full rebuild, independent
review, or validation on Windows/other environments.

## M1 derived legacy bridge and persistent compatibility mode

The authoritative Source now has const byte/digest storage. `legacy_source()`
creates a small derived FontSource adapter which strongly owns that immutable
Source and exposes the identical byte address through `byte_view()`. It never
copies a font file per adapter, event or frame. Only the typed internal bridge
can manufacture the shared-backing token. Source/FontInstance readers use the
accessor for hashing, decoding, metrics and validation; the original `bytes`
vector remains unchanged for default owning sources and is empty in adapters.

FontSource gains private span/owner fields. Its legacy logical reservation is
explicitly `sizeof(LegacyStorageLayout)+128`, the original charge, rather than
silently changing one-shot admission when the internal class grows. Public C
ABI structs are unaffected. The adapter/control block use OwnedAllocator;
its small std::string digest is a derived legacy representation, excluded by M1.

`FontProvider(..., PersistentInteractionTag{budget})` accepts these adapters and
is used only by the interaction backend. `operation()` must enclose a complete
candidate preparation, including measure/layout/finalize, not a single string.
It starts work counters fresh and reconstructs the inherited logical payload
allowance from valid cache entries plus explicit session base charges. On scope
exit after success or failure, abandoned per-operation charges disappear; valid
resources may remain cached. No map snapshot or per-operation resource list is
created. The new auxiliary PersistentState is allocated with OwnedAllocator.

The EXISTING legacy map caches, products, runs and masks are derived legacy
storage explicitly excluded by M1; `usage().payload` is a logical operation
allowance and MUST NOT be presented as the256MiB owned counter. Font bytes remain
in the counted immutable Source backing. Default one-shot constructor/perfiles
retain their cumulative accounting and limits exactly. A baseline repetition
checks those counters, not just pixels.

Persistent outline cache capacity is256. Eviction considers only entries whose
shared_ptr use_count is1 (the cache itself). If every entry is retained, the
operation fails LIMIT_EXCEEDED without invalidating any referenced outline.
Releasing a retained entry permits a later operation to retry. Completed cache
entries can survive OOM; the next operation reconstructs counters and is not
poisoned by failed allocations.

`test_font_source_owned_bridge.cpp` checks source equivalence, pointer identity,
retained lifetime,10000 no-copy reference operations, malformed backing, adapter
OOM and legacy charges. `test_font_provider_persistent_legacy.cpp` checks10000
complete provider operations, both sources, scalar variation and masking,
256 pinned outlines/eviction rejection/unpin recovery, and product OOM retries.
These remain focused provider tests, not integrated UI session completion.
