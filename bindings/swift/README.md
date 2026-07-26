# AetherUI Swift binding

**Status: not implemented.** This directory defines the integration contract a Swift package must
satisfy — it is the spec a contributor implements against, not working code yet. The binding
consumes the *installed* C ABI only; it must never link against C++ symbols, a private native
header, or a CMake build tree. [`bindings/dotnet/`](../dotnet/) is the reference implementation of
the same contract in a different language — when a lifetime or ownership question isn't obvious
from this document, that binding's `SafeHandle`-derived classes (`NativeDomDocument`,
`NativeLayoutResult` in `AetherUI.Core.Native.Interop`) are the concrete precedent to mirror.

## Intended scope

The binding will expose DOM parsing, the style runtime, layout, hit testing, and display-list
generation as an idiomatic Swift API. It may be used headlessly (parse → resolve → layout → read
the display list, no pixels drawn) or combined with a renderer supplied by the application or a
future platform package. Windowing, text measurement, and rasterization are not implied by the core
binding — that split mirrors how `AetherUI.Windows` sits on top of the .NET binding without being
part of it.

**This is a cross-platform binding, not an Apple-only one.** AetherUI's whole premise is a portable
C++ core behind a flat C ABI, and Swift's toolchain — including its C interop story — runs on Linux
and Windows in addition to macOS/iOS. The Swift package must build and pass its test suite on all
platforms the native library is installed for; nothing in this document should be read as assuming
Xcode, an Apple SDK, or Foundation's Apple-specific behavior.

### Swift/C interop: no code-gen tool required

Swift imports a plain C header directly through a **module map** — there is no separate bindgen
step analogous to JNI's generated glue or `bindgen` for Rust. A `module.modulemap` pointing at
`aether.h`, wrapped in a small `CAether` (or similarly named) system-library target, is enough for
Swift to see every function, struct, and enum in the header with their C types translated
automatically (`const char*` becomes `UnsafePointer<CChar>?`, opaque `struct aether_doc_s*` becomes
an imported pointer type, etc.). This is close to native — the same mechanism Swift uses to import
any C system library — and is the reason Swift is a comparatively low-friction binding to prototype
despite having no official ABI-generation tooling of its own for C headers.

### Sketch of the target shape

Not an API commitment — illustrative, to make the ownership contract concrete before anyone writes
the module map or wrapper code:

```swift
public final class Document {
    private var handle: aether_doc_t?

    public static func parse(_ html: String) throws -> Document { /* aether_doc_create + ...parse_html */ }

    deinit {
        if let handle { aether_doc_destroy(handle) }
    }
}

public final class StyleRuntime {
    public init(options: Options, environment: Environment) throws { /* aether_style_runtime_create */ }
    public func addStylesheet(_ css: String, layer: Layer, sourceName: String) throws -> StylesheetHandle { .. }
    deinit { /* aether_style_runtime_destroy */ }
}

public final class LayoutResult {
    // Borrowed Node/LayoutBox views obtained from this result become invalid once it is deinitialized.
    public func hitTest(x: Float, y: Float) -> Node? { .. }
    deinit { /* aether_layout_result_destroy */ }
}
```

Every owning type is a `final class` whose `deinit` calls the matching
`aether_*_destroy`/`aether_*_free` — Swift's automatic reference counting makes `deinit` fire
deterministically the instant the last strong reference drops, which is the same deterministic-
disposal property `SafeHandle` gives the .NET binding, just driven by ARC instead of an explicit
`Dispose()`/`Close()` call. A retain cycle (e.g. a callback closure capturing the owning
`LayoutEngine` strongly) would delay or prevent `deinit` from ever firing, so callback storage must
use `unowned`/weak capture or an explicit unmanaged-pointer registry rather than a naive strong
closure capture — the binding's own tests should include a cycle check for exactly this failure
mode.

## Native library discovery

Development uses the installed CMake/pkg-config contract documented in `native/README.md`:

```text
<install-prefix>/include/aether/aether.h
<install-prefix>/lib/pkgconfig/aetherui-core.pc
<install-prefix>/bin/aether_core.dll                Windows
<install-prefix>/lib/libaether_core.so               Linux
<install-prefix>/lib/libaether_core.dylib             macOS
```

A Swift Package Manager `Package.swift` target for the C shim (`CAether`) should point its
`unsafeFlags`/`linkerSettings` at the installed prefix via an environment variable (e.g.
`AETHERUI_INSTALL_PREFIX`) or, where available, `pkg-config --cflags --libs aetherui-core`; SwiftPM
has no first-class pkg-config target type, so the module map's include/link paths likely need to be
generated or templated rather than hard-coded. It must never point at `native/build/` or a private
CMake cache directory — see [`examples/cpp/abi-info`](../../examples/cpp/abi-info/) for the
discovery pattern to mirror in whatever build-time resolution script the package uses.

## ABI verification

Before creating a single handle, the package must validate, in a one-time initialization path:

```text
aether_abi_version()
aether_abi_capabilities()
aether_paint_command_size()
```

Accept only a compatible ABI version (same major, minor ≥ what the binding was written against),
require only the capability bits the binding actually calls into, ignore unknown capability bits
from a newer library, and size every display-list command buffer using the runtime value from
`aether_paint_command_size()` — never `MemoryLayout<aether_paint_command_t>.size`, since that
reflects the header the Swift module was compiled against, not necessarily the ABI of whatever
native library is actually loaded at runtime.

## Ownership model

Every owning native handle is wrapped by a `final class` whose `deinit` releases it — there is no
struct-based value-type option for owning handles, since Swift structs have no deinitializer and
copying one would silently duplicate ownership of a single native pointer. Nodes and layout boxes
are borrowed views; they should be lightweight structs holding the raw handle plus an unowned/weak
reference to their owning class, so a use-after-owner-deinit access can throw a clear
`AetherError.disposed` rather than crash on a dangling C pointer. A document reparse invalidates
every node wrapper for the previous tree; the binding should track a generation counter on
`Document` and check it on each borrowed-view access. Strings and gradient-stop arrays returned
inside display-list commands are copied into native Swift `String`/`[Float]` values before
`aether_paint_command_free()` runs — no wrapper type should expose a `String` backed directly by
native memory whose lifetime is tied to the command.

## Callback and threading policy

Native callbacks (text measurement, scroll-offset queries) are synchronous, invoked on whatever
native thread is currently inside the call that triggered them. Swift closures cannot be passed
directly as C function pointers when they capture context, so callback registration needs a
`Unmanaged<T>`-based user-data pointer (`Unmanaged.passRetained`/`.passUnretained` paired
deliberately with the callback's actual lifetime) rather than relying on ARC to keep a captured
closure alive across the C boundary — a plain Swift closure literal with captures cannot be
converted to a C function pointer at all, so this indirection is not optional. Every trampoline must
catch anything a Swift callback could throw (`do { ... } catch { return safeFallback }`) and, if
running under recent Swift concurrency features, must never `await` or otherwise suspend inside a
callback invoked synchronously from native code; a Swift error or a fatal trap must never be allowed
to unwind across the C callback boundary back into the native engine's call stack.

ABI 0.1 requires serialized access to a handle graph — no two threads may operate on the same
document, style runtime, layout result, or display list concurrently. If the binding adopts Swift's
`Sendable`/actor isolation model, owning wrapper types should either be non-`Sendable` by default (so
the compiler flags accidental cross-actor sharing) or explicitly isolated to a single actor that
owns all calls into a given handle graph — silently marking wrapper types `Sendable` without an
internal lock would make a real native-side data race look, to the Swift compiler, like safe
concurrent code.

## Release gates

Before publishing a package version: verify ABI negotiation against the pinned native version, run
a leak check (e.g. via Instruments or `swift test` with retain-cycle assertions) for `deinit`-based
cleanup ordering, confirm callback error containment (a measurement callback that throws or traps
must not crash the process or corrupt the native call stack), verify display-list command decoding
against a fixture with every paint-command variant, and run a headless parse→style→layout→
display-list smoke test on every platform (macOS, Linux, Windows) the release ships a compiled
artifact for — not macOS alone.
