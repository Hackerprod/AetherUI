# AetherUI .NET binding

The .NET binding is the **reference** managed integration for AetherUI's C ABI — the first consumer,
the one every ABI change is validated against before it ships, and today the only binding with a
working rendering backend. It does not reimplement HTML parsing, CSS, layout, or display-list
generation in managed code; every one of those responsibilities lives in `native/` and is reached
through P/Invoke. What lives here is strictly what's genuinely host-specific: how a .NET app boots the
engine, how it authors components, how it puts pixels on a Windows screen or a Linux software offscreen surface.

## Drawing modules

Windows hosting uses `WindowsDirect2DRenderModule`, which returns the existing
Direct2D/DirectWrite renderer without changing its public constructor or optional
incremental interface. `IRenderModule<TTarget>` in Core lets an embedding host
select a renderer explicitly.

`AetherUI.Linux` adds a Cairo/Pango software renderer for **offscreen/embedding**
use on Linux glibc x64. It does not create a Linux window or provide an input/event
loop. System Cairo/Pango libraries and fonts are required; Vortice and
System.Drawing are not Linux dependencies.

```csharp
using AetherUI.Linux;
using AetherUI.Primitives;
using AetherUI.Rendering;

var presenter = new MemoryFramePresenter();
using var renderer = new LinuxCairoRenderModule().Create(
    new LinuxSoftwareRenderTarget(presenter), new RenderViewport(320, 180, 1f));
renderer.Render(new DisplayList([
    new FillRoundedRectangleCommand(new RectF(16, 16, 80, 48),
        CornerRadii.Uniform(8), new ColorRgba(.2f, .5f, .9f, 1f))
]), ColorRgba.White);
// presenter.CurrentFrame owns a copy of the complete BGRA8 premultiplied frame.
```

See [drawing module contracts and conformance](docs/render-modules.md) for lifetime,
DIP/pixel units, dependencies, presentation failures, and platform-specific limits.

## Visibility and input

`visibility:hidden` retains layout and scroll offsets. The host rebuilds the complete
paint list when computed visibility changes, and resolves current styles before focus,
keyboard defaults, scrollbar interaction, and visibility-driven hover retargeting.
A visible descendant remains eligible even when its parent is hidden.

Hover changes caused by visibility settle within the same frame, with a maximum of
8 target applications. CSS without a stable target, such as `:hover { visibility:hidden }`,
falls back to an empty hover chain after that budget. The final styles are resolved,
the cursor comes from their current hit target, and no additional frame is requested
solely to repeat the oscillation. New pointer input or a later visibility invalidation
can reconsider the target. This does not change pointer-events-only hover scheduling.

## Minimal usage

```csharp
using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Hosting;

var options = new UiHostOptions
{
    Title = "AetherUI — Registro",
    Width = 900,
    Height = 680,
    ClearColor = ColorRgba.White,
};

return WindowsUiApplication.Run<RegistrationView>(options);
```

`RegistrationView` is a generated `.cshtml` component (compiled by `AetherUI.Generator` at build
time). `WindowsUiApplication.Run` owns the Win32 window, message loop, and input routing; internally
it drives one `UiDocumentHost`, which is the actual engine coordinator &mdash; see
[`RegistrationApp`](../../examples/dotnet/RegistrationApp/) for the complete, runnable version of the
snippet above.

## Modals

`AetherUI.Runtime.ModalController` is the reusable modal host for any `UiComponent`. It owns all the
modal chrome &mdash; fixed overlay, darken, optional backdrop blur, z-index, opacity/scale transitions,
backdrop and Escape close &mdash; so a page only supplies the card content:

```csharp
using AetherUI.Events;
using AetherUI.Runtime;

public sealed class SettingsPage : UiComponent
{
    private readonly ModalController _modal = new();

    public SettingsPage()
    {
        // Show/Close re-render through the owning component.
        _modal.Changed += () => StateHasChanged();
    }

    protected override void BuildRenderTree(MarkupWriter writer)
    {
        writer.WriteLiteral("<button id=\"open\"");
        writer.WriteEventAttribute("onclick", OpenSettings);
        writer.WriteLiteral(">Configuración</button>");

        // Renders the <style> + overlay whenever a modal is open.
        _modal.Render(writer);
    }

    private void OpenSettings(UiEvent e) => _modal.Show(w =>
    {
        w.WriteLiteral("<h2>Ajustes</h2><button id=\"done\"");
        w.WriteEventAttribute("onclick", CloseSettings);
        w.WriteLiteral(">Cerrar</button>");
    });

    private void CloseSettings(UiEvent e) => _modal.Close();
}
```

The engine does not execute page `<script>` tags; interactive behavior is authored through the
component/event model above, exactly like any other AetherUI interaction.

### Theming

The modal's visual tokens are CSS custom properties with defaults, so any page theme can override
them in its own stylesheet (the cascade resolves them per node; the fallback applies only when the
variable is undefined):

```css
:root {
  --aether-modal-overlay-bg: rgba(0, 0, 0, 0.8);
  --aether-modal-overlay-blur: 16px;
  --aether-modal-z-index: 100;
  --aether-modal-fade-duration: 0.3s;
  --aether-modal-card-bg: #111827;
  --aether-modal-card-color: #f9fafb;
  --aether-modal-card-radius: 15px;
  --aether-modal-card-padding: 30px;
  --aether-modal-card-max-width: 500px;
  --aether-modal-scale-duration: 0.3s;
  --aether-modal-card-shadow: 0 8px 32px rgba(0, 0, 0, 0.37);
}
```

The Validator includes an interactive demo ("Modal Interactivo (transitions)") showing the modal
fade + scale animation and the close paths (button, backdrop, Escape).

## Scope

| Project | Role |
|---|---|
| `src/AetherUI.Core.Native.Interop` | Loads `aether_core`, validates its ABI version/capabilities, declares every `[LibraryImport]`, and owns opaque-handle wrapping (`NativeDomDocument`, `NativeLayoutInterop`, `NativeDisplayListInterop`, `NativeStyleMap`, `StyleRuntimeInterop`). |
| `src/AetherUI.Core` | `UiDocumentHost` (the coordinator: parse → resolve styles → layout → build display list → dispatch input), the component/event model (`UiComponent`, `UiEvent`), and the `DomInterop`/`LayoutInterop`/`RenderingInterop` layer that projects native results into the managed `ElementNode`/`LayoutBox`/`DisplayList` types the rest of this binding consumes. |
| `src/AetherUI.Generator` | Incremental Roslyn source generator that compiles `.cshtml` views into `UiComponent` subclasses at build time — no runtime parsing of view markup. |
| `src/AetherUI.Windows` | Win32 window/message-loop hosting and the Direct2D/DirectWrite rendering backend. Also supplies the `ITextMeasurer` callback the native layout and display-list engines call back into during layout. |
| `tests/AetherUI.Tests` | A hand-rolled runner (not xUnit) exercising the binding end-to-end against the native pipeline — DOM identity, style cascade, layout geometry, event dispatch, and a checked-in dashboard fixture. |
| `tools/AetherUI.Validator` | Side-by-side visual regression tool: the same `.html` file rendered by this binding's Direct2D backend next to a WebView2/Chromium reference pane. |

`AetherUI.Windows` is deliberately a Windows/.NET rendering backend, not a portable component. Nothing
under `native/` depends on it, and a future Java or Go binding does not inherit it — each binding that
wants to paint needs its own backend, or consumes the display list headlessly.

### Why the projection layer (`DomInterop`/`LayoutInterop`/`RenderingInterop`) exists

The native core owns the single DOM parse, the style cascade, layout, and paint-command generation.
This binding still exposes `ElementNode`/`DocumentNode`/`LayoutBox`/`LayoutResult`/`DisplayList` as
plain managed types, because the component/event model (`UiDocumentHost`'s focus/hover/active
tracking, hit-testing, `.cshtml`-generated event handlers) needs stable managed object identity across
a render pass — the native ABI only hands back opaque `IntPtr` handles. `DomInterop` is the identity
map: one dictionary from native handle to managed `ElementNode` and back, built once per markup
rebuild and reused for every render until the next rebuild. `LayoutInterop`/`RenderingInterop` do the
equivalent projection for layout boxes and paint commands, decoding the native tagged-union ABI into
the existing `LayoutBox`/`PaintCommand` record types so `Direct2DRenderBackend` needed zero changes
when the engine moved from managed to native.

### ID lookup contract and managed parity

`DocumentNode.GetElementById` returns the first connected element in current preorder
for an exact, ordinal ID match, including duplicates after tree or attribute changes.
It returns null for empty/missing IDs and throws `ArgumentNullException` for null.
`DomIdOrderTests` checks object identity, duplicate ordering, moves, disconnection,
attribute changes, managed tree replacement and exact Unicode/case/space matching.
This is managed parity only: its separate `TryAdd` index does not call or validate the
C ABI lookup. Native ABI regression and opt-in JavaScript tests validate that function.
Run the existing test runner on its supported Windows/.NET 10 target; this managed
parity test does not make that runner portable or replace native ABI integration.

## Native runtime prerequisite

Build and install the matching native runtime **before** building this binding — it is not built for
you as part of `dotnet build`. The interop project resolves it from the installed tree, never from a
CMake build directory:

```text
artifacts/native/<rid>/bin/aether_core.dll          Windows
artifacts/native/<rid>/lib/libaether_core.so         Linux
artifacts/native/<rid>/lib/libaether_core.dylib      macOS
```

For a Windows x64 development build, from the repository root:

```powershell
cmake -S native -B artifacts/build/native/win-x64 -A x64 `
  -DAETHERUI_BUILD_TESTS=ON `
  -DCMAKE_INSTALL_PREFIX=artifacts/native/win-x64
cmake --build artifacts/build/native/win-x64 --config Release --parallel
ctest --test-dir artifacts/build/native/win-x64 -C Release --output-on-failure
cmake --install artifacts/build/native/win-x64 --config Release
```

`AetherNativeRuntimeIdentifier` selects which installed asset a .NET build consumes. It resolves, in
order: an explicit `-p:AetherNativeRuntimeIdentifier=<rid>`, then `$(RuntimeIdentifier)` if the .NET
project sets one, then the SDK's own default RID, then falls back to `win-x64`.

```powershell
dotnet build .\bindings\dotnet\AetherUI.DotNet.sln -c Release `
  -p:AetherNativeRuntimeIdentifier=win-x64
```

The build fails fast — with the resolved path in the error — if the selected installed runtime is
absent. There is no silent fallback and no copy from an internal build directory; if you find yourself
manually copying `aether_core.dll` between `bin/` folders, something upstream of that (the CMake
install step, or `AetherNativeRuntimeIdentifier`) is misconfigured.

## Native attribute identity scope

The native parser/C ABI and CSS attribute selectors use ASCII-only lowercase
names, with first-source-wins parser duplicates and canonical enumeration. Native
`ID` and `id` address the same attribute; ID values still match exactly.
`NativeDomDocument.SetAttribute` delegates to that native contract. Explicit style
resolution remains required after attribute mutation.

The managed `ElementNode` model and its public `Attributes` view still use
`OrdinalIgnoreCase` and `ToLowerInvariant`. Those Unicode rules can conflate
non-ASCII names that native code keeps distinct, and `DomInterop` projects native
attributes through the managed setter. This native change does not establish
managed parity or broader bidirectional attribute synchronization. The native
regression suite is not evidence that .NET integration or rendering tests ran.

## ABI and lifetime rules

`AetherUI.Core.Native.Interop` validates `aether_abi_version()` and the capability bits the managed
pipeline requires (including input-hit-test bit 5 and layout-transition-context bit 6, required mask `0x7F`) before any native handle is created — a mismatch fails at startup, not partway
through a render. ABI 0.34 and structure sizes remain unchanged; an older 0.34
runtime reporting `0x1F` fails the capability check before the new symbol is called.
Scrollbar presses use the native input-part query with current styles and offsets.
A selected thumb/track consumes through release, even at a limit or when its native
owner cannot be projected; corner retains normal DOM routing without capturing.
Display-list decoding (`NativeDisplayListInterop`) sizes its native buffer from
`aether_paint_command_size()`; it never assumes a hard-coded struct layout, so a native ABI update that
grows the paint-command union cannot silently corrupt a managed read past the end of a stale buffer.

Native handles have exactly one owner. Managed wrappers (`NativeDomDocument`, `NativeLayoutResult`,
`NativeStyleMap`, `StyleRuntimeInterop`) dispose their own native resource and only that resource;
borrowed views (nodes, layout boxes) are never independently freed, and reparsing a document
invalidates every wrapper built against the previous native tree — `UiDocumentHost` drops its whole
`DomInterop` identity map on every markup rebuild for exactly this reason. Text and gradient-stop data
returned inside a display-list command are copied into managed memory before the command (or the list
that owns it) is released.

Callbacks from native code into .NET (`ITextMeasurer`, scroll-offset queries) are synchronous. Their
delegates and backing `GCHandle` are kept alive for the full registration lifetime of the native object
that holds them — freed only in that object's `Dispose()` — and every callback body is wrapped so a
managed exception is caught and converted to a safe fallback value instead of unwinding across the C++
call stack that invoked it.

## Build and test

```powershell
dotnet restore .\bindings\dotnet\AetherUI.DotNet.sln `
  -p:AetherNativeRuntimeIdentifier=win-x64
dotnet build .\bindings\dotnet\AetherUI.DotNet.sln -c Release --no-restore `
  -p:AetherNativeRuntimeIdentifier=win-x64
dotnet run --project .\bindings\dotnet\tests\AetherUI.Tests\AetherUI.Tests.csproj `
  -c Release --no-build -p:AetherNativeRuntimeIdentifier=win-x64
```

The test suite includes a checked-in native-pipeline fixture (a full HTML/CSS dashboard) asserted
against fixed pixel positions and colors — this is the regression bar for any future native-side
change, not just this binding's own code. The Validator is the visual-parity tool for the Windows
rendering backend specifically; WebView2 is used only as a side-by-side comparison surface, never as an
engine runtime dependency.

[`scripts/verify.ps1`](scripts/verify.ps1) runs restore, build, test, and pack in one pass, and — after
packing — opens the produced `AetherUI.Core.Native.Interop` `.nupkg` to assert it actually contains the
expected `runtimes/<rid>/native/<lib>` entry, catching a packaging regression before it ships rather
than after a downstream consumer fails to load the library. [`scripts/publish-sample.ps1`](scripts/publish-sample.ps1)
publishes `RegistrationApp` for a given RID as an end-to-end "does a real app run from this" check.

## Troubleshooting

- **`dotnet build` fails with "AetherUI native runtime was not found at ..."** — the native runtime
  for the resolved RID hasn't been installed. Run the CMake install step above for that RID before
  building, or pass the RID you actually installed via `-p:AetherNativeRuntimeIdentifier=<rid>`.
- **Changes to `native/` don't show up after rebuilding the binding** — `dotnet build` does not invoke
  CMake. Rebuild and reinstall the native target first (`cmake --build ... && cmake --install ...`),
  *then* rebuild the .NET solution so it picks up the freshly installed binary.
- **`cmake` is not recognized** — on a Visual Studio Build Tools–only machine, `cmake.exe` ships inside
  the VS install and is not automatically added to `PATH`. It's typically under
  `<VS install>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`; either add that to
  `PATH` for the session or invoke it by full path.
- **A native fix doesn't seem to apply even after reinstalling** — check the loaded library's mtime,
  not just the interop project's `bin/` output. A stale `aether_core.dll` sitting in a downstream
  project's own `bin/` folder from a previous build can outlive the source install if something copied
  it there by hand instead of through the RID-resolved path; delete the stale copy so the next build
  re-resolves it from `artifacts/native/<rid>/`.

## Packages

The binding produces four NuGet packages:

- `AetherUI.Core.Native.Interop`
- `AetherUI.Core`
- `AetherUI.Generator`
- `AetherUI.Windows`

The interop package carries whichever native assets were actually built and installed under
`runtimes/<rid>/native/` at pack time. Publish a RID only once its native artifact has been built and
tested for that target — the packaging step does not fabricate a RID folder for a platform nobody
built.

## Examples and tools

- [RegistrationApp](../../examples/dotnet/RegistrationApp/) — the minimal single-view app shown above.
- [ExamplesGallery](../../examples/dotnet/ExamplesGallery/) — a multi-view gallery exercising a broader
  slice of the supported CSS profile.
- [AetherUI.Validator](tools/AetherUI.Validator/) — native-vs-Chromium visual regression tool; point it
  at any `.html` file and it renders both side by side, reloading on save.
