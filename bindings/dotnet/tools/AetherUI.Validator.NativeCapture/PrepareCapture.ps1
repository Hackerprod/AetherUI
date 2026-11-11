# No GUI build or execution. Real native validation remains enabled. No retries.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference=$false
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
Set-Location $root
$evidence='artifacts/validator-native-capture/evidence'
$logs='artifacts/validator-native-capture/process-logs'
$nativeBuild='artifacts/build/validator-native-capture/win-x64'
$nativeInstall='artifacts/native/win-x64'
$project='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/AetherUI.Validator.NativeCapture.csproj'
$bin='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/bin/Release/net10.0-windows10.0.19041.0/win-x64'
$expectedFixture='eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
if (Test-Path $evidence) { throw 'Preserve previous evidence; no overwrite or retry.' }
New-Item -ItemType Directory -Path $evidence,$logs | Out-Null
$script:record=[ordered]@{schema='aether-native-build-v1';status='running';stage='source';commands=@();native=$null;assemblies=@();errorType=$null}
function Write-Json([string]$Path,$Value) { $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $Path -Encoding utf8 }
function Save { Write-Json "$evidence/build.json" $script:record }
function Run([string]$File,[string[]]$Arguments) {
    $command=[ordered]@{file=$File;arguments=@($Arguments | ForEach-Object {$_.Replace($root,'<workspace>')});exitCode=$null}
    $script:record.commands += $command
    $log="$logs/build-$($script:record.commands.Count).txt"
    [IO.File]::Open((Join-Path $root $log),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write).Dispose()
    & $File @Arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
    $code=$LASTEXITCODE
    $command.exitCode=$code; Save
    if ($code -ne 0) { throw "Build step failed: $File exit $code." }
    return @(Get-Content -LiteralPath $log)
}
function Identity([string]$Path) {
    $item=Get-Item -LiteralPath $Path
    if ($item.Length -le 0) { throw 'Required artifact is empty.' }
    $relative=[IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/')
    if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith('../')) { throw 'Identity escaped checkout.' }
    return [ordered]@{path=$relative;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function PeIdentity([string]$Path) {
    $identity=Identity $Path
    $stream=[IO.File]::OpenRead((Join-Path $root $Path)); $reader=[IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'Missing MZ header.' }
        $stream.Position=0x3c; $offset=$reader.ReadInt32()
        if ($offset -lt 0x40 -or $offset -gt $stream.Length-6) { throw 'Invalid PE offset.' }
        $stream.Position=$offset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'Expected AMD64 PE.' }
        $identity.machine='AMD64'
    } finally { $reader.Dispose() }
    return $identity
}
Save
try {
    $commit=@(Run git @('rev-parse','HEAD'))[-1].Trim()
    $tree=@(Run git @('rev-parse','HEAD^{tree}'))[-1].Trim()
    $nativeTree=@(Run git @('rev-parse','HEAD:native'))[-1].Trim()
    if ($commit -ne $env:GITHUB_SHA) { throw 'Checkout is not the exact push SHA.' }
    $fixture=Identity 'bindings/dotnet/tools/AetherUI.Validator/Pages/flexbox_runtime_validation.html'
    if ($fixture.sha256 -ne $expectedFixture -or $fixture.bytes -ne 474495) { throw 'Fixture identity mismatch.' }
    $base='99c336187bd264dade2f177c32f420172b3cf777'
    $allowedProduct=@('bindings/dotnet/src/AetherUI.Windows/Properties/AssemblyInfo.cs',
        'bindings/dotnet/src/AetherUI.Windows/Graphics/Direct2DRenderBackend.cs',
        'bindings/dotnet/src/AetherUI.Windows/Graphics/Direct2DRenderBackend.Incremental.cs',
        'bindings/dotnet/src/AetherUI.Windows/Graphics/Direct2DRenderBackend.CaptureDiagnostics.cs')
    $changed=@(Run git @('diff','--name-only',$base,'HEAD','--'))
    foreach ($path in $changed) {
        if ($path -notin $allowedProduct -and $path -ne '.github/workflows/validator-native-capture.yml' -and
            -not $path.StartsWith('bindings/dotnet/tools/AetherUI.Validator.NativeCapture/')) { throw 'Change outside native capture allowlist.' }
    }
    $null=Run git @('diff','--exit-code','HEAD','--')
    Write-Json "$evidence/source.json" ([ordered]@{commit=$commit;tree=$tree;nativeTree=$nativeTree;base=$base;fixture=$fixture;changed=$changed})
    $script:record.stage='toolchain'; Save
    $version=@(Run dotnet @('--version'))[-1].Trim()
    if ($version -ne '10.0.302' -or -not $IsWindows -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') { throw 'Official SDK 10.0.302 on Windows x64 required.' }
    $script:record.sdk=$version
    $script:record.stage='native-build'; Save
    $null=Run cmake @('-S','native','-B',$nativeBuild,'-A','x64','-DAETHERUI_BUILD_TESTS=OFF',"-DCMAKE_INSTALL_PREFIX=$(Join-Path $root $nativeInstall)")
    if (-not (Select-String -LiteralPath "$nativeBuild/CMakeCache.txt" -Pattern '^AETHERUI_BUILD_TESTS:BOOL=OFF$' -Quiet)) { throw 'Native tests must remain disabled.' }
    $null=Run cmake @('--build',$nativeBuild,'--config','Release','--target','aether_core','--parallel','3')
    $null=Run cmake @('--install',$nativeBuild,'--config','Release')
    $script:record.native=PeIdentity "$nativeInstall/bin/aether_core.dll"
    $script:record.stage='managed-build'; Save
    $common=@('-r','win-x64','-p:AetherNativeRuntimeIdentifier=win-x64','-p:SelfContained=false','-p:EnableWindowsTargeting=true','-p:UseAppHost=true')
    $null=Run dotnet (@('restore',$project)+$common)
    $null=Run dotnet (@('build',$project,'-c','Release','--no-restore')+$common)
    $script:record.assemblies=@(foreach ($file in @('AetherUI.Validator.NativeCapture.dll','AetherUI.Windows.dll','AetherUI.Core.dll','AetherUI.Core.Native.Interop.dll')) { Identity "$bin/$file" })
    $script:record.executable=PeIdentity "$bin/AetherUI.Validator.NativeCapture.exe"
    $copied=PeIdentity "$bin/aether_core.dll"
    if ($copied.sha256 -ne $script:record.native.sha256) { throw 'Capture output native DLL differs from freshly built native library.' }
    # Identify the actual assembly used by this console, without assuming the referenced
    # project's intermediate RID layout. LoadedIdentity verifies this same output at runtime.
    $script:record.interop=Identity "$bin/AetherUI.Core.Native.Interop.dll"
    $script:record.stage='dependency-and-source-guard'; Save
    $forbidden='(?i)WebView2|Playwright|Selenium|Puppeteer|CefSharp|CefGlue|Chromium'
    $assets=@()
    foreach ($directory in @('bindings/dotnet/tools/AetherUI.Validator.NativeCapture','bindings/dotnet/src/AetherUI.Windows','bindings/dotnet/src/AetherUI.Core','bindings/dotnet/src/AetherUI.Core.Native.Interop')) {
        $path="$directory/obj/project.assets.json"
        $json=Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -AsHashtable
        # Central package versions may mention unused WebView2. Inspect resolved closure only.
        $resolved=@($json.libraries.Keys)
        if (@($resolved | Where-Object {$_ -match $forbidden}).Count -ne 0) { throw 'Forbidden browser dependency in resolved graph.' }
        $assets += [ordered]@{project=$directory;assets=(Identity $path);resolvedLibraries=$resolved}
    }
    $depsPath="$bin/AetherUI.Validator.NativeCapture.deps.json"
    $deps=Get-Content -Raw -LiteralPath $depsPath | ConvertFrom-Json -AsHashtable
    if (@($deps.libraries.Keys | Where-Object {$_ -match $forbidden}).Count -ne 0) { throw 'Forbidden browser runtime dependency.' }
    [xml]$proj=Get-Content -Raw -LiteralPath $project
    $references=@($proj.SelectNodes('//ProjectReference'))
    if ($references.Count -ne 1 -or $references[0].Include -ne '../../src/AetherUI.Windows/AetherUI.Windows.csproj') { throw 'Only the Windows ProjectReference is allowed.' }
    if ($proj.SelectNodes('//UseWindowsForms').Count -ne 0) { throw 'Windows Forms is forbidden.' }
    $links=@($proj.SelectNodes('//Compile[@Include]') | ForEach-Object {$_.Include})
    $expectedLinks=@('../AetherUI.Validator/RawHtmlFileComponent.cs','../AetherUI.Validator/PerfReporter.cs','../AetherUI.Validator/ValidationReport.cs')
    if ($links.Count -ne 3 -or @($links | Where-Object {$_ -notin $expectedLinks}).Count -ne 0) { throw 'Only the three reviewed Validator sources may be linked.' }
    # Scan actual invocation shapes, not harmless denied-name literals in the guard tests.
    foreach ($source in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cs') {
        $text=Get-Content -Raw -LiteralPath $source.FullName
        if ($text -match '\bProcess\s*\.\s*Start\s*\(|\bShellExecute\w*\s*\(|\bEnsureCoreWebView2\w*\s*\(|new\s+ValidatorForm\s*\(') { throw 'Forbidden process or GUI invocation in console source.' }
    }
    Write-Json "$evidence/dependencies.json" ([ordered]@{status='passed';resolved=$assets;deps=(Identity $depsPath);project=(Identity $project);sourceInvocationGuard='passed';ilGuard='required at executable startup';browser='not run'})
    $script:record.status='passed'; $script:record.stage='complete'
} catch {
    $script:record.status='failed'; $script:record.errorType=$_.Exception.GetType().FullName
    throw
} finally { Save }
