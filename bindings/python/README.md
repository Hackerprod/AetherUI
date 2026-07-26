# AetherUI Python binding

**Status: not implemented.** This directory defines the integration contract a Python package must
satisfy — it is the spec a contributor implements against, not working code yet. The binding
consumes the *installed* C ABI only; it must never link against C++ symbols, a private native
header, or a CMake build tree. [`bindings/dotnet/`](../dotnet/) is the reference implementation of
the same contract in a different language — when a lifetime or ownership question isn't obvious
from this document, that binding's `SafeHandle`-derived classes (`NativeDomDocument`,
`NativeLayoutResult` in `AetherUI.Core.Native.Interop`) are the concrete precedent to mirror.

## Intended scope

The binding will expose DOM parsing, the style runtime, layout, hit testing, and display-list
generation as an idiomatic Python API. It may be used headlessly (parse → resolve → layout → read
the display list, no pixels drawn — a natural fit for server-side rendering, snapshot testing, or
static-site generation from HTML/CSS) or combined with a renderer supplied by the application or a
future platform package. Windowing, text measurement, and rasterization are not implied by the core
binding — that split mirrors how `AetherUI.Windows` sits on top of the .NET binding without being
part of it.

### Two FFI strategies

- **`ctypes`** (standard library, no build step) is the baseline. It works with a stock CPython
  install and requires nothing beyond `pip install aetherui` locating and `ctypes.CDLL`-loading the
  installed native library — appropriate as the default so the package has zero native-toolchain
  requirement for end users.
- **`cffi`** is an alternative, offered for better call-overhead performance and more ergonomic
  struct/callback declarations (`cffi`'s ABI mode can still avoid a compiler at install time,
  matching `ctypes`'s no-build-step property; API mode trades that for speed). If both exist, they
  must expose one identical public Python API — application code should never need to know or care
  which FFI layer is loaded underneath.

### Sketch of the target shape

Not an API commitment — illustrative, to make the ownership contract concrete before anyone writes
`ctypes`/`cffi` code:

```python
class Document:
    """Wraps aether_doc_t."""

    @classmethod
    def parse(cls, html: str) -> "Document": ...  # aether_doc_create + ...parse_html

    def close(self) -> None: ...  # aether_doc_destroy

    def __enter__(self) -> "Document": return self
    def __exit__(self, *exc) -> None: self.close()

    def __del__(self) -> None: ...  # last-resort net; see below


class StyleRuntime:
    def __init__(self, options: Options, environment: Environment) -> None: ...
    def add_stylesheet(self, css: str, layer: Layer, source_name: str) -> int: ...
    def close(self) -> None: ...  # aether_style_runtime_destroy


class LayoutEngine:
    def __init__(self, measure: Callable[[str, ComputedStyleView, float], TextMetrics]) -> None: ...
    def layout(self, doc: Document, styles: StyleRuntime, width: float, height: float) -> "LayoutResult": ...
    def close(self) -> None: ...


class LayoutResult:
    # Borrowed Node/LayoutBox views obtained from this result become unusable after close().
    def hit_test(self, x: float, y: float) -> Optional["Node"]: ...
    def close(self) -> None: ...  # aether_layout_result_destroy
```

Every owning type is a context manager, so `with Document.parse(html) as doc: ...` is the natural
shape — deliberately parallel to `SafeHandle`'s deterministic-disposal role in the .NET binding, and
to `try (var doc = Document.parse(html))` in the Java contract.

## Native library discovery

Development uses the installed CMake/pkg-config contract documented in `native/README.md`:

```text
<install-prefix>/include/aether/aether.h
<install-prefix>/lib/pkgconfig/aetherui-core.pc
<install-prefix>/bin/aether_core.dll                Windows
<install-prefix>/lib/libaether_core.so               Linux
<install-prefix>/lib/libaether_core.dylib             macOS
```

Library loading should try, in order: an explicit override (an `AETHERUI_NATIVE_LIBRARY` or
`AETHERUI_INSTALL_PREFIX` environment variable), a native shared library bundled inside the Python
wheel itself for the platform's target triple (the common distribution shape for a `ctypes`-based
package — no separate native install step for the end user), and finally the system loader's normal
search path. It must never read from `native/build/` or a developer's private CMake cache — see
[`examples/cpp/abi-info`](../../examples/cpp/abi-info/) for the equivalent discovery pattern in
plain CMake that any wheel-building tooling should mirror when it invokes `cmake --install` to stage
the bundled library.

## ABI verification

Before exposing a single native handle to a Python caller, the binding must validate, at import
time or on first use:

```text
aether_abi_version()
aether_abi_capabilities()
aether_paint_command_size()
```

Accept only a compatible ABI version (same major, minor ≥ what the binding was written against),
require only the capability bits the binding actually calls into, ignore unknown capability bits
from a newer library, and allocate every display-list command buffer using the size
`aether_paint_command_size()` reports at runtime — never a `ctypes.Structure`/`cffi` type's
hard-coded byte size, since a stale Python-side struct definition and the loaded library's actual
`aether_paint_command_t` layout can silently drift apart as paint features are added.

## Ownership model

The public API uses context managers (`__enter__`/`__exit__`) for every owning handle — documents,
style runtimes, layout engines, layout results, display lists — as the primary, encouraged pattern.
`__del__` may exist as a last-resort safety net for a handle a caller forgot to close explicitly,
but it carries the same caveat as Java's `Cleaner` or Go's finalizer: CPython's garbage collector
gives no ordering or timing guarantee (and in the presence of reference cycles, may not even run
`__del__` promptly, or ever, without `gc.collect()`), while native objects have a strict dependency
order — a style runtime must outlive every layout built from it, a document must outlive nodes
borrowed from it. Relying on `__del__` for that ordering will eventually produce a use-after-free.
`__del__` should log a warning when it fires on a handle that was never explicitly closed, not
silently clean up and let the pattern go unnoticed.

Nodes and layout boxes are borrowed views. They must hold a reference back to their owning
`Document`/`LayoutResult` (so Python's own reference counting keeps the owner's Python wrapper
alive) and raise a clear `AetherDisposedError` — never crash the interpreter — once that owner has
been closed. Re-parsing a document invalidates every node wrapper for the previous tree; the binding
should track a generation counter on `Document` and check it on each borrowed-view access rather
than trust the caller to discard stale references. Strings and gradient-stop arrays returned inside
display-list commands are copied into Python-owned `str`/`list[float]` values (via `ctypes.string_at`
or the `cffi` equivalent) before `aether_paint_command_free()` runs — a Python `str` must never be
built as a thin alias over native memory that outlives the command's lifetime.

## Callback and threading policy — the GIL

Native callbacks (text measurement, scroll-offset queries) are synchronous, invoked on whatever
native thread is currently inside the call that triggered them. Before a callback trampoline can run
any Python code, it must reacquire the GIL: `ctypes` callback thunks and `cffi` `@ffi.callback`
functions do this automatically for the thread that originally called into the FFI, but if a future
platform layer ever invokes a registered callback from a **different** native thread than the one
that made the original call (not expected for ABI 0.1's synchronous callback model, but a real
hazard if that ever changes), the trampoline must explicitly call `PyGILState_Ensure()`/
`PyGILState_Release()` around the callback body — calling into CPython's interpreter without holding
the GIL is memory-unsafe, not merely slow. Every callback implementation must also catch every
Python exception raised inside it and return a documented safe fallback value: an exception escaping
a `ctypes`/`cffi` callback back into C is at best silently swallowed with a printed traceback and at
worst undefined behavior, never a Python exception the caller can catch on the other side.

ABI 0.1 requires serialized access to a handle graph — no two threads may operate on the same
document, style runtime, layout result, or display list concurrently. The binding should either
guard each owning wrapper with a `threading.Lock` or document the restriction loudly enough that a
caller sharing a `Document` across threads (easy to do by accident once the GIL makes people assume
thread-safety is free) finds out from the docstring, not from a native crash the GIL does nothing to
prevent — the GIL serializes *Python bytecode*, not native calls made while it's released for a
long-running native operation.

## Release gates

Before publishing a package version: verify ABI negotiation against the pinned native version, run
a leak check for `close()`/context-manager cleanup ordering across CPython and PyPy if PyPy is a
supported target, confirm callback exception containment (a measurement callback that raises must
not crash the interpreter or corrupt the native call stack), verify display-list command decoding
against a fixture with every paint-command variant, and run a headless parse→style→layout→
display-list smoke test against every published wheel platform tag before it ships.
