# H1.1 isolated geometry CLI: first implementation

Linux-only, C++20, manual build/run. No Actions, windows, .NET, Chromium or
system font fallback. This is not a general HTML/CSS conformance validator.

From repository root:

```
sh tests/isolated_capture/build.sh /tmp/h1-build
sh tests/isolated_capture/test.sh /tmp/h1-build/aether_capture /tmp/h1-results
/tmp/h1-build/aether_capture --geometry input.html NEW_OUTPUT_DIR 1280 800 1
```

The supervisor creates one worker process, which reads bytes into private
storage and creates its own document/runtime/layout. Handles never escape. No
external callbacks, loader, resolver or font lookup is installed. This does not
promise an OS security sandbox. Input paths are trusted CLI arguments; the final
component must be a regular nonsymlink file. Output must not already exist.

## Honest domains

Every report states `css_eligibility=Unknown`,
`css_reason=PARSER_COVERAGE_UNPROVEN`, `authored_css_validation=NOT_RUN`, and
`flexbox_semantic_acceptance=false`. The engine parser's OK is **not** proof of
coverage; original source spans/recovery evidence are not implemented here.
`--css` rejects instead of downgrading to geometry. `--geometry` explicitly
requests only Aether's resolved emitted commands. Unknown CSS properties may be
discarded by the existing engine; a generated image is not CSS fidelity proof.

## Actual limits versus missing limits

During the complete worker lifetime Linux enforces RLIMIT_AS=1 GiB,
RLIMIT_CPU=5 seconds and RLIMIT_STACK=8 MiB. The supervisor kills the worker
after 10 seconds wall time. Core dumps are disabled. These are process-wide
backstops, **not** per-node/per-operation parser or layout instrumentation.
The report explicitly marks internal parse/layout work limits NOT_IMPLEMENTED.
Input is bounded to 2 MiB while reading; NUL is rejected. Viewports are bounded
to 2048x2048, DPR=1. DOM preflight rejects over 50,000 nodes/depth 128 **after**
parsing; it is not claimed to protect the parser. CSS token preflight uses the
engine lexer with a work budget. Preparation/raster retain their existing limits.

For this first cut all CSS at-rules and function tokens are rejected,
including otherwise harmless media/calc/color syntax, conservatively. All
script/link/image/SVG/media/embed elements are rejected. No resources are
silently downloaded. Non-whitespace body text is rejected before layout;
metadata text is exempt from that preflight, but any text measurement request
invalidates the operation before image publication. The internal sentinel
callback cannot provide a supported text result; no output is accepted from a
layout that requested it. Pseudo text and unsupported draw commands can also
reject later. UA is the real engine builtin sheet; its hash is recorded.

Style blocks are traversed in DOM order and compiled separately after UA;
compiled-byte hashes are recorded. This is not a complete source provenance
certificate. HTML/parser omissions remain Unknown.

## Artifacts and metrics

`report.log` reports started stages and measured elapsed wall milliseconds,
worker exit/signal, child CPU seconds and peak RSS KiB from Linux wait4.
Unstarted stages have no duration (not zero); supervisor wall includes startup
and waiting. No comparison timing or Chromium capture is claimed. CPU/RSS apply
to the whole worker, not individual stages. A rejected/terminated worker does
not publish `aether.png`; a successful image covers emitted Aether commands only.
The original flexbox fixture is tested without edits and is expected to reject
for real missing text/resources/CSS support. This is useful diagnostic output,
not a completed flexbox render.

The manual build also emits `aether_capture_limit_probes`, a test-only variant
with compile-time probe branches. Never use it for captures. Verify the actual
same supervisor limits (allocation denial, CPU kill, wall timeout) with:

```
sh tests/isolated_capture/test_limits.sh /tmp/h1-build/aether_capture_limit_probes /tmp/h1-limit-results
```

These probes do not prove per-operation parser/layout budgets; none are claimed.

OS CPU accounting/enforcement has granularity: a configured five-second CPU
limit is not an exact five-second deadline. See measured probe evidence. This
first increment does not claim full H1 parser provenance or fine-grained work
budgets. It is a diagnostic/geometry CLI with explicit failure reports.
