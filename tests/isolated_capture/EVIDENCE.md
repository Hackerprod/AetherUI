# Manual evidence, 2026-11-12

Built from local base `f952aac972613f60ff140049b65f1ad45c38869b` with g++ C++20:
71 engine translation units, native preparation/software raster, and the new
harness. No CMake execution or Actions. The existing `measure_state.h` enum
conditional emits a warning through the legacy layout headers; no new harness
warnings in the final link.

`test.sh`: PASS. Shape HTML emitted 64x64 PNG with a red rectangle, visually
inspected. Final run: raster 0.155476 ms, worker CPU 0.002675 s, peak RSS 2440 KiB.
These are single-run diagnostics, not stable performance benchmarks. Other
checks: text, external resource, import, URL, NUL, input byte limit, CSS-domain
rejection, existing-output preservation, and original fixture checksum.

Original `flexbox_runtime_validation.html` read unchanged:
- SHA256 `eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`
- 474495 bytes; 13862 DOM nodes; one style block.
- Final parse: 40.7946 ms; preflight: 2.11663 ms.
- Worker CPU: 0.061497 s; peak RSS: 7684 KiB.
- Rejected: `CSS_FUNCTION_UNSUPPORTED`,
  `RESOURCE_OR_SCRIPT_ELEMENT_UNSUPPORTED`, `TEXT_RENDERER_UNAVAILABLE`.
- No PNG; no layout/raster timing, Chromium comparison or flexbox PASS.

`test_limits.sh`: PASS using the separate compile-time probe binary exercising
the same limit installation/supervisor:
- 2 GiB malloc denied under 1 GiB address-space limit (probe exit 42).
- CPU-bound worker killed by signal 9. Observed CPU 6.07466 s for nominal
  RLIMIT_CPU=5: OS enforcement granularity is not a precise timing deadline.
- Sleeping worker killed by parent wall deadline (observed 10008.8 ms).

These tests verify process backstops, not internal parser/layout operation
budgets or CSS coverage. Both remain explicitly unimplemented/unproven.
