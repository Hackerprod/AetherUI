[CmdletBinding()]
param(
    [ValidateSet('win-x64', 'win-arm64', 'win-x86')]
    [string]$RuntimeIdentifier = 'win-x64',

    [switch]$SelfContained
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$runningOnWindows = [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [Runtime.InteropServices.OSPlatform]::Windows)

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw '.NET SDK no está instalado o dotnet no está disponible en PATH.'
}

if (-not $runningOnWindows) {
    throw 'La publicación validada de la demo requiere Windows.'
}

$bindingRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root = (Resolve-Path (Join-Path $bindingRoot '../..')).Path
$output = Join-Path $root "artifacts/publish/RegistrationApp/$RuntimeIdentifier"
$project = Join-Path $root 'examples/dotnet/RegistrationApp/RegistrationApp.csproj'

if (Test-Path $output) {
    Remove-Item $output -Recurse -Force
}
New-Item $output -ItemType Directory -Force | Out-Null

$args = @(
    'publish', $project,
    '-c', 'Release',
    '-r', $RuntimeIdentifier,
    '-o', $output,
    '-p:PublishSingleFile=true',
    '-p:PublishAot=false',
    "--self-contained=$($SelfContained.IsPresent.ToString().ToLowerInvariant())"
)

& dotnet @args
if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish terminó con código $LASTEXITCODE."
}

Write-Host "Demo publicada en: $output"
