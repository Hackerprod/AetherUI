# AetherUI

AetherUI is an embeddable UI engine written in C++. It parses HTML and CSS, builds a DOM, resolves styles, performs layout, and emits a retained display list. Its public boundary is a C ABI designed for C, C++, C#, Java, Go, and other languages.

The current ABI version is **0.1.0**. The .NET binding is the reference integration and validation host. Java and Go bindings are planned but are not implemented yet.

> **Status: engineering alpha.** The native contract and .NET integration are suitable for development and validation, but AetherUI is not yet a general-purpose browser replacement or production framework.

## Why a C++ core

AetherUI did not start this way — it began as a pure C# engine, and the CSS cascade, layout, and
display-list generation were later ported to C++ one subsystem at a time, each cutover validated
against the C# implementation's own test assertions before the managed code was deleted. The reason to
do that instead of keeping a fast, working C# engine: a managed runtime is a ceiling on which languages
can embed AetherUI, and on how the engine ships (JIT startup cost, GC pauses in a paint pipeline,
runtime distribution size). A C ABI has none of those constraints — it's the lowest common denominator
every mainstream language can call into directly or through a thin native-interop layer, which is what
makes `bindings/dotnet`, `bindings/java`, and `bindings/go` peers instead of one primary implementation
with ports bolted on.

## Repository layout

```text
native/                 Portable C++ core, public C header, CMake, and native tests
bindings/
  dotnet/               Reference binding, Windows backend, tests, and Validator
  java/                 Planned Java binding contract
  go/                   Planned Go binding contract
  rust/                 Planned Rust binding contract
  python/               Planned Python binding contract
  cpp/                  Planned modern C++ (RAII) binding contract
  swift/                Planned Swift binding contract
  kotlin/                Planned Kotlin/Native binding contract
  zig/                  Planned Zig binding contract
  dart/                 Planned Dart binding contract
  javascript/           Planned JavaScript/TypeScript binding contract
examples/
  cpp/                  Installed CMake-package consumer
  dotnet/               .NET example applications
artifacts/              Generated build, install, package, and publish output
```

Language-specific guidance:

- [Native core](native/README.md)
- [.NET](bindings/dotnet/README.md)
- [Java](bindings/java/README.md)
- [Go](bindings/go/README.md)
- [Rust](bindings/rust/README.md)
- [Python](bindings/python/README.md)
- [C++ (modern/RAII binding)](bindings/cpp/README.md)
- [Swift](bindings/swift/README.md)
- [Kotlin/Native](bindings/kotlin/README.md)
- [Zig](bindings/zig/README.md)
- [Dart](bindings/dart/README.md)
- [JavaScript/TypeScript](bindings/javascript/README.md)

## Architecture boundary

```text
application
    |
language binding / host
    |
aether.h (versioned C ABI)
    |
DOM -> CSS cascade -> layout -> display list
                                  |
                         renderer or headless consumer
```

The core does not create windows or depend on a graphics API. `AetherUI.Windows` is a Windows/.NET host and Direct2D/DirectWrite backend; it is not part of the portable core. A future Java or Go consumer may use the engine headlessly or provide its own text measurement, windowing, input, and rendering backend.

Inside `native/`, the boundary above the C ABI is itself modular — DOM/HTML parsing, the CSS style
runtime (parser, cascade, selectors, custom properties, media queries), layout, and display-list
generation are separate translation units with no upward dependency on any binding. See
[`native/README.md`](native/README.md#module-map) for the full module breakdown.

## Build and verify

From the repository root:

```sh
cmake -S native -B artifacts/build/native/win-x64 \
  -A x64 \
  -DAETHERUI_BUILD_TESTS=ON \
  -DCMAKE_INSTALL_PREFIX=artifacts/native/win-x64
cmake --build artifacts/build/native/win-x64 --config Release --parallel
ctest --test-dir artifacts/build/native/win-x64 -C Release --output-on-failure
cmake --install artifacts/build/native/win-x64 --config Release
```

Omit `-A x64` when using a single-configuration generator or a non-Windows host. To verify the .NET reference integration:

```powershell
dotnet restore .\bindings\dotnet\AetherUI.DotNet.sln
dotnet build .\bindings\dotnet\AetherUI.DotNet.sln -c Release --no-restore
dotnet run --project .\bindings\dotnet\tests\AetherUI.Tests\AetherUI.Tests.csproj -c Release --no-build
```

## Installed native package

CMake consumers use the exported target:

```cmake
find_package(AetherUI CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE AetherUI::Core)
```

pkg-config consumers use:

```sh
pkg-config --cflags --libs aetherui-core
```

Every binding must validate `aether_abi_version()` and the required bits from `aether_abi_capabilities()` before creating handles. Display-list consumers must allocate command buffers using `aether_paint_command_size()` rather than a hard-coded struct size.

## Binding status

| Consumer | Status | Rendering | ABI validated against |
|---|---|---|---|
| C / C++ | Available | Headless or consumer-provided backend | 0.1.0 (`examples/cpp/abi-info`) |
| .NET | Reference binding | Windows backend (Direct2D/DirectWrite) included | 0.1.0 |
| Java | Contract defined, not implemented | Headless or future Java backend | — |
| Go | Contract defined, not implemented | Headless or future Go backend | — |
| Rust | Contract defined, not implemented | Headless or future Rust backend | — |
| Python | Contract defined, not implemented | Headless or future Python backend | — |
| C++ (RAII binding) | Contract defined, not implemented | Headless or consumer-provided backend | — |
| Swift | Contract defined, not implemented | Headless or future Swift backend | — |
| Kotlin/Native | Contract defined, not implemented | Headless or future Kotlin/Native backend | — |
| Zig | Contract defined, not implemented | Headless or future Zig backend | — |
| Dart | Contract defined, not implemented | Headless or future Dart backend | — |
| JavaScript/TypeScript | Contract defined, not implemented | Headless or future JS/TS backend | — |

"Contract defined" means the binding's `README.md` documents the ownership, threading, and callback
rules it must implement, with the .NET binding as the reference to mirror — see
[`bindings/java/README.md`](bindings/java/README.md) and [`bindings/go/README.md`](bindings/go/README.md).

## Collaboration

Contributions are welcome. Useful areas include the portable C++ core, ABI validation,
language bindings, rendering backends, examples, tests, and platform packaging.

Before opening a change, keep the C ABI as the only cross-language contract, avoid
depending on private native headers or build directories, and validate the affected
native and binding paths. Small, focused changes with a reproducible verification
command are especially helpful.

**Visual correctness rule (non-negotiable):** when an HTML/CSS design renders differently
from its expected result, treat it as an engine defect until disproven. Do not alter the
document or stylesheet merely to hide the discrepancy. Fix the generic DOM, style, layout,
or display-list behavior in the native core and add a regression test that captures the
original failure.

## License

MIT. See [LICENSE](LICENSE).
