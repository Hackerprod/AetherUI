# AetherUI

**AetherUI 0.1.0-alpha.1** is a native desktop UI engine for .NET that uses HTML as structure, CSS as the visual language, and compiled C# as view logic. The included backend creates its own Win32 window and renders a retained display list with Direct2D and DirectWrite. It does not embed Chromium, WebView2, WPF, WinForms, or a JavaScript engine.

> **Status: engineering alpha.** The architecture and samples are usable for experimentation, extension, and validating the approach. This is not yet a full browser replacement and should not be deployed as a general-purpose production framework before the gaps listed in [Limitations & Roadmap](docs/roadmap.md) are closed.

## What's included

- Incremental Roslyn compiler for `.cshtml` views.
- C# and markup in the same file via `@code`, `@Expression`, and `@(expression)`.
- Dynamic attributes and event bindings compiled to C# delegates — no reflection, no runtime code evaluation.
- A first-party HTML parser and DOM.
- A first-party CSS parser: selectors, cascade, and computed styles.
- Block, inline, `inline-block`, and a one-dimensional Flexbox layout implementation.
- Retained display list with hit testing.
- Basic forms: focus, keyboard, mouse, `required`, `maxlength`, and event bubbling.
- Win32 host with per-monitor DPI awareness.
- Direct2D GPU backend and DirectWrite text via Vortice.Windows.
- Demo applications and an executable test suite with no external test framework.
- NuGet package layout for `AetherUI.Core`, `AetherUI.Windows`, and `AetherUI.Generator`.

## View example

```cshtml
@namespace MyApp.Views
@class RegistrationView

@code {
    private string Name { get; set; } = string.Empty;
    private string Message { get; set; } = "Type your name.";

    private void OnInput(UiEvent e)
    {
        Name = e.Value ?? string.Empty;
        Message = $"Typing: {Name}";
        StateHasChanged();
    }

    private void Submit(UiEvent e)
    {
        e.PreventDefault();
        Message = $"Registration submitted for {Name}.";
        StateHasChanged();
    }
}

<style>
  body {
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    width: 100%;
    height: 100vh;
    margin: 0;
  }
</style>

<body>
  <form onsubmit="@Submit">
    <label for="name">Name</label>
    <input id="name"
           value="@Name"
           oninput="@OnInput"
           required>
    <button type="submit">Submit</button>
  </form>
  <p>@Message</p>
</body>
```

At build time, the generator turns the view into a partial C# class. Literals are written directly, dynamic values are HTML-encoded, and event handlers are registered as compiled C# delegates through internal tokens. The runtime only ever receives already-produced markup — it never compiles or interprets C# on its own.

## Samples

- **[RegistrationApp](samples/RegistrationApp)** — minimal form with live input feedback and validation.
- **[DashboardApp](samples/DashboardApp)** — interactive control panel demonstrating flex layout, toggle switches, a progress indicator, dark-mode theming, and a bound greeting form.

## Requirements

- Windows 10 version 2004 or later to run the included host.
- .NET SDK 10.0.302 or a compatible later feature band.
- A GPU/driver combination compatible with Direct2D. Windows falls back automatically depending on the available graphics stack.

## Build and run

From the repository root:

```powershell
dotnet restore AetherUI.sln
dotnet build AetherUI.sln -c Release
dotnet run --project tests/AetherUI.Tests/AetherUI.Tests.csproj -c Release --no-build
dotnet run --project samples/RegistrationApp/RegistrationApp.csproj -c Release --no-build
dotnet run --project samples/DashboardApp/DashboardApp.csproj -c Release --no-build
```

The full verification pipeline can also be run directly:

```powershell
pwsh ./scripts/verify.ps1
```

## Consuming it from a project

While the code is consumed directly from this solution:

```xml
<ItemGroup>
  <ProjectReference Include="..\AetherUI\src\AetherUI.Core\AetherUI.Core.csproj" />
  <ProjectReference Include="..\AetherUI\src\AetherUI.Windows\AetherUI.Windows.csproj" />
  <ProjectReference Include="..\AetherUI\src\AetherUI.Generator\AetherUI.Generator.csproj"
                    OutputItemType="Analyzer"
                    ReferenceOutputAssembly="false" />
  <AdditionalFiles Include="Views\**\*.cshtml" />
</ItemGroup>
```

Once packages are published, the intended reference shape is:

```xml
<ItemGroup>
  <PackageReference Include="AetherUI.Core" Version="0.1.0-alpha.1" />
  <PackageReference Include="AetherUI.Windows" Version="0.1.0-alpha.1" />
  <PackageReference Include="AetherUI.Generator"
                    Version="0.1.0-alpha.1"
                    OutputItemType="Analyzer"
                    ReferenceOutputAssembly="false" />
</ItemGroup>
```

The generator package ships a `buildTransitive` target that automatically wires up `.cshtml` files as `AdditionalFiles`.

## Pipeline

```text
.cshtml
  │
  ├─ AetherUI.Generator ──► C# generated at build time
  │                              │
  │                              ▼
  └──────────────────────► Safe MarkupWriter
                                 │
                                 ▼
                         HTML parser + DOM
                                 │
                                 ▼
                     CSS cascade + computed style
                                 │
                                 ▼
                       Layout tree + hit testing
                                 │
                                 ▼
                         Retained display list
                                 │
                                 ▼
                   Win32 + Direct2D + DirectWrite
```

The GPU handles rasterization and composition. Parsing, cascade resolution, layout, and event routing all run on the CPU, as expected of a retained-mode UI engine.

## Documentation

- [Architecture](docs/architecture.md)
- [`.cshtml` template syntax](docs/template-syntax.md)
- [Supported HTML/CSS/events profile](docs/supported-profile.md)
- [Build, packaging, and release](docs/build-and-release.md)
- [Limitations & roadmap](docs/roadmap.md)
- [Package validation status](VALIDATION.md)

## Security

AetherUI does not browse the internet, does not execute JavaScript, does not load remote resources, and does not interpret C# at runtime. Dynamic values produced by views are HTML-encoded. `WriteLiteral` is a low-level API and must only ever receive trusted markup generated or controlled by the developer.

See [SECURITY.md](SECURITY.md) for the threat model and reporting process.

## License

MIT. See [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
