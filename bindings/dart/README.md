# AetherUI Dart binding

**Status: not implemented.** This directory defines the integration contract a Dart package must
satisfy — it is the spec a contributor implements against, not working code yet. The binding
consumes the *installed* C ABI only; it must never link against C++ symbols, a private native
header, or a CMake build tree. [`bindings/dotnet/`](../dotnet/) is the reference implementation of
the same contract in a different language — when a lifetime or ownership question isn't obvious
from this document, that binding's `SafeHandle`-derived classes (`NativeDomDocument`,
`NativeLayoutResult` in `AetherUI.Core.Native.Interop`) are the concrete precedent to mirror.

## Why this binding, given Dart already has Flutter

Dart/Flutter already targets Windows, macOS, and Linux desktop as first-class platforms — but
Flutter's authoring model is a widget tree (imperative composition of `Widget` objects in Dart code)
rather than markup and stylesheets. This binding is not a Flutter replacement or a Flutter rendering
backend; it is an **alternative to Flutter's own rendering stack** for a team that specifically wants
HTML+CSS authoring ergonomics — a designer-editable stylesheet, a DOM a non-Dart tool can also
produce or diff, existing HTML/CSS skills transferring directly — while still shipping a native Dart
desktop binary. A Dart application choosing this binding is choosing AetherUI's DOM/CSS/layout
pipeline *instead of* `package:flutter`'s widget/render-object pipeline, not layering one on top of
the other. Whether or not a given Flutter widget renders it, the point of `dart:ffi` here is that the
binding needs neither the Flutter engine nor Skia/Impeller to function — it can be used from a plain
Dart standalone executable with its own minimal native window, which is the actual "skip the
browser/Electron stack" pitch this repository is built around.

## Intended scope

The binding will expose DOM parsing, the style runtime, layout, hit testing, and display-list
generation as an idiomatic Dart API. It may be used headlessly (parse → resolve → layout → read the
display list, no pixels drawn) or combined with a renderer supplied by the application or a future
platform package. Windowing, text measurement, and rasterization are not implied by the core binding
— that split mirrors how `AetherUI.Windows` sits on top of the .NET binding without being part of it.

### `dart:ffi`: no native compilation for the binding itself

`dart:ffi` is part of the Dart SDK standard library — the binding needs no build step of its own to
call into a C ABI, only `DynamicLibrary.open()` against the installed native library and
`Pointer`/`NativeFunction` typedefs generated (by hand, or via `package:ffigen` run against the
installed header) to match `aether.h`. `ffigen` is the closest Dart analogue to Rust's `bindgen` or
Kotlin/Native's `cinterop` — a build-time code generator producing Dart FFI bindings from a C header
— but it is optional; the ABI is simple enough (flat functions, opaque pointers, fixed-width structs)
that hand-written typedefs are a realistic fallback if `ffigen`'s struct/union support doesn't cover
`aether_paint_command_t`'s tagged union cleanly.

### Sketch of the target shape

Not an API commitment — illustrative, to make the ownership contract concrete before anyone writes
FFI code:

```dart
class Document {
  Pointer<aether_doc_s>? _handle;
  static final _finalizer = Finalizer<Pointer<aether_doc_s>>(
    (handle) => _bindings.aether_doc_destroy(handle),
  );

  Document._(this._handle) {
    _finalizer.attach(this, _handle!, detach: this);
  }

  static Document parse(String html) { /* aether_doc_create + ...parse_html */ }

  void dispose() {
    if (_handle == null) return;
    _finalizer.detach(this);
    _bindings.aether_doc_destroy(_handle!);
    _handle = null;
  }
}

class StyleRuntime {
  StyleRuntime(Options options, Environment environment) { .. }
  StylesheetHandle addStylesheet(String css, Layer layer, String sourceName) { .. }
  void dispose() { /* aether_style_runtime_destroy */ }
}

class LayoutResult {
  // Borrowed Node/LayoutBox views obtained from this result become invalid after dispose().
  Node? hitTest(double x, double y) { .. }
  void dispose() { /* aether_layout_result_destroy */ }
}
```

Every owning type exposes an explicit `dispose()` (or `close()`) method as the real contract —
deliberately parallel to `SafeHandle`'s deterministic-disposal role in the .NET binding. A
`Finalizer` attachment backs each instance purely as a backstop that logs/frees on GC if a caller
forgets `dispose()`; Dart's GC gives no timing or ordering guarantee, so relying on the finalizer
for deterministic cleanup — rather than as a leak-detection net — will eventually violate the native
dependency order (a style runtime must outlive every layout built from it, a document must outlive
nodes borrowed from it).

## Native library discovery

Development uses the installed CMake/pkg-config contract documented in `native/README.md`:

```text
<install-prefix>/include/aether/aether.h
<install-prefix>/lib/pkgconfig/aetherui-core.pc
<install-prefix>/bin/aether_core.dll                Windows
<install-prefix>/lib/libaether_core.so               Linux
<install-prefix>/lib/libaether_core.dylib             macOS
```

`DynamicLibrary.open()` should resolve the library path via an explicit override (an
`AETHERUI_INSTALL_PREFIX` environment variable, or a Dart-level configuration hook the package
exposes) with a fallback to a native asset bundled alongside the Dart package for the target
platform — mirroring how a pub package ships prebuilt native artifacts today. It must never read
from `native/build/` or a developer's private CMake cache directory; see
[`examples/cpp/abi-info`](../../examples/cpp/abi-info/) for the discovery pattern any build-time
native-staging script (invoked from a `hook/build.dart` or similar) should mirror.

## ABI verification

Before creating a single handle, the package must validate, at the point the library is loaded:

```text
aether_abi_version()
aether_abi_capabilities()
aether_paint_command_size()
```

Accept only a compatible ABI version (same major, minor ≥ what the binding was written against),
require only the capability bits the binding actually calls into, ignore unknown capability bits
from a newer library, and size every display-list command buffer using the runtime value from
`aether_paint_command_size()` — never `sizeOf<aether_paint_command_t>()` from a hand-written or
`ffigen`-generated struct definition, since that reflects the header the binding was built against,
not necessarily the ABI of the native library actually loaded at runtime.

## Ownership model

Every owning native handle is wrapped by a Dart class with an explicit `dispose()` method calling
the matching `aether_*_destroy`/`aether_*_free`, plus a `Finalizer` registration as the backstop
described above — never the other way around. Nodes and layout boxes are borrowed views and must not
be retained past their owning `Document`/`LayoutResult`'s `dispose()`; the binding should track a
generation counter on `Document` and check it on each borrowed-view access, throwing a clear
`StateError`/`AetherDisposedException` rather than dereferencing a freed native pointer. Strings and
gradient-stop arrays returned inside display-list commands are copied into native Dart `String`/
`Float32List` values (via `Pointer<Utf8>.toDartString()`/explicit array copies from `ffigen`'s
struct bindings) before `aether_paint_command_free()` runs.

## Isolate model and callback thread-affinity — an open design question

This is a real constraint the binding must design around, not a detail to hand-wave: Dart code runs
in isolates, each with its own memory and its own event loop, and a Dart closure cannot simply be
invoked from an arbitrary native thread the way a JVM/CLR callback marshaler can hop back onto a
managed thread pool. Native callbacks (text measurement, scroll-offset queries) arrive synchronously
on whatever native thread called into the layout/display-list engine — which, in this binding's
initial design, will most likely be the same thread that made the original Dart→native call (ABI 0.1
is synchronous, single-call-stack by construction), meaning the callback *can* usually re-enter Dart
directly via `NativeCallable.isolateLocal` on that same isolate. But that assumption breaks the
moment any future platform layer or renderer integration invokes layout from a different native
thread than the isolate that owns the callback (a background isolate spun up specifically for
layout work, for instance) — at that point a raw synchronous re-entry into a specific isolate is
unsafe, and the binding needs either `NativeCallable.listener` with `SendPort`/`ReceivePort`
marshaling (asynchronous, adds a hop, breaks the ABI's synchronous callback contract from the native
side's point of view) or `Isolate.exit`/isolate-group-shared state carefully scoped to avoid tearing
down state the native call still needs. This binding's design must pick one of these explicitly and
document which native threads are and are not safe to call back into which isolate from — shipping
without an explicit answer here is not a placeholder, it is a latent crash.

Every callback trampoline must also catch any Dart error and return a documented safe fallback
value; an uncaught Dart exception must never be allowed to propagate across the FFI callback boundary
back into native code.

ABI 0.1 requires serialized access to a handle graph — no two isolates/threads may operate on the
same document, style runtime, layout result, or display list concurrently. Given the isolate model
above, the more conservative default is to confine a given handle graph to the isolate that created
it and document that restriction explicitly, rather than attempt cross-isolate sharing of native
handles at all (a raw `Pointer` cannot be sent through a `SendPort` to another isolate meaningfully
without the receiving isolate independently reopening the library, which defeats handle sharing
entirely).

## Release gates

Before publishing a package version: verify ABI negotiation against the pinned native version, run a
leak check for `dispose()`/`Finalizer` cleanup ordering, confirm the isolate/callback thread-affinity
design decision above is actually enforced (a test that calls a measurement callback from the
expected thread and asserts a call from an unexpected context fails loudly rather than silently
corrupting isolate state), verify display-list command decoding against a fixture with every
paint-command variant, and run a headless parse→style→layout→display-list smoke test for every
published platform (Windows, Linux, macOS) before it ships.
