# Native drawing reconstruction R0

New work from published d5e6f65; not recovery of the lost local renderer.
R0 owns frame storage only. No rasterizer, platform backend, image export,
window, engine load, or claimed rendering performance.

Physical viewport dimensions are signed 0..8192, scale finite and positive.
A surface is limited to 64 MiB, top-down premultiplied BGRA8, stride width*4.
This is a per-surface limit, not a total-memory/RSS cap: old, candidate and
copied frames can coexist. Zero either dimension suspends preparation.
Resize validates without graphics probing; every valid resize, even identical,
discards the candidate but preserves confirmed frame and sequence. An invalid
resize has no effect. This differs intentionally from C#'s native surface probe.

Prepare allocates before replacing the old candidate. Allocation failure leaves
state and output unchanged. Exactly one candidate belongs to the store; token
contains store identity and monotonically increasing serial. Tokens are valid
only during that store lifetime. No guarantee for use after destruction/address
reuse. A foreign live-store token rejects. Commit moves to confirmed, consumes
token and increments sequence once. Exhaustion never wraps or mutates state.
Discard consumes only the current token. A copy of confirmed includes original
viewport and sequence, not the current viewport; copying failure preserves state
and caller output. A pixel span exposes bytes only, not metadata. Its borrow ends
on successful prepare, resize, commit, discard or destruction. Never retain aliases
after commit. Caller writes must preserve BGRA premultiplication. Violating span
lifetime is invalid usage, not prevented by this API.

Store is noncopyable/nonmovable and owner-thread-only. Calls reject wrong thread;
there is no synchronization and concurrent use/destruction is forbidden. The
preallocation test hook may throw bad_alloc only, must not reenter, and models a
failure path, not actual process OOM. Destruction must occur on owner thread.

## Next stages

Extract the existing validated command preparation from tests/visual_compare into
shared native production code, preserving compatibility tests and explicit 9-tag
support until extended. Then implement Cairo image surface and Pango text adapter,
writing to a candidate before committing; extend full command coverage. Windows
Direct2D/DirectWrite adapter must satisfy the same ownership/frame contract. Keep
existing C# backends until proven parity. Finally expose the common C ABI and
thin bindings and connect the C++ validator to PNG export and real timings.
No backend interface is frozen in R0. R0 validation does not execute graphics.

## Chromium reference

Chromium separates recorded operations from rasterization, with Skia as raster
backend; it does not require paint logic per language. Sources inspected:
- https://chromium.googlesource.com/chromium/src/+/HEAD/cc/paint/README.md
- https://chromium.googlesource.com/chromium/src/+/main/cc/paint/paint_record.h
- https://chromium.googlesource.com/chromium/src/+/5377aa4a6a1a1bd3a613b0d22f3c7a48f303a656/docs/how_cc_works.md
This is architectural guidance, not equivalence of Aether output with Chromium.

## Reproduce the pure R0 test

From the repository root:

```sh
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -pthread native/src/rendering/native_frame.cpp native/tests/test_native_frame.cpp -o /tmp/aether-native-frame-r0-test
/tmp/aether-native-frame-r0-test
```

The test rejects NDEBUG builds so disabled assertions cannot produce a false
PASS. Only this standalone test was compiled and run. CMake integration and the
aggregate test suite have not been validated.
