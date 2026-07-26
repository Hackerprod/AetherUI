# AetherUI JavaScript/TypeScript binding

**Status: not implemented.** This directory defines the integration contract a JavaScript/TypeScript
package must satisfy — it is the spec a contributor implements against, not working code yet. The
binding consumes the *installed* C ABI only; it must never link against C++ symbols, a private
native header, or a CMake build tree. [`bindings/dotnet/`](../dotnet/) is the reference
implementation of the same contract in a different language — when a lifetime or ownership question
isn't obvious from this document, that binding's `SafeHandle`-derived classes (`NativeDomDocument`,
`NativeLayoutResult` in `AetherUI.Core.Native.Interop`) are the concrete precedent to mirror.

## Framing: a desktop-app binding, not (primarily) a browser one

The primary target for this binding is **Node.js/Deno/Bun as a desktop host**, paired with a
lightweight native window rather than a bundled browser runtime. That is the actual pitch: instead of
shipping an entire Chromium/Electron runtime to get HTML+CSS authoring on desktop, an application
ships a small JS runtime plus the native `aether_core` library, gets the same markup/stylesheet
authoring ergonomics, and skips the multi-hundred-megabyte browser payload and its separate process
model entirely. Consuming AetherUI from Node/Deno/Bun this way is a peer of the Go, Rust, and .NET
desktop bindings — not a web deployment story.

A WASM build of `native/` (via Emscripten) exists as a **secondary, browser-portable possibility** —
worth noting because it would let the same DOM/CSS/layout pipeline run inside an actual browser tab
or a browser-hosted tool without a native binary at all — but it is not this binding's primary
framing, is not built yet, and would in practice be closer to its own binding target (a WASM glue
layer has a different ownership/memory story than calling a native shared library through FFI) than
a mode of the Node-facing binding described below. Anyone picking this up should build and validate
the Node/desktop FFI path first.

**TypeScript is not a separate binding.** Whichever JS mechanism below is chosen, TypeScript support
is `.d.ts` type declarations layered over that same JS surface — there is no `bindings/typescript/`
directory, and there should not be one; a second directory would just be documentation for the same
runtime contract with a different file extension.

## Intended scope

The binding will expose DOM parsing, the style runtime, layout, hit testing, and display-list
generation as an idiomatic JS/TS API. It may be used headlessly (parse → resolve → layout → read the
display list, no pixels drawn — e.g. server-side rendering, snapshot testing) or combined with a
renderer supplied by the application or a future platform package. Windowing, text measurement, and
rasterization are not implied by the core binding — that split mirrors how `AetherUI.Windows` sits on
top of the .NET binding without being part of it.

### Two concrete FFI paths for the primary (desktop/Node) target

- **N-API / native FFI addon**, via a hand-written N-API addon, or a no-compilation-required FFI
  library such as [`koffi`](https://koffi.dev/) or `node-ffi-napi`'s successors, calling the
  *installed* native library directly. `koffi`-style FFI is the closest analogue to Python's
  `ctypes`/Go's `cgo` here — no native toolchain required at install time for the pure-FFI path,
  since it dynamically loads `aether_core` and describes its function signatures in JS rather than
  compiling a native shim. A hand-written N-API addon is the alternative if the project wants
  stronger typing at the native boundary or needs to do work (buffer copies, struct marshaling) more
  efficiently than a generic FFI call layer allows — but it does require a native build step (`node-
  gyp`/`cmake-js`) at package-install time unless prebuilt binaries are shipped per platform.
- **WASM via Emscripten** (secondary, browser-portable): compiling `native/` to a `.wasm` module
  with an Emscripten-generated JS glue layer, loadable from a browser or from Node/Deno/Bun's own
  WASM runtime. This removes the native-library-installation step entirely at the cost of losing
  direct OS/filesystem/threading access the native build has, and is not the primary path this
  binding should be designed around first.

### Sketch of the target shape

Not an API commitment — illustrative, to make the ownership contract concrete before anyone writes
FFI/addon code:

```typescript
export class Document implements Disposable {
  #handle: NativeDocHandle | null;

  static parse(html: string): Document { /* aether_doc_create + ...parse_html */ }

  dispose(): void {
    if (this.#handle === null) return;
    native.aether_doc_destroy(this.#handle);
    this.#handle = null;
  }

  [Symbol.dispose](): void { this.dispose(); }
}

export class StyleRuntime implements Disposable {
  constructor(options: Options, environment: Environment) { .. }
  addStylesheet(css: string, layer: Layer, sourceName: string): StylesheetHandle { .. }
  dispose(): void { /* aether_style_runtime_destroy */ }
  [Symbol.dispose](): void { this.dispose(); }
}

export class LayoutResult implements Disposable {
  // Borrowed Node/LayoutBox views obtained from this result become invalid after dispose().
  hitTest(x: number, y: number): AetherNode | null { .. }
  dispose(): void { /* aether_layout_result_destroy */ }
  [Symbol.dispose](): void { this.dispose(); }
}
```

Every owning type exposes an explicit `dispose()` and implements `Symbol.dispose`, so
`using doc = Document.parse(html);` — the TC39 explicit-resource-management `using` declaration,
already shipping in current TypeScript/JS runtimes — is the natural, idiomatic shape: deterministic,
scope-based cleanup that runs `dispose()` automatically when the `using` binding goes out of scope,
deliberately parallel to `SafeHandle`'s role in the .NET binding and to `using`/`try`-with-resources
in the other bindings' respective languages. `dispose()`/`Symbol.dispose` is the real contract;
`FinalizationRegistry` may back a last-resort leak-detection net (logging a warning if a handle is
GC'd without having been disposed), but it has no ordering or timing guarantee in V8 or any other JS
engine and must never be relied on for deterministic cleanup — the same caveat every other binding's
GC-driven backstop carries.

## Native library discovery (Node/desktop path)

Development uses the installed CMake/pkg-config contract documented in `native/README.md`:

```text
<install-prefix>/include/aether/aether.h
<install-prefix>/lib/pkgconfig/aetherui-core.pc
<install-prefix>/bin/aether_core.dll                Windows
<install-prefix>/lib/libaether_core.so               Linux
<install-prefix>/lib/libaether_core.dylib             macOS
```

The FFI-based path (`koffi` or similar) should resolve the library location via an explicit override
(an `AETHERUI_INSTALL_PREFIX` environment variable, or a package-level configuration hook) with a
fallback to a prebuilt native binary bundled per-platform inside the npm package — the common
distribution shape for a no-compilation-required FFI package. An N-API addon path additionally needs
its own `binding.gyp`/`CMakeLists.txt` resolving the same installed prefix at build time. Neither path
may read from `native/build/` or a developer's private CMake cache directory; see
[`examples/cpp/abi-info`](../../examples/cpp/abi-info/) for the discovery pattern either build script
should mirror.

## ABI verification

Before creating a single handle, the package must validate, at module load time:

```text
aether_abi_version()
aether_abi_capabilities()
aether_paint_command_size()
```

Accept only a compatible ABI version (same major, minor ≥ what the binding was written against),
require only the capability bits the binding actually calls into, ignore unknown capability bits
from a newer library, and size every display-list command buffer using the runtime value from
`aether_paint_command_size()` — never a hard-coded byte length in the FFI struct descriptor or N-API
addon's C++ code, since the command union grows as paint features are added and a stale size becomes
a silent out-of-bounds read on either side of the boundary.

## Ownership model

Every owning native handle is wrapped by a JS class exposing `dispose()`/`[Symbol.dispose]()` calling
the matching `aether_*_destroy`/`aether_*_free`, with a `FinalizationRegistry` entry as the backstop
described above. Nodes and layout boxes are borrowed views and must not be retained past their owning
`Document`/`LayoutResult`'s `dispose()`; the binding should track a generation counter on `Document`
and check it on each borrowed-view access, throwing a clear error rather than dereferencing freed
native memory (an FFI call through a stale pointer can crash the process outright — there is no JS-
level safety net once a raw native pointer is involved). Strings and gradient-stop arrays returned
inside display-list commands are copied into native JS `string`/`Float32Array` values before
`aether_paint_command_free()` runs.

## Callback and threading policy

Native callbacks (text measurement, scroll-offset queries) are synchronous and run on whatever
native thread is currently inside the call that triggered them. In the FFI path (`koffi` and
similar), calling back into JS from a native callback requires the FFI library's synchronous-
callback support, which under the hood must marshal onto the JS engine's single-threaded execution
context — a callback invoked from a native thread that is not the main Node thread (not expected
under ABI 0.1's synchronous, same-call-stack callback model, but worth flagging since a future
native worker-thread integration could change this) is unsafe to call directly into JS from and would
need `worker_threads`/`Atomics.wait`-based synchronous handoff or an async bridge, which changes the
callback's synchronous contract from the native side's point of view. An N-API addon has the
equivalent constraint via `napi_threadsafe_function` if it ever needs to call back from a non-JS
thread. Every callback trampoline must catch any JS exception/error thrown inside it and return a
documented safe fallback value — an uncaught JS exception must never be allowed to propagate back
into native code across the FFI/N-API boundary.

ABI 0.1 requires serialized access to a handle graph — no two threads may operate on the same
document, style runtime, layout result, or display list concurrently. Given JS's single-threaded
execution model this is largely automatic for a single-threaded Node/Deno/Bun process, but the
binding must still document explicitly that a handle must not be shared across `worker_threads`
workers (each of which has its own JS heap and would need its own native library binding state) —
silently allowing a `Document` reference to be passed to a worker via structured clone (it can't be,
today, for a raw native handle) or, more realistically, letting two independent bindings loaded in
two workers race on a supposedly-independent handle graph is exactly the class of bug this note is
meant to head off.

## Release gates

Before publishing a package version: verify ABI negotiation against the pinned native version, run a
leak check for `dispose()`/`FinalizationRegistry` cleanup ordering, confirm callback exception
containment (a measurement callback that throws must not crash the process), verify display-list
command decoding against a fixture with every paint-command variant, confirm the published `.d.ts`
typings match the actual runtime surface (a type-only mismatch here is a silent contract break for
TypeScript consumers), and run a headless parse→style→layout→display-list smoke test for every
published platform binary (Windows, Linux, macOS; each supported Node/Deno/Bun runtime) before it
ships. If the WASM path is ever built out, it needs its own equivalent smoke test rather than
inheriting the native path's coverage.
