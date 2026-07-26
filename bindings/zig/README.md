# AetherUI Zig binding

**Status: not implemented.** This directory defines the integration contract a Zig package must
satisfy — it is the spec a contributor implements against, not working code yet. The binding
consumes the *installed* C ABI only; it must never link against C++ symbols, a private native
header, or a CMake build tree. [`bindings/dotnet/`](../dotnet/) is the reference implementation of
the same contract in a different language — when a lifetime or ownership question isn't obvious
from this document, that binding's `SafeHandle`-derived classes (`NativeDomDocument`,
`NativeLayoutResult` in `AetherUI.Core.Native.Interop`) are the concrete precedent to mirror.

## Why Zig is the lowest-friction binding to prototype

`@cImport(@cInclude("aether/aether.h"))` gives Zig code direct, essentially zero-effort access to
every function, struct, and enum in the header — no bindgen tool, no `.def`/module-map/IDL file, no
separate code-generation build step at all. Zig's C interop is a first-class, built-in compiler
feature specifically because Zig's own type system was designed to represent C types (including
unions, bitfields, and function pointers) natively. Since `aether.h` is already plain, portable C —
no macros the translator can't handle, no C++ template instantiation to approximate — this is about
as close to "add one import line and start calling the ABI" as any binding in this repository gets.
That ease is exactly why this binding is worth documenting even before Zig has a stable 1.0 release:
it is the cheapest way for a contributor to validate a new corner of the ABI end-to-end.

## Intended scope

The binding will expose DOM parsing, the style runtime, layout, hit testing, and display-list
generation as an idiomatic Zig API — thin wrapper structs and functions returning Zig error unions
instead of raw `aether_status_t` values. It may be used headlessly (parse → resolve → layout → read
the display list, no pixels drawn) or combined with a renderer supplied by the application. Windowing,
text measurement, and rasterization are not implied by the core binding — that split mirrors how
`AetherUI.Windows` sits on top of the .NET binding without being part of it.

### Sketch of the target shape

Not an API commitment — illustrative, to make the ownership contract concrete before anyone writes
wrapper code:

```zig
const c = @cImport(@cInclude("aether/aether.h"));

pub const AetherError = error{ NullArg, InvalidState, OutOfMemory, Disposed, ParseFailed };

pub const Document = struct {
    handle: c.aether_doc_t,

    pub fn parse(html: []const u8) AetherError!Document {
        // aether_doc_create + aether_doc_parse_html, translating aether_status_t -> AetherError
    }

    pub fn deinit(self: *Document) void {
        c.aether_doc_destroy(self.handle);
        self.handle = null;
    }
};

pub const StyleRuntime = struct {
    handle: c.aether_style_runtime_t,

    pub fn init(options: Options, environment: Environment) AetherError!StyleRuntime { .. }
    pub fn addStylesheet(self: *StyleRuntime, css: []const u8, layer: Layer, source_name: []const u8) AetherError!StylesheetHandle { .. }
    pub fn deinit(self: *StyleRuntime) void { c.aether_style_runtime_destroy(self.handle); }
};
```

Every owning type has an explicit `deinit()`, meant to be called via `defer` at the point of
creation — `var doc = try Document.parse(html); defer doc.deinit();` is the natural shape,
deliberately parallel to `SafeHandle`'s deterministic-disposal role in the .NET binding. Zig has no
implicit destructor mechanism and no garbage collector, so `defer doc.deinit()` is not a convention
of convenience here the way scope-based cleanup is in a GC'd language — it is the only cleanup
mechanism that exists, which makes disciplined, consistent use of it non-negotiable for this binding
specifically.

## Allocator discipline

Zig's own idiom is explicit allocator passing (`std.mem.Allocator` threaded through every
allocating function), but the native library has no concept of a Zig allocator and never will —
every native allocation made inside `aether_core` (a document's DOM tree, a `char*` returned by
`aether_node_get_text_content`, a `aether_paint_command_t`'s embedded `text`/gradient-stop pointers)
is made with the native library's own internal allocator, and must be released exclusively through
the matching native free function: `aether_string_free()` for `char*` returns,
`aether_paint_command_free()`/`aether_linear_gradient_free()` for command/gradient payloads, and the
appropriate `aether_*_destroy` for every owning handle. **A Zig caller must never pass a native
pointer to `allocator.free()`, and must never assume `aether_string_free` internally uses whatever
allocator the Zig application configured (a `GeneralPurposeAllocator`, an arena, `page_allocator`, or
anything else)** — doing so is a guaranteed heap corruption bug, not merely an inefficiency, because
the native allocator and a Zig-side allocator are two entirely separate allocation domains with no
shared bookkeeping. Where the binding copies native data into Zig-owned memory (see below), that copy
*does* go through a caller-supplied `std.mem.Allocator` and is the caller's normal responsibility to
free with the same allocator — the rule above applies specifically to pointers that came directly out
of the native library and haven't been copied yet.

## Native library discovery

Development uses the installed CMake/pkg-config contract documented in `native/README.md`:

```text
<install-prefix>/include/aether/aether.h
<install-prefix>/lib/pkgconfig/aetherui-core.pc
<install-prefix>/bin/aether_core.dll                Windows
<install-prefix>/lib/libaether_core.so               Linux
<install-prefix>/lib/libaether_core.dylib             macOS
```

`build.zig` should resolve the installed prefix via an environment variable (e.g.
`AETHERUI_INSTALL_PREFIX`) or, where available, by shelling out to `pkg-config --cflags --libs
aetherui-core`, adding the resulting include path to the `@cImport` search path and linking the
resulting library path — it must never point at `native/build/` or a private CMake cache directory.
See [`examples/cpp/abi-info`](../../examples/cpp/abi-info/) for the discovery pattern `build.zig`'s
native-resolution step should mirror.

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
`aether_paint_command_size()` — never `@sizeOf(c.aether_paint_command_t)`, since Zig's C translator
reflects whichever header it processed at compile time, not necessarily the ABI of whatever native
library is actually loaded at runtime.

## Ownership model

Every owning native handle is wrapped by a Zig struct with an explicit `deinit()` calling the
matching `aether_*_destroy`/`aether_*_free`; there is no finalizer or GC-driven fallback to lean on,
consistent with Zig having neither. Nodes and layout boxes are borrowed views — thin structs wrapping
the raw handle with no `deinit()` of their own — and must not be retained past their owning
`Document`/`LayoutResult`'s `deinit()`. The binding should track a generation counter on `Document`
and check it on each borrowed-view access, returning `AetherError.Disposed` rather than
dereferencing a freed native pointer. Strings and gradient-stop arrays returned inside display-list
commands are copied into caller-allocator-owned `[]u8`/`[]f32` slices (via the allocator the caller
passed in) before `aether_paint_command_free()` runs and the native pointer is released through the
matching native free function, per the allocator-discipline rule above.

## Callback safety

Native callbacks (text measurement, scroll-offset queries) are synchronous and run on whatever
native thread is currently inside the call that triggered them. Zig function pointers passed as
`aether_text_measurer_fn`/`aether_scroll_offset_fn` must be plain functions (not closures capturing
context implicitly — Zig has none), with any needed context passed through the callback's
`user_data: *anyopaque` parameter and cast back with `@ptrCast`/`@alignCast` inside the trampoline.
Every trampoline must contain anything the callback body could otherwise propagate as a Zig error or
a panic: a Zig `@panic`/unrecoverable trap crossing the callback boundary back into native code is
undefined behavior, not a Zig panic the native side can catch, so the trampoline should catch Zig
error unions explicitly and translate any panicable path into a documented safe fallback return
value instead of letting it happen.

ABI 0.1 requires serialized access to a handle graph — no two threads may operate on the same
document, style runtime, layout result, or display list concurrently. The binding should either wrap
each owning handle with its own `std.Thread.Mutex` or document the restriction explicitly enough
that a caller sharing a `*Document` across threads finds out from a doc comment, not from a native
crash — Zig's lack of a built-in data-race detector for this kind of cross-language sharing makes
explicit documentation here more load-bearing than in a language with stronger static guarantees.

## Release gates

Before publishing a package version: verify ABI negotiation against the pinned native version, run
the binding's test suite under Zig's own `GeneralPurposeAllocator` leak-detection mode (with the
allocator-discipline boundary above verified — no native pointer ever handed to a Zig allocator's
`free`), fuzz a measurement callback that returns an error/panics and confirm the trampoline contains
it, verify display-list command decoding against a fixture with every paint-command variant, and run
a headless parse→style→layout→display-list smoke test on every platform/architecture the release
ships a build for.
