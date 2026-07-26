# AetherUI Kotlin/Native binding

**Status: not implemented.** This directory defines the integration contract a Kotlin/Native
library must satisfy — it is the spec a contributor implements against, not working code yet. The
binding consumes the *installed* C ABI only; it must never link against C++ symbols, a private
native header, or a CMake build tree. [`bindings/dotnet/`](../dotnet/) is the reference
implementation of the same contract in a different language — when a lifetime or ownership question
isn't obvious from this document, that binding's `SafeHandle`-derived classes (`NativeDomDocument`,
`NativeLayoutResult` in `AetherUI.Core.Native.Interop`) are the concrete precedent to mirror.

## This is not the same thing as the Java binding — read this first

This directory is specifically **Kotlin/Native**, not Kotlin running on the JVM. That distinction
matters because Kotlin-on-JVM is *already covered*: any JVM-hosted Kotlin project can consume
[`bindings/java/`](../java/) directly through normal JVM interop the moment that binding exists —
Kotlin/JVM classes call Java classes with no translation layer, so a second JNI-based binding
written "in Kotlin" for the JVM target would be pure duplication of `bindings/java/`'s contract with
a different syntax on top.

What Kotlin/JVM *cannot* do is produce a native, JVM-free binary — no embedded JRE, no JIT startup
cost, no GC pause profile inherited from the JVM. Kotlin/Native exists precisely to compile Kotlin to
a native executable/library via LLVM, with its own runtime and memory model, entirely independent of
the JVM. That is the actual reason this directory exists: a Kotlin/Native binding lets a desktop
Kotlin Multiplatform target (or any Kotlin/Native consumer — Linux, Windows, macOS, or KMP shared
code targeting those) embed AetherUI the same way the Go and Rust bindings do — directly against the
native C ABI, no managed runtime underneath at all. If a project's Kotlin code already runs on a JVM,
it should use `bindings/java/`; this directory is only relevant when a project specifically needs a
JVM-free native Kotlin binary.

## Intended scope

The binding will expose DOM parsing, the style runtime, layout, hit testing, and display-list
generation as an idiomatic Kotlin/Native API. It may be used headlessly (parse → resolve → layout →
read the display list, no pixels drawn) or combined with a renderer supplied by the application or a
future platform library. Windowing, text measurement, and rasterization are not implied by the core
binding — that split mirrors how `AetherUI.Windows` sits on top of the .NET binding without being
part of it.

### `cinterop`: bindings generated directly from a C header, no JVM involved

Kotlin/Native's own `cinterop` tool consumes a `.def` file pointing at `aether.h` (headers +
compiler/linker flags) and generates Kotlin declarations for every function, struct, and enum in the
header directly — analogous in spirit to Rust's `bindgen` or Go's `cgo`, but built into the
Kotlin/Native toolchain itself rather than a third-party crate. No JNI, no JVM classloading, no
`System.loadLibrary` — the generated Kotlin calls the native library through Kotlin/Native's own
interop layer at the compiled-binary level.

### Sketch of the target shape

Not an API commitment — illustrative, to make the ownership contract concrete before anyone writes
the `.def` file or wrapper code:

```kotlin
class Document private constructor(private var handle: CPointer<aether_doc_s>?) : Closeable {
    companion object {
        fun parse(html: String): Document { /* aether_doc_create + ...parse_html */ }
    }

    override fun close() {
        handle?.let { aether_doc_destroy(it); handle = null }
    }
}

class StyleRuntime(options: Options, environment: Environment) : Closeable {
    fun addStylesheet(css: String, layer: Layer, sourceName: String): StylesheetHandle { .. }
    override fun close() { /* aether_style_runtime_destroy */ }
}

class LayoutResult internal constructor(/* wraps aether_layout_result_t */) : Closeable {
    // Borrowed Node/LayoutBox views obtained from this result become invalid once closed.
    fun hitTest(x: Float, y: Float): Node? = ..
    override fun close() { /* aether_layout_result_destroy */ }
}
```

Every owning type implements Kotlin's `Closeable` (the Kotlin/Native-available equivalent of Java's
`AutoCloseable`, usable with the `use { }` extension function for scope-based cleanup), giving
`Document.parse(html).use { doc -> ... }` as the natural shape — deliberately parallel to
`SafeHandle`'s deterministic-disposal role in the .NET binding and to `AutoCloseable` in the Java
contract.

## Native library discovery

Development uses the installed CMake/pkg-config contract documented in `native/README.md`:

```text
<install-prefix>/include/aether/aether.h
<install-prefix>/lib/pkgconfig/aetherui-core.pc
<install-prefix>/bin/aether_core.dll                Windows
<install-prefix>/lib/libaether_core.so               Linux
<install-prefix>/lib/libaether_core.dylib             macOS
```

The `cinterop` `.def` file's `headers`/`compilerOpts`/`linkerOpts` should resolve against the
installed prefix via a Gradle property or environment variable (e.g. `AETHERUI_INSTALL_PREFIX`)
rather than a hard-coded path, and the Gradle build should select the correct Kotlin/Native target
(`linuxX64`, `mingwX64`, `macosX64`/`macosArm64`, etc.) to match whichever installed native artifact
is present. It must never point at `native/build/` or a developer's private CMake cache directory —
see [`examples/cpp/abi-info`](../../examples/cpp/abi-info/) for the discovery pattern the Gradle
build's native-resolution logic should mirror.

## ABI verification

Before creating a single handle, the library must validate, in a one-time initialization path:

```text
aether_abi_version()
aether_abi_capabilities()
aether_paint_command_size()
```

Accept only a compatible ABI version (same major, minor ≥ what the binding was written against),
require only the capability bits the binding actually calls into, ignore unknown capability bits
from a newer library, and size every display-list command buffer using the runtime value from
`aether_paint_command_size()` — never `sizeOf<aether_paint_command_t>()`, since the `cinterop`-
generated struct layout reflects the header it was generated from, not necessarily the ABI of
whatever native library is loaded at runtime.

## Ownership model

Every owning native handle is wrapped by a Kotlin class implementing `Closeable`, whose `close()`
calls the matching `aether_*_destroy`/`aether_*_free`. Kotlin/Native's own object-cleanup hooks (a
`Cleaner` obtained via `kotlinx.cinterop`, roughly parallel to Java's `java.lang.ref.Cleaner`) may
back a last-resort safety net for an abandoned handle, but the same caveat applies as it does for
Java and Go: native objects have a dependency order (a style runtime must outlive every layout built
from it) that a GC/cleaner pass does not respect, so `close()` — not the cleaner — is the real
contract.

Nodes and layout boxes are borrowed views and must not be retained past their owning
`Document`/`LayoutResult`'s `close()`. The binding should track a generation counter on `Document`
and check it on each borrowed-view access, throwing a clear exception rather than dereferencing a
freed `CPointer` — Kotlin/Native's interop layer does not itself protect against use of a `CPointer`
whose target has already been freed. Strings and gradient-stop arrays returned inside display-list
commands are copied into native Kotlin `String`/`FloatArray` values (via `toKString()`/explicit
array copies) before `aether_paint_command_free()` runs.

## Kotlin/Native memory model and callback user-data

Kotlin/Native has its own memory model governing how objects move between Kotlin and native/C code —
a Kotlin object reference cannot simply be cast to a raw `void*` and handed to a C callback's
`user_data` parameter the way a JVM object reference never crosses a JNI boundary as a raw pointer
either. Callback user-data must go through `StableRef.create(kotlinObject)`, whose `.asCPointer()`
result is what gets passed as `void*` to `aether_layout_create`/`aether_style_runtime_create`/etc.;
the trampoline on the C side receives that pointer back and calls `.asStableRef<T>().get()` to
recover the Kotlin object. The `StableRef` must be disposed (`.dispose()`) exactly once, after native
code can no longer invoke the callback (i.e., after the owning `LayoutEngine`/style runtime's
`close()`), or it leaks; disposing it too early turns the next native callback invocation into a
use-after-free on the Kotlin side.

Native callbacks (text measurement, scroll-offset queries) are synchronous and run on whatever
native thread is currently inside the call that triggered them — which, in Kotlin/Native's newer
memory model, is fine for calling back into Kotlin code as long as the callback trampoline is a
`staticCFunction` (not a closure) and doesn't assume it's running on any particular Kotlin
worker/thread. Every trampoline must catch every `Throwable` and return a documented safe fallback
value; a Kotlin exception must never be allowed to propagate across the `cinterop` callback boundary
back into native code.

ABI 0.1 requires serialized access to a handle graph — no two threads may operate on the same
document, style runtime, layout result, or display list concurrently. The binding should either
enforce this with its own lock per owning handle or document the restriction explicitly, since
Kotlin/Native's memory model changes around shared mutability do not, by themselves, make concurrent
native calls against the same handle safe.

## Release gates

Before publishing a library version: verify ABI negotiation against the pinned native version, run a
leak check for `close()`/`StableRef` disposal ordering, confirm callback exception containment (a
measurement callback that throws must not crash the process), verify display-list command decoding
against a fixture with every paint-command variant, and run a headless parse→style→layout→
display-list smoke test for every published Kotlin/Native target (`linuxX64`, `mingwX64`,
`macosX64`/`macosArm64`, and any additional target the release ships a native artifact for) before it
ships.
