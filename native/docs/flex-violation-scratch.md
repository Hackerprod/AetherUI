# Invocation-local flexible-length violation scratch

`aether_resolve_flexible_lengths` owns one initially empty `std::vector<double>`
and passes it to its private `resolve_line` helper. A finite line first performs
the existing initial freeze and free-space calculation. Only after counting at
least one unfrozen item does it grow the scratch to the full line size, if
needed. It never shrinks the logical size or clears the vector between lines.

## Invariants

No arithmetic, accumulation order, line formation, numeric validation or freeze
rule changes. In particular, initial free space is still computed before the
iteration loop. Scratch is indexed by the position in the entire line, not by
an active-item ordinal. Each active position is written before either reading
it for the violation sum or using it to freeze an item. Frozen positions and
the unused tail are never read; frozen items never become active again within
a line. Thus previous-line values cannot influence the result.

The vector is an automatic local, not a cache. Nested and concurrent calls own
separate storage. No DOM pointers or results persist between invocations. Inputs
remain const and all temporary results and scratch unwind through RAII on an
exception. Removed or postponed allocations deliberately change allocation
failure ordinals. This optimization does not add a public transaction guarantee
or change the existing public error-handling contract.

## Cost and budget

For equal-sized finite lines, scratch allocations fall from L to one when the
lines are active, and from L to zero when all are initially frozen. Lines with
indefinite available size do not use scratch. Increasing line sizes can require
additional growth; there is no universal one-allocation promise.

Let M be the largest full size of any finite line with an unfrozen item. The
validation budget is retained scratch <=16*M bytes, simultaneous old plus new
buffers <=24*M bytes, and peak excess over the predecessor <=16*M bytes. M is
zero if no line is active. Scratch live bytes must be zero on return or unwind.
These bounds count requested allocator bytes, not allocator metadata, heap-wide
cost, fragmentation or RSS. Vector growth policy is implementation-dependent,
so these are validation gates for each STL/platform, not C++ guarantees. A
platform that exceeds them needs an independently reviewed growth design;
the budget must not be silently increased.

For example, libstdc++ growth on active line sizes 1 through 1025 can retain
2048 doubles and briefly hold old plus new buffers, with a 24576-byte scratch
peak versus the predecessor's 8200 bytes. Reuse reduces allocations but can
nearly triple scratch peak. There is no claim of memory neutrality.

## Validation scope

Keep broad field-by-field differential coverage separate from minimal allocation
failure sweeps. Compare floating-point bit patterns, never struct padding.
Cover frozen/active mixtures, min/max iterations, numeric extremes, increasing
line sizes and smaller later lines. An allocator ledger or poisoned scratch is
test-only and must not appear in production or timing executables. Standard
allocator component timings and forced public-layout timings are separate:
a component speedup does not establish a whole-layout or application speedup.

The fixed acceptance thresholds are >=5% median improvement for both large
wrapped component workloads and no reproducible >5% control regression.
Preserve raw samples and noisy results; inconclusive evidence stays inconclusive.
Public tests must compare boxes, scrolling, hit tests and drawing across layout,
relayout and DOM mutation. ABI, public headers, exported symbols and package
consumers remain unchanged.
