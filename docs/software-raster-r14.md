# R14.1: private native group opacity

The opt-in C++ `NativePaintList` path owns its raw command list and ordered opacity
boundary events. Public C builders retain their prior per-command behavior. The
native snapshot owns all captured payloads; no external sidecar lifetime is needed.
Opening IDs are monotonic, parent IDs and closing IDs must match, and groups cannot
cross transform, clip or chunk scopes. Events inside a collapsed border group fail.

Each element with nonunit opacity paints its subtree into a transparent viewport
layer; its premultiplied pixels are multiplied once and composited source-over at
close. Child clips apply once, not again at composition. Anonymous text/control
boxes do not open duplicate element groups. The isolated-node builder emits an
isolated subtree, not a patch for an existing frame.

The isolated builder rejects root/body/document nodes at every opacity and ignores
all ancestor opacity for eligible descendants. In the full builder,
root/document/canvas opacity other than 1 is unsupported, including current style,
transition and root visual overrides. This is not complete CSS stacking-context or
text support. Unsupported commands still fail inside opacity 0 groups.

## Bounds and atomicity

Maximum group nesting is 8. Aggregate live layer storage is limited to 64 MiB,
separate from FrameStore's existing storage limit. Full-surface clear and composite
work are charged against the existing 50 million work bound. Preflight validates
the entire snapshot before candidate pixels. Layer allocation and replay support
cancellation; RAII aborts the candidate on failures, preserving the confirmed frame.

## Validation

Manual reproduction: `sh tests/software_render_smoke/run_r14.sh OUTPUT_DIRECTORY`.
No Actions/workflows are added or invoked. The script compiles the 71 core units
with the installed C++ compiler, then performs real HTML/style/layout/emission.
Strict standalone tests and UBSan passed. The legacy core integration retains its
pre-existing enum/non-enum warning exception from measure_state.h.

The HTML integration checks .5 parent opacity over overlapping opaque descendants:
native overlap alpha 128; unchanged legacy overlap alpha 192. Both full and isolated
builders agree; malformed boundaries and nonunit root override fail. Prepared pixels
remain usable after source DOM/layout/list destruction. Pure tests cover nested
opacity, 0/1, fractional clipping, malformed scope/identity, allocation failure,
cancellation, depth/storage/work limits and preservation of confirmed pixels.

Generated `aether-r14.png` and `report.log` describe the 40x40 minimum HTML case.
Timings are local repeated rendering measurements, not a Chromium comparison.
Chromium, Windows, CMake configuration and full flexbox page are NOT validated here.
