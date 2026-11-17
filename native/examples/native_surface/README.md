# Native surface: external C11 consumer

From the repository root, after building the shared library:

```sh
native/examples/native_surface/run.sh build build/native-surface-consumer
```

The recipe compiles a separate C translation unit using `cc -std=c11 -Werror`,
only the public include directory, and `libaether_core.so`. No private sources,
headers or extra objects are linked. Set `CC` to choose the C compiler. The first
argument selects the directory containing the actual delivery DSO. The recipe
saves checksums of that DSO, the public header, consumer source/executable and PPM.
The PPM is converted directly from returned BGRA bytes, not redrawn by another
renderer. Its RGB channels are premultiplied against black; transparent pixels
therefore appear black. The tests separately assert all four returned channels.

The 16x16 example contains an 8x8 opaque red square at (2,2), a 4x4 rectangular
clip at (4,4), and an overflowing half-alpha blue rectangle. Every pixel is
checked analytically: BGRA `(128,0,127,255)` in the clipped overlap,
`(0,0,255,255)` in the rest of the red square, zero outside. This includes sharp
edges and transparency. Alpha means color alpha, **not** overlapping CSS opacity
groups. This API rasterizes the resolved public display list; it does not
promise full HTML/CSS semantics. That list does not retain all native paint
metadata, so the wrapper cannot retrospectively detect every semantic omission.
Text, images and other unsupported commands reject the entire render. Large
gradients retain main's existing limits; this consumer does not claim the full
original page works or incorporate the pending P4 experiment.

## Public test coverage

The same consumer exercises NULLs, invalid dimensions, zero/suspended viewports,
NO_FRAME, exact struct size, metadata queries, short/exact/oversized buffers with
canaries, failed-call output sentinels, sequences 1 through 6, same-size and
changed-size resize, invalid resize, suspension/resumption, synchronized
wrong-thread render/resize/copy/destroy, input destruction and 16 lifetime cycles.
The wrong thread is joined before the owner accesses the handle again.

Before running, the atomic rejection recipes are fixed as follows:

- Text: `<body>hello</body>` with a deterministic public measurement callback,
  expected UNSUPPORTED.
- Image: an 8x8 background `url(asset://test)` with explicit size, expected
  UNSUPPORTED; no network resource is read.
- Work budget: three opaque red, absolutely positioned 8192x2048 divs at (0,0)
  in an 8192x2048 viewport. Clear plus fills exceeds main's 50,000,000 work
  budget, expected LIMIT_EXCEEDED. No budget is changed to trigger failure.

Each rejection compares every confirmed byte, width, height and sequence to the
prior successful frame, including after resizing the pending viewport.

R1 owns each surface on its creating thread, is synchronous, and exposes no
cancel token or callbacks. Copies use a temporary owning copy and can fail OOM
even for metadata queries. Resize keeps the previous confirmed frame and its
original dimensions. A successful render advances the sequence even when pixels
are identical. The 64 MiB limit applies to one frame payload, not total process
memory: confirmed and candidate frames, snapshot/preparation and temporary copy
may coexist. Arbitrary/stale handles, double destroy, concurrent destroy and
invalid or overlapping output pointers are caller errors, not safe probes.

Private fault injection, overflow and renderer regression results belong to the
separate implementation/validation report. This consumer alone does not prove
those tests, CMake installation, Windows, sanitization or full product readiness.

The scene is built with the public style runtime and its resolved-style-map API.
It explicitly uses `overflow:clip`, which clips without dynamic scroll commands;
`overflow:hidden` is not interchangeable because it emits scroll commands that
R1 intentionally rejects. No commands are cloned, patched or synthesized by the
consumer. Fixture setup was corrected to these public paths before the final
run; the analytic pixel oracle and renderer remained unchanged.

Optional lossless RGB PNG conversion for inspection (Pillow already installed
on this validation machine; not a required product dependency):

```sh
python3 - <<'PY'
from PIL import Image
p = 'build/native-surface-consumer/surface.ppm'
Image.open(p).save(p.replace('.ppm', '.png'))
PY
```
