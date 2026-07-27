[CmdletBinding()]
param(
    [switch]$SkipPack,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('win-x64', 'win-arm64', 'win-x86')]
    [string]$RuntimeIdentifier = 'win-x64'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$runningOnWindows = [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [Runtime.InteropServices.OSPlatform]::Windows)

if (-not $runningOnWindows) {
    throw 'The complete .NET reference integration requires Windows (Win32/Direct2D).'
}
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw '.NET SDK is not installed or dotnet is not available in PATH.'
}

$bindingRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$repositoryRoot = (Resolve-Path (Join-Path $bindingRoot '../..')).Path
$solution = Join-Path $bindingRoot 'AetherUI.DotNet.sln'
$testProject = Join-Path $bindingRoot 'tests/AetherUI.Tests/AetherUI.Tests.csproj'
$windowsRegressionProject = Join-Path $bindingRoot 'tests/AetherUI.Windows.RegressionTests/AetherUI.Windows.RegressionTests.csproj'
$packages = Join-Path $repositoryRoot 'artifacts/packages'
$projectsToPack = @(
    (Join-Path $bindingRoot 'src/AetherUI.Core.Native.Interop/AetherUI.Core.Native.Interop.csproj'),
    (Join-Path $bindingRoot 'src/AetherUI.Core/AetherUI.Core.csproj'),
    (Join-Path $bindingRoot 'src/AetherUI.Windows/AetherUI.Windows.csproj'),
    (Join-Path $bindingRoot 'src/AetherUI.Generator/AetherUI.Generator.csproj')
)

Push-Location $repositoryRoot
try {
    Write-Host '== .NET SDK =='
    dotnet --version

    Write-Host '== Restore .NET binding =='
    dotnet restore $solution -p:AetherNativeRuntimeIdentifier=$RuntimeIdentifier

    Write-Host "== Build .NET binding ($Configuration) =="
    dotnet build $solution `
        -c $Configuration `
        --no-restore `
        -p:AetherNativeRuntimeIdentifier=$RuntimeIdentifier

    Write-Host '== .NET integration tests =='
    dotnet run `
        --project $testProject `
        -c $Configuration `
        --no-build `
        -p:AetherNativeRuntimeIdentifier=$RuntimeIdentifier

    Write-Host '== Direct2D WARP pixel regression =='
    dotnet run `
        --project $windowsRegressionProject `
        -c $Configuration `
        -p:AetherNativeRuntimeIdentifier=$RuntimeIdentifier

    if (-not $SkipPack) {
        Write-Host '== NuGet packages =='
        if (Test-Path -LiteralPath $packages) {
            $resolvedPackages = (Resolve-Path -LiteralPath $packages).Path
            if (-not $resolvedPackages.StartsWith($repositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to replace package output outside the repository: $resolvedPackages"
            }
            Remove-Item -LiteralPath $resolvedPackages -Recurse -Force
        }
        New-Item -Path $packages -ItemType Directory -Force | Out-Null

        foreach ($project in $projectsToPack) {
            dotnet pack $project `
                -c $Configuration `
                --no-build `
                -o $packages `
                -p:AetherNativeRuntimeIdentifier=$RuntimeIdentifier
        }

        $nativePackage = Get-ChildItem -LiteralPath $packages -File -Filter 'AetherUI.Core.Native.Interop.*.nupkg' |
            Where-Object { $_.Name -notlike '*.symbols.nupkg' } |
            Select-Object -First 1
        if (-not $nativePackage) {
            throw 'The AetherUI.Core.Native.Interop package was not produced.'
        }

        $nativeName = if ($RuntimeIdentifier.StartsWith('win-')) {
            'aether_core.dll'
        }
        elseif ($RuntimeIdentifier.StartsWith('linux-')) {
            'libaether_core.so'
        }
        else {
            'libaether_core.dylib'
        }
        $expectedEntry = "runtimes/$RuntimeIdentifier/native/$nativeName"
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($nativePackage.FullName)
        try {
            if ($archive.Entries.FullName -notcontains $expectedEntry) {
                throw "Native package is missing '$expectedEntry'."
            }
        }
        finally {
            $archive.Dispose()
        }

        Get-ChildItem -LiteralPath $packages -File | Sort-Object Name | ForEach-Object {
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            Write-Host "$hash  $($_.Name)"
        }
    }

    Write-Host '.NET binding verification completed.'
}
finally {
    Pop-Location
}
