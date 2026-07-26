# AetherUI Go binding

**Status: not implemented.** This directory defines the integration contract a Go module must satisfy
— it is the spec a contributor implements against, not working code yet. The binding will consume the
*installed* C ABI through `cgo`; it must never bind to C++ symbols, private native headers, or a CMake
build tree. [`bindings/dotnet/`](../dotnet/) is the reference implementation of the same contract in a
different language — when in doubt about lifetime or ownership behavior, that's the binding to mirror.

## Intended scope

The binding will expose DOM parsing, the style runtime, layout, hit testing, and display-list
generation as idiomatic Go APIs. It may be used headlessly (parse → resolve → layout → read the
display list, no pixels drawn) or combined with a renderer supplied by the application or a future
platform package. Windowing, text measurement, and rasterization are not implied by the core binding —
they're a decision for whoever builds a Go rendering backend on top of it, the same way
`AetherUI.Windows` sits on top of the .NET binding without being part of it.

### Sketch of the target shape

Not an API commitment — illustrative, to make the contract concrete before anyone writes `cgo` code:

```go
package aetherui

type Document struct{ /* wraps aether_doc_t */ }

func Parse(html string) (*Document, error)
func (d *Document) Close() error

type StyleRuntime struct{ /* wraps aether_style_runtime_t */ }

func NewStyleRuntime(opts Options, env Environment) (*StyleRuntime, error)
func (r *StyleRuntime) AddStylesheet(css string, layer Layer, sourceName string) error
func (r *StyleRuntime) Close() error

type Layout struct{ /* wraps aether_layout_engine_t */ }

func NewLayout(measure MeasureFunc) (*Layout, error)
func (l *Layout) Run(doc *Document, styles *StyleRuntime, width, height float32) (*LayoutResult, error)
func (l *Layout) Close() error

type LayoutResult struct{ /* wraps aether_layout_result_t, borrowed views only */ }

func (r *LayoutResult) Close() error
func (r *LayoutResult) HitTest(x, y float32) (Node, bool)

func BuildDisplayList(result *LayoutResult, focused Node) (*DisplayList, error)
```

Every owning type gets a `Close() error`, mirroring `NativeDomDocument`/`NativeLayoutResult` on the
.NET side; every borrowed view (`Node`, a layout box) is invalid once its owner is closed.

## Native library discovery

Development uses the installed CMake/pkg-config contract — the same one `native/README.md` documents:

```text
<install-prefix>/include/aether/aether.h
<install-prefix>/lib/pkgconfig/aetherui-core.pc
<install-prefix>/bin/aether_core.dll                Windows
<install-prefix>/lib/libaether_core.so               Linux
<install-prefix>/lib/libaether_core.dylib             macOS
```

The `cgo` integration should prefer `#cgo pkg-config: aetherui-core` where `pkg-config` is available
(set `PKG_CONFIG_PATH` to `<install-prefix>/lib/pkgconfig`), with a documented environment-variable
override (e.g. `AETHERUI_INSTALL_PREFIX`) for environments without `pkg-config`. Release artifacts must
be selected by `GOOS`/`GOARCH` at build time; they must never be copied by hand from a developer's
`native/build/` directory — see [`examples/cpp/abi-info`](../../examples/cpp/abi-info/) for the
equivalent discovery pattern in plain CMake, which the Go module's own build tooling should mirror.

## ABI verification

Before exposing a single native handle to a Go caller, the binding must validate, at `init()` or on
first use:

```text
aether_abi_version()
aether_abi_capabilities()
aether_paint_command_size()
```

Accept only a compatible ABI version (same major, minor ≥ what the binding was written against),
require only the capability bits the binding actually calls into, ignore unknown capability bits from
a newer library, and allocate every display-list command buffer using the size `aether_paint_command_size()`
reports at runtime — never a Go struct's `unsafe.Sizeof` guess at the C layout.

## Go ownership model

Every owning native handle is wrapped by a Go value with an explicit `Close() error`. A `runtime.
SetFinalizer` can report or recover an abandoned handle as a last-resort leak detector, but callers
must not rely on finalizers for timely cleanup — native resources have a dependency order (a style
runtime must outlive layouts built against it; a document must outlive nodes borrowed from it) that a
GC finalization pass does not respect.

Nodes and layout boxes are borrowed views and cannot outlive their document or layout result. A
document reparse invalidates every node wrapper for the old tree — the binding should either poison
those wrappers explicitly or document, loudly, that using one after a reparse is undefined behavior on
the native side. Display-list text and gradient-stop data are copied into Go-owned memory (`[]byte`/
`string`, not a `*C.char` alias) before the command — and the list that owns it — is freed.

## cgo callback safety

The binding must obey the [cgo pointer-passing rules](https://pkg.go.dev/cmd/cgo#hdr-Passing_pointers):
a Go pointer handed to C must not be retained by C beyond the call that received it, so callback
user-data needs a `cgo.Handle` (or an integer-indexed registry) rather than a raw `*T` cast through
`unsafe.Pointer`. Native callbacks (text measurement, scroll-offset queries) are synchronous and run on
whatever OS thread native code is currently executing on — the callback implementation must recover any
panic and return a documented safe native value, exactly like the ABI's own C-side rule about not
letting exceptions unwind across language boundaries. A panic escaping a cgo callback back into C is
undefined behavior, not a Go runtime panic someone can `recover()` from on the other side.

ABI 0.1 requires serialized access to a handle graph — no two goroutines may operate on the same
document, style runtime, layout result, or display list concurrently. The Go API should either enforce
this with its own mutex per owning handle or document the restriction explicitly enough that a caller
sharing a `*Document` across goroutines finds out from the doc comment, not from a native crash.

## Release gates

Before publishing a module version: verify ABI negotiation against the pinned native version, run a
leak check for explicit-`Close()` cleanup ordering, confirm callback panics are contained (fuzz a
measurement callback that panics and assert the native call still returns cleanly), verify display-list
command decoding against a fixture with every paint-command variant, and run a headless
parse→style→layout→display-list smoke test for every published `GOOS/GOARCH` artifact before it ships.
