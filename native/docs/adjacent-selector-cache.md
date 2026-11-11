# Scoped adjacent-sibling navigation

`selector_match_context_s` keeps an independent, lazy predecessor index for the
`+` combinator during one immutable-topology `prepare_tree` traversal. It caches
DOM node identity, not selector results. The generic matcher remains uncached;
the existing nth position index, general-sibling traversal, cascades, public
headers and DOM representation are unchanged.

## Activation and publication

- Parents with at most 128 raw children, and scans visiting at most 128 children,
  allocate no predecessor metadata. Missing subjects are never memoized.
- A costly successful lookup memoizes the previous DOM element. A stored null
  value is a hit for the first element, including after a long text prefix.
- Each parent starts with a checked budget of 16 times its raw child count.
  Distinct costly scans subtract their visits with saturation. Hits do not pay.
- Full indexing requires both an exhausted budget and at least the ceiling of
  the greatest observed element ordinal divided by eight distinct memos.
- The full map is built separately without reserving raw child count. It is
  published only after the entire scan succeeds. Duplicate-pointer fixtures
  retain the first occurrence; traversal still advances at every occurrence.
- A partial miss scans normally. Only a complete-map miss proves nonmembership.
  Allocation failures propagate to transactional preparation, not false matches.

Generated nodes and anonymous wrappers are excluded from DOM traversal. Only
the public matcher entry normalizes before/after subjects to their host. The
right-hand compound is checked before predecessor lookup, and recursive matching
keeps the same context and relative anchor. This change does not expand the
parser's supported `:has()` grammar.

## Lifetime and costs

Both indices die before freeze, commit and diagnostic callbacks. No pointer or
index is stored in a node, runtime, transaction or retained revision. A new
prepare therefore starts with empty indices after topology, attribute or state
changes. Mutation during a live context is not supported by this optimization.

The separate map preserves nth-only heap costs, but not fixed context size. On
the measured GCC/libstdc++ 64-bit ABI, the context grows from 64 to 128 bytes.
At 4,096 eligible siblings, nth-only and adjacent-only each request 185,160 bytes
in 4,368 allocations, with a 147,400-byte peak. Activating both requests 370,320
bytes in 8,736 allocations and peaks at 286,600 bytes (1.944 times one index).
These are requested allocator bytes, not RSS or portable constants.

The coverage guard deliberately avoids huge full maps for sparse late queries.
It does not guarantee linear work for reverse or arbitrary query orders, nor
remove general-sibling or `:has()` search costs. Structural work gates are
deterministic; wall-clock gains depend on the real workload.

## Verification

`aether.selector-adjacent-cache` exercises structural identity, lazy activation,
state-derived visit traces, recursive contextual/generic differential matching,
DOM boundaries, fresh scopes, exact allocation costs and every fault ordinal of
isolated and mixed index creation. Visit counts are reconstructed from actual
pre/post index state rather than collected by production instrumentation.

`aether.adjacent-cache-runtime` compares full frozen style storage, sidecars,
impacts, changes and legacy updates against the uncached matcher. It covers
mutation between prepares, generated content, address reuse, retained revisions,
document replacement, abort/retry, reentrant diagnostics and allocation failure
through real preparation. Its dispatch/lifetime seams exist only in the test
translation unit and do not add exported hooks.

The opt-in `aether_adjacent_cache_runtime_benchmark` uses the public ABI and
standard allocator. Compile the same source against base and candidate builds,
alternate libraries/process order, and compare checksums as well as multiple
timing samples. Include no-adjacent, nth-only, dense, sparse, mixed, small-parent,
many-parent and text-prefix workloads; report regressions and variation too.
