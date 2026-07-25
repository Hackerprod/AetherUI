[CmdletBinding()]
param(
    [switch]$SkipPack
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$artifacts = Join-Path $root 'artifacts'
$packages = Join-Path $artifacts 'packages'

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw '.NET SDK no está instalado o dotnet no está disponible en PATH.'
}

if (-not $IsWindows) {
    throw 'La verificación completa requiere Windows porque AetherUI.Windows compila y ejecuta contra Win32/Direct2D.'
}

Push-Location $root
try {
    Write-Host '== SDK =='
    dotnet --version

    Write-Host '== Restore =='
    dotnet restore AetherUI.sln

    Write-Host '== Build Release =='
    dotnet build AetherUI.sln -c Release --no-restore

    Write-Host '== Tests =='
    dotnet run --project tests/AetherUI.Tests/AetherUI.Tests.csproj -c Release --no-build

    if (-not $SkipPack) {
        Write-Host '== NuGet packages =='
        if (Test-Path $packages) {
            Remove-Item $packages -Recurse -Force
        }
        New-Item $packages -ItemType Directory -Force | Out-Null

        dotnet pack src/AetherUI.Core/AetherUI.Core.csproj -c Release --no-build -o $packages
        dotnet pack src/AetherUI.Windows/AetherUI.Windows.csproj -c Release --no-build -o $packages
        dotnet pack src/AetherUI.Generator/AetherUI.Generator.csproj -c Release --no-build -o $packages

        Get-ChildItem $packages -File | Sort-Object Name | ForEach-Object {
            $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            Write-Host "$hash  $($_.Name)"
        }
    }

    Write-Host 'Verificación completada.'
}
finally {
    Pop-Location
}
