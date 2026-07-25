# Build, empaquetado y publicación

## SDK fijado

`global.json` solicita .NET SDK `10.0.302` y permite avanzar a un feature band posterior compatible. Las versiones de paquetes están centralizadas en `Directory.Packages.props`.

## Verificación local

```powershell
pwsh ./scripts/verify.ps1
```

El script:

1. comprueba que existe `dotnet` y que el sistema es Windows;
2. restaura la solución;
3. compila Release;
4. ejecuta `AetherUI.Tests`;
5. crea paquetes NuGet en `artifacts/packages`.

El host Windows debe probarse en Windows porque el render target requiere HWND y Direct2D.

## Publicar la demo

Framework-dependent, single-file, win-x64:

```powershell
pwsh ./scripts/publish-sample.ps1 -RuntimeIdentifier win-x64
```

La salida se coloca en:

```text
artifacts/publish/RegistrationApp/win-x64/
```

La alpha mantiene `PublishAot=false`. Native AOT deberá activarse únicamente después de verificar todas las rutas COM/PInvoke y el generador en una matriz de publicación dedicada.

## Paquetes

### `AetherUI.Core`

Biblioteca multiplataforma con DOM/CSS/layout/runtime.

### `AetherUI.Windows`

Host Windows; depende de `AetherUI.Core` y `Vortice.Direct2D1`.

### `AetherUI.Generator`

Se empaca en `analyzers/dotnet/cs`, sin assembly de runtime, e incluye props `buildTransitive` para agregar vistas `.cshtml`.

Comando manual:

```powershell
dotnet pack src/AetherUI.Core/AetherUI.Core.csproj -c Release -o artifacts/packages
dotnet pack src/AetherUI.Windows/AetherUI.Windows.csproj -c Release -o artifacts/packages
dotnet pack src/AetherUI.Generator/AetherUI.Generator.csproj -c Release -o artifacts/packages
```

## Checklist de release

- build Release sin errores;
- pruebas 100% aprobadas;
- demo revisada a 100%, 150% y 200% DPI;
- paquete del generador contiene DLL bajo `analyzers/dotnet/cs`;
- paquete del generador contiene `buildTransitive/AetherUI.Generator.props`;
- dependencias NuGet auditadas;
- changelog actualizado;
- versión SemVer actualizada;
- SHA-256 del ZIP y paquetes generado;
- limitaciones conocidas revisadas.
