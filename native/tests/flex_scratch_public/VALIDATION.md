# Public flex scratch integration

`runtime.cpp` only uses public APIs. Build the identical source against the exact
baseline and candidate shared libraries with `run.py`; it does not reconstruct an
oracle from the changed source. `manifest` executes 36 fixtures (six modes, three
sizes and both axes), with initial layout, changed viewport width and then inline
style mutation plus child reordering. All scalar fields are serialized explicitly;
floats use their IEEE bit patterns, never structure padding. Paint command payloads
are exhaustive for the commands these simple fixtures produce; unexpected tags
fail. Stable node IDs in retained paint are mapped through the public
`aether_node_get_style_node_id` to the exact fixture node index; unexpected owners
fail. Root and body have distinct sentinel indices. Primary boxes, tree order, paint order,
rectangles, scroll metrics/chrome and three hit points per item are included.
Each fixture must show a changed manifest for both relayout and mutation.

## Actual layout, not cached lookup

The measured call is `aether_layout_layout`, not a style or layout-result getter.
In the pinned source its implementation allocates a fresh result on every call
(`layout_engine.cpp`), builds the overlay, and invokes `layout_element` on the root
unconditionally. Flex item resolution then calls `aether_resolve_flexible_lengths`
from `layout_flex.cpp` unconditionally. There is no result-reuse branch in this
path. Width also alternates 101/102 in every measured block. Column main size is
explicitly fixed at 101px: changing the width changes its cross axis, and its flex
resolution still executes. The wider 101/113 manifest witness proves that the
public relayout does change geometry for both axes.

Timing excludes DOM creation, style preparation, result inspection/destruction
and printing. Seven blocks follow three warmups, with 20/5/2 layouts per block
for 32/256/4096 items. Timing adds six `mutated` cases (mixed fixtures after inline mutation and
reordering, for all sizes/axes), for 42 cases total. The runner uses five campaigns, alternates AB/BA order by
case/campaign, preserves every sample, and reports p50, p95, min/max and coefficient
of variation. The executables import the standard allocation functions; the OOM
replacement allocator is never linked into timing. No claim of total-layout
speedup is inferred from a component speedup.

`oom.cpp` is an entirely separate two-item fixture. It first counts ordinary
new/new[] points with a 2000-point safety cap, then fails each reached point once.
Each failure must return OUT_OF_MEMORY, leave the caller's prior output pointer
and complete public manifest intact, and allow an equivalent retry. This tests
public propagation/atomicity of these allocation failures, not all malloc,
calloc, realloc or aligned-allocation failures. Scratch stage coverage belongs
to the separate pure-resolver sweep. No large graphical fixture is swept.

`memory` is a separate, deliberately perturbed run. It requires glibc >= 2.33;
other platforms return 77 and the runner records unsupported, never PASS. A 100µs sampler records
`mallinfo2.uordblks + hblkhd`, also sampled while completed results are retained;
RSS high-water comes from getrusage. The heap peak is a sampled lower bound, not an
exact allocator-request peak. It includes the fixture, allocator arena behavior
and sampler thread. RSS includes the shared library and process. Neither is the
scratch ledger nor evidence of a leak. Memory runs are not timing samples.

Example (paths must identify exact builds):

    python3 run.py --baseline /path/base/libaether_core.so.0.1.0 \
      --candidate /path/candidate/libaether_core.so.0.1.0 --out /path/evidence --phase all

Use phases build, manifest, oom, memory and timing separately to schedule timing
without overlapping compiler/test CPU work. `libraries.json` preserves DSO hashes.
A standalone CMake smoke target can run `runtime manifest mixed 32 row` and column;
only the independent two-library runner establishes A/B equivalence.
