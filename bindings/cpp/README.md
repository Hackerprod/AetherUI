# AetherUI modern C++ binding

**Status: not implemented.** This directory defines the integration contract a modern C++ wrapper
library must satisfy — it is the spec a contributor implements against, not working code yet. It
consumes the *installed* C ABI only, the same header and installed target every other binding
consumes — never a private native header or a build-tree artifact.
[`bindings/dotnet/`](../dotnet/) is the reference implementation of the same ownership contract in
a different language; its `SafeHandle`-derived classes (`NativeDomDocument`, `NativeLayoutResult`)
are the concrete precedent for the RAII wrapper classes described below.

## Why this exists alongside `aether.h` and `examples/cpp/abi-info`

[`examples/cpp/abi-info`](../../examples/cpp/abi-info/) already demonstrates a **minimal** C++
consumer of `aether.h`: it links `AetherUI::Core`, calls `aether_abi_version()`/
`aether_abi_capabilities()`/`aether_paint_command_size()`, and fails the build on a major-version
mismatch. That is intentionally as far as it goes — it is a build/discovery smoke test, not an
application-facing API.

`bindings/cpp/` is a different thing: an idiomatic, RAII-based C++ wrapper *on top of* `aether.h`,
for application code that wants `std::unique_ptr`-friendly ownership, `std::string`/`std::vector`
instead of raw `char*`/count-and-index pairs, and `try`/`catch` error handling instead of manually
checking an `aether_status_t` after every call. It is not a new ABI — it compiles directly against
the installed C header and the installed native library, adds zero new native symbols of its own
beyond what a header-only (or small compiled) wrapper library provides, and any application already
comfortable calling `aether.h` directly loses nothing by skipping this binding entirely. The reason
it's worth having as a real target rather than "just use `aether.h`, C++ can call C headers fine":
raw `aether.h` usage means every call site re-derives its own ownership discipline, its own
status-code-checking convention, and its own borrowed-pointer lifetime bookkeeping by hand — exactly
the kind of repeated, error-prone boilerplate a thin wrapper layer exists to centralize once,
correctly, with the dependency ordering (style runtime outlives layouts, documents outlive nodes)
encoded in the type system instead of left to every caller's memory.

## Intended scope

The binding will expose DOM parsing, the style runtime, layout, hit testing, and display-list
generation as RAII C++ types. It may be used headlessly (parse → resolve → layout → read the
display list, no pixels drawn) or combined with a renderer supplied by the application — the same
separation `AetherUI.Windows` maintains from the portable .NET core binding. Distribution is either
header-only (templates and inline wrappers directly over `aether.h`) or a small compiled wrapper
library, whichever keeps the translation-unit cost reasonable once error-message formatting and any
non-trivial glue code are accounted for; either way it links against the same installed
`AetherUI::Core` target, never a private copy of the engine.

### Sketch of the target shape

Not an API commitment — illustrative, to make the ownership and error-handling contract concrete
before anyone writes wrapper code:

```cpp
namespace aether {

class aether_error : public std::runtime_error {
public:
    explicit aether_error(aether_status_t status);
    aether_status_t status() const noexcept;
};

class Document {
public:
    static Document parse(std::string_view html);  // throws aether_error

    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;             // handle is not copyable
    ~Document();                                     // aether_doc_destroy

    // ...

private:
    struct HandleDeleter { void operator()(aether_doc_t) const noexcept; };
    std::unique_ptr<std::remove_pointer_t<aether_doc_t>, HandleDeleter> handle_;
};

class StyleRuntime {
public:
    StyleRuntime(Options options, Environment env);  // throws aether_error
    StylesheetHandle add_stylesheet(std::string_view css, Layer layer, std::string_view source_name);
    ~StyleRuntime();                                  // aether_style_runtime_destroy
};

class LayoutResult {
public:
    // Borrowed Node/LayoutBox views obtained from this result become invalid once it is destroyed.
    std::optional<Node> hit_test(float x, float y) const noexcept;
    ~LayoutResult();                                  // aether_layout_result_destroy
};

}  // namespace aether
```

Every owning type follows Rule-of-Five with move-only semantics (mirroring the handle's inherent
non-shareability — an `aether_doc_t` has exactly one owner) and destructors that call the matching
`aether_*_destroy`/`aether_*_free`. Native status codes are translated to `aether_error` exceptions
at the wrapper boundary — an application built against `bindings/cpp/` should never need to inspect
a raw `aether_status_t` itself; that integer is the wrapper's implementation detail, not the
error-reporting contract exposed to app code (application code catching a C++ exception is a very
different, and much safer, default than application code that forgets to check a return value).

## Native library discovery

The wrapper is a CMake package consumed the same way `examples/cpp/abi-info` is built — via
`find_package(AetherUI CONFIG REQUIRED)` and `target_link_libraries(... AetherUI::Core)` — with its
own CMake target (e.g. `AetherUI::Cpp`) layered on top, so an application depends on one CMake
target and gets both the C ABI link and the RAII wrapper headers. pkg-config consumers use
`aetherui-core` the same as any other binding. Neither path ever points at `native/build/` or a
private CMake cache directory; only the installed tree under `<install-prefix>/` is a valid
consumption target, exactly as `native/README.md` specifies.

## ABI verification

Before creating a single handle, the wrapper library must validate, in a one-time initialization
path (e.g. a function-local static, thread-safe under C++11's magic-statics guarantee):

```text
aether_abi_version()
aether_abi_capabilities()
aether_paint_command_size()
```

Accept only a compatible ABI version (same major, minor ≥ what the wrapper was built against),
require only the capability bits its own code paths use, ignore unknown capability bits from a
newer library, and size every display-list command buffer using the runtime value from
`aether_paint_command_size()` — never `sizeof(aether_paint_command_t)` from the header the wrapper
happened to compile against, since an application can link a newer installed library at runtime than
the one the wrapper library itself was built and shipped against.

## Ownership model

Every owning native handle is held by exactly one RAII wrapper object using a `std::unique_ptr` with
a custom deleter (or an equivalent hand-rolled RAII member) bound to the matching
`aether_*_destroy`/`aether_*_free`. Wrapper types are move-only, not copyable — copying an
`aether_doc_t` has no native meaning, so the C++ type must not pretend otherwise by silently sharing
a handle across two live objects. Shared ownership, if an application genuinely needs it, is the
application's decision to make explicitly (e.g. wrapping the RAII type in its own `std::shared_ptr`),
not a default the binding should offer, since it obscures exactly the dependency-order bugs (a style
runtime destroyed while a layout still references it) that RAII is meant to catch at scope-exit time.

Nodes and layout boxes are non-owning, borrowed views (plain wrapper structs around the raw handle,
not RAII types) and must not outlive their owning `Document`/`LayoutResult`. Debug builds should
track a generation counter on the owner and assert (or throw `aether_error`) on access through a
stale borrowed view rather than silently reading freed native memory; a document reparse must
invalidate every outstanding node view for the prior tree the same way. Strings and gradient-stop
arrays returned inside display-list commands are copied into `std::string`/`std::vector<float>`
before the owning command — and the list — are destroyed; no wrapper type hands the application a
`char*` that aliases native memory past the call that produced it.

## Callback and threading policy

Native callbacks (text measurement, scroll-offset queries) are synchronous and run on whatever
native thread is currently inside the call that triggered them. A `std::function` passed as a
measurer must be heap-allocated and kept alive by the owning `LayoutEngine`/builder wrapper for the
full registration lifetime, with a raw pointer to it as the callback's `user_data` — never a pointer
into a temporary or a stack frame. Every trampoline that bridges the C callback into the
`std::function` call must be wrapped in `try { ... } catch (...) { return safe_fallback; }`; a C++
exception must never be allowed to unwind across the `extern "C"` callback boundary back into the
native engine's call stack, since that is undefined behavior even though both sides are nominally
"the same language" here — the ABI boundary, not the source language, is what matters.

ABI 0.1 requires serialized access to a handle graph — no two threads may operate on the same
document, style runtime, layout result, or display list concurrently. The wrapper should either
provide an explicit thread-confinement check (debug-only assertion comparing the creating thread ID)
or document plainly that callers must synchronize access themselves; it must not silently assume
single-threaded use without saying so, since C++ gives no free thread-safety guarantee the way a
GC'd runtime's default heap semantics might lull a caller into expecting.

## Release gates

Before publishing a wrapper release: verify ABI negotiation against the pinned native version, run
the wrapper's own test suite under AddressSanitizer/UndefinedBehaviorSanitizer for use-after-free and
double-destroy bugs in the RAII layer, fuzz a measurement callback that throws and confirm the
trampoline contains it with the native call still returning cleanly, verify display-list command
decoding against a fixture covering every paint-command variant, and run a headless
parse→style→layout→display-list smoke test on every platform/compiler combination the release
ships headers or a compiled wrapper library for.
