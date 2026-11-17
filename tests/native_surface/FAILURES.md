# Private surface failure harness

Run from the repository root after the normal shared build:

```sh
python3 tests/native_surface/build_shared.py build
python3 tests/native_surface/run_failures.py build
python3 tests/native_surface/run_failures.py build --sanitize
```

The first harness links the same authentic object files as the delivered DSO,
except that `aether_surface.cpp` is rebuilt with `AETHER_SURFACE_TESTING`.
It is **not** the external C consumer and does not claim to test injected errors
through the production shared library. The second command form rebuilds the
entire source closure with AddressSanitizer and UndefinedBehaviorSanitizer,
with leak detection and halt-on-error enabled. The normal DSO is unchanged.

The private compile-time seam is excluded from production and installation.
The harness arms its replacement `operator new` at an identified stage, fails
exactly one real allocation, and asserts that the requested stage consumed the
failure. It prints the allocation's requested byte count. It does not infer
stage coverage merely from a generic allocation failure. Creation, snapshot,
and preparation fail their first allocation; the existing FrameStore allocation
hook identifies candidate pixel allocation and confirmed-frame copy precisely.
Both a destination-buffer copy and metadata-only query are tested for OOM.

After each failure, live allocation count returns to its prior value, confirmed
pixels and metadata remain unchanged, and retry without injection succeeds.
The final count returns to the initial baseline after destroying the surface
and list. Overflow is injected through FrameStore's existing friend test access
for token and sequence counters. An integer exception is thrown at the wrapper
seam outside all internal `noexcept` boundaries to verify INTERNAL translation.
Malformed numeric payload and unbalanced clip-stack tests use a private real
list, not fabricated pointers submitted by a purported public C caller.

A successful run writes `failure-private/manifest.json` or
`failure-sanitized/manifest.json` under the supplied build directory, including
flags and hashes of linked objects, test source, wrapper, seam and executable.
These test artifacts are separate from the delivered DSO manifest. No hooks,
new dependencies, modified budgets, or P4 candidate algorithms are introduced
into the production target.

## Observed environment limitation (2026-11-17)

The full sanitizer closure compiled and linked. The initial run with leak
detection failed at process shutdown because LeakSanitizer reported that it
cannot operate under ptrace. This is **not** a LSan pass. Replaying the exact
same executable with leak detection explicitly disabled passed ASan and UBSan:

```sh
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
build/failure-sanitized/test_native_surface_failures
```

A fresh complete sanitized build for this environment can use the same options
in front of `python3 tests/native_surface/run_failures.py build --sanitize`.
The original failed LSan log is retained separately. Allocation-count cleanup
assertions passed in both ordinary and ASan/UBSan runs; they are explicit
cleanup evidence, not a substitute claim that LSan executed successfully.
