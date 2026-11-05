# AetherUI native core

This is the product. Everything else in this repository — `bindings/`, `examples/`, `tools/` —
consumes what is built and installed from here. The implementation is C++20; the only thing a
consumer is allowed to depend on is the C header at [`include/aether/aether.h`](include/aether/aether.h)
plus the installed binary it declares.

The core owns HTML parsing, the DOM, the CSS cascade (parser, layered cascade, selector matching
including `:has()`/`:is()`/`:not()`/nth-child, custom properties, `calc()`, media queries), layout
(block/inline/flex, intrinsic sizing, vertical scroll, hit testing), and retained display-list
generation. It does not own windows, an OS event loop, text shaping/rasterization, or a graphics API —
those are host concerns, deliberately left to each binding.

## Module map

```text
src/
  dom/            Node/ElementNode/DocumentNode, HTML parser
  css/             Legacy simplified cascade + primitives (CssLength, ColorRgba, ComputedStyle) —
                    kept for the primitive/geometry types layout and rendering still share
  style_runtime/  The real cascade engine: property registry, selector parser/matcher, rule index,
                    cascade resolution, custom-property/var() resolution, media conditions, the
                    CSS→compiled-rule source compiler, and the AetherStyleRuntime equivalent
  layout/         LayoutBox/LayoutResult and the block/inline/flex layout engine
  rendering/      DisplayList and DisplayListBuilder — the paint-command model
  primitives/     RectF/PointF/SizeF geometry shared across every module
  abi/            aether_abi_version()/aether_abi_capabilities()/aether_paint_command_size() and
                    internal C-string/error-code helpers used by every public function
```

`css/` predates `style_runtime/`: it's the original, simplified hand-rolled cascade. It has no
production consumer today — every real caller goes through `style_runtime/`. It stays because
`ComputedStyle`, `CssLength`, and `ColorRgba` are the value types layout and the display-list builder
still consume directly. Don't add new cascade logic there; extend `style_runtime/` instead.

## ABI contract

The current ABI is **0.1.0**: flat C functions, opaque handles, fixed-width value types, integer
status codes, and plain C callbacks. No C++ classes, STL containers, or exceptions cross the boundary
— every public function is a `noexcept`-equivalent that converts any internal exception into a status
code before returning.

Every consumer must inspect the loaded library before creating a single handle:

```c
#include <aether/aether.h>

uint32_t version = aether_abi_version();
if (AETHER_ABI_VERSION_GET_MAJOR(version) != AETHER_ABI_VERSION_MAJOR) {
    /* refuse to load: major-version mismatch is a breaking-change signal */
}

aether_capabilities_t capabilities = aether_abi_capabilities();
size_t paint_command_size = aether_paint_command_size();
```

`aether_paint_command_size()` exists specifically so a binding never hard-codes
`sizeof(aether_paint_command_t)`. The display-list command union grows as paint features are added;
allocate command buffers from this call, not from a literal.

The ABI advertises DOM, style-runtime, layout, display-list, render-plan and input-hit-test capabilities through
`aether_abi_capabilities()`. Treat unset bits as "not available in this build," and ignore bits you
don't recognize — a newer library may set capability bits an older binding has never heard of.

**Ownership.** Every `aether_*_create`/`aether_*_parse_*` that returns an owning handle has an exact
matching `aether_*_destroy`/`aether_*_free`. Nodes, layout boxes, and computed-style views obtained
from an owning handle are *borrowed* — they are valid only while their owner is alive, and become
invalid the instant the owner is destroyed or the document is reparsed. Copy out anything you need to
outlive the owner (text, gradient stop arrays) before releasing it.

**Threading.** Handle access is serialized in ABI 0.1: no two threads may operate on the same
document, style runtime, layout result, or display list concurrently. Callbacks (text measurement,
scroll-offset queries) run synchronously on the calling thread during the native call that triggered
them, must remain valid for as long as they're registered, and must not let an exception, panic, or
longjmp unwind through native stack frames — catch everything at the callback boundary and return a
safe fallback value instead.

See the per-binding READMEs under `bindings/` for how each language enforces these rules in its own
idiom (`SafeHandle` in .NET, `AutoCloseable` in Java, an explicit `Close()` plus cgo pointer rules in
Go).

## Build, test, and install

The install tree — not the build tree — is the consumption contract. Never point a consumer at
`native/build/` or a private CMake cache directory.

**Windows (MSVC):**

```powershell
cmake -S native -B artifacts/build/native/win-x64 -A x64 `
  -DAETHERUI_BUILD_TESTS=ON `
  -DCMAKE_INSTALL_PREFIX=artifacts/native/win-x64
cmake --build artifacts/build/native/win-x64 --config Release --parallel
ctest --test-dir artifacts/build/native/win-x64 -C Release --output-on-failure
cmake --install artifacts/build/native/win-x64 --config Release
```

**Linux / macOS:**

```sh
cmake -S native -B artifacts/build/native/$(uname -m) \
  -DCMAKE_BUILD_TYPE=Release \
  -DAETHERUI_BUILD_TESTS=ON \
  -DCMAKE_INSTALL_PREFIX=artifacts/native/$(uname -m)
cmake --build artifacts/build/native/$(uname -m) --parallel
ctest --test-dir artifacts/build/native/$(uname -m) --output-on-failure
cmake --install artifacts/build/native/$(uname -m)
```

`AETHERUI_BUILD_TESTS` defaults to CMake's own `BUILD_TESTING`; pass it explicitly if your generator
doesn't set that automatically. Two test binaries run under CTest:

- `aether.smoke` (`tests/test_smoke.cpp`) — a C++ compile/link smoke test against the installed target.
- `aether.abi-c` (`tests/test_abi_c.c`) — a **C11** consumer, compiled and linked with no C++ in its
  translation unit, proving the header is genuinely C-clean and not "C++ wearing an `extern "C"` hat."

`tests/package_consumer/` is a separate, standalone CMake project (its own `CMakeLists.txt`, not part
of the main build) that runs `find_package(AetherUI CONFIG REQUIRED)` against an *installed* tree and
links `AetherUI::Core` — this is the regression test for the install/export step itself, not for the
library's behavior. Run it after `cmake --install` to catch a broken `AetherUIConfig.cmake` or a
missing installed header before a downstream binding does.

## Installed package

CMake consumers use the exported imported target:

```cmake
find_package(AetherUI CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE AetherUI::Core)
```

pkg-config consumers (non-CMake C/C++ builds, most cgo setups) use:

```sh
pkg-config --cflags --libs aetherui-core
```

Both are generated from `cmake/AetherUIConfig.cmake.in` and `cmake/aetherui-core.pc.in` at configure
time and installed under `<prefix>/lib/cmake/AetherUI/` and `<prefix>/lib/pkgconfig/` respectively —
private headers and build-tree binaries (`native/build/`) are never a distribution interface, only
`<prefix>/include/aether/aether.h` and the installed library are.

See [`examples/cpp/abi-info`](../examples/cpp/abi-info/) for the minimal working consumer: it links
against the installed package, calls `aether_abi_version()`/`aether_abi_capabilities()`/
`aether_paint_command_size()`, and fails the build if the loaded ABI's major version doesn't match
what it was compiled against. Copy that project's `CMakeLists.txt` as the starting point for a new
native or cgo consumer.

## HTML attribute values

Slashes are data in unquoted HTML attribute values, including a slash directly
before `>`: `src=logo.svg/>` has the value `logo.svg/`, while `src=logo.svg />`
has the value `logo.svg` followed by the parser's self-closing marker. URLs and
paths are not normalized. The parser extracts the raw value before decoding
HTML entities once; decoded whitespace, quotes, and `>` never become delimiters.

`aether.html-unquoted-attributes` checks the complete attribute map through the
public ABI, including real versus accidentally invented `disabled` attributes,
child relationships, production cascade matches, and reparsing. Its checks also
run with `NDEBUG`. The slash boundary follows HTML tokenization, but this is not
a claim of general HTML5 conformance: the existing entity decoder, `std::isspace`
behavior (including vertical tab), incomplete-start-tag recovery at EOF, and
self-closing behavior for non-void elements remain unchanged.

## Custom properties

Custom-property names use decoded CSS identifier values with exact case and Unicode
identity: `--Brand` and `--brand` are distinct, while `--j`, `--\6a` and `--\6A`
name the same property. Declarations, inline styles, inheritance and `var()` share
this identity. Standard property and `var` function names remain case-insensitive.
Values retain their source spelling. Custom-only changes refresh inherited scopes
even when the ancestor's concrete style does not change.

`aether.custom-property-case` checks this through the public ABI, including escaped
names, cascading, recomputation and snapshot lifetime. `aether.css-variable-strings`
also covers the private decoder and resolver budgets. Private scopes must use
decoded keys; source spellings are not a compatibility alias. This does not add
`@property`, computed-value alias inheritance, full `revert` semantics, or deferred
inline shorthands, and does not normalize Unicode.

## Visibility

`visibility:hidden` preserves layout and overflow geometry. It suppresses the
node's own paint and hit eligibility, while descendants with computed
`visibility:visible` remain visible and interactive within the existing ancestor
transform, clip, scroll and opacity scopes. Visibility is inherited. Changing
only its computed value on a stable DOM topology requires paint/hit updates,
not layout; pass a current style map for live hit testing and display lists.
The hit APIs without a style map intentionally use the layout's captured styles.
`aether_layout_result_hit_test_input` (capability bit 5) returns the frontmost
eligible content, thumb, track or corner plus a borrowed native owner, in reverse
local paint order. Own chrome precedes children and uses snapshot geometry with
live thumb offsets. A null offset callback uses snapshot scroll offsets for this
new API; the three DOM hit APIs retain their previous behavior. Outputs are
initialized to NONE/null before argument validation; a valid miss is OK/NONE/null.
The query preserves generated owners even when a binding cannot project them.

`visibility:collapse` is currently an alias of `hidden` for all displays,
including table rows/cells and flex items. CSS table/flex geometric collapse is
not implemented. Tables retain their existing unspanned phase-1 layout, and
flattened inline ancestors and table groups do not gain new paint scopes.
DOM hit testing retains the existing rectangular clipping, border-box candidates (including border and padding, excluding margins),
and traversal order; this change does not add inverse-transform hit testing.
Hit queries accept logical input coordinates without implicit viewport rejection
or clamping. Eligible boxes outside the viewport remain queryable, subject to
existing clips and scroll offsets. This coordinate domain differs from a browser's
`Document.elementFromPoint` query.
Generated pseudo-elements are still recreated by the runtime, so their identity
changes require layout even when triggered by a visibility declaration.
Canvas background propagation remains separate from a box's own visibility.
