# Baseline preparation only. Fail closed; no retries, alternate engines or UI execution.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false # Native exits are recorded immediately below.

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
Set-Location $root
$evidence = 'artifacts/validator-metrics/evidence'
$logs = 'artifacts/validator-metrics/process-logs' # Actions logs only; never uploaded.
$nativeBuild = 'artifacts/build/validator-metrics/win-x64'
$nativeInstall = 'artifacts/native/win-x64'
$probeOutput = 'artifacts/validator-metrics/preflight-bin'
$baseline = '1229a9544daff36ae5d4c5b53767a232db24dfe0'
$baselineTree = 'f82c52cd615afc7540fe70ae4e2c495055037087'
$core = 'bindings/dotnet/src/AetherUI.Core/AetherUI.Core.csproj'
$interop = 'bindings/dotnet/src/AetherUI.Core.Native.Interop/AetherUI.Core.Native.Interop.csproj'
$windows = 'bindings/dotnet/src/AetherUI.Windows/AetherUI.Windows.csproj'
$validator = 'bindings/dotnet/tools/AetherUI.Validator/AetherUI.Validator.csproj'
$probe = 'bindings/dotnet/tests/AetherUI.Metrics.Preflight/AetherUI.Metrics.Preflight.csproj'
$common = @('-r', 'win-x64', '-p:AetherNativeRuntimeIdentifier=win-x64', '-p:SelfContained=false')
$validatorOptions = $common + @('-p:EnableWindowsTargeting=true', '-p:UseAppHost=true')

# Do not overwrite an earlier attempt's evidence, even when run outside fresh Actions checkouts.
if (Test-Path $evidence) { throw 'Preparation evidence already exists; preserve the previous attempt.' }
New-Item -ItemType Directory -Path $evidence, $logs | Out-Null
$script:manifest = [ordered]@{
    schema = 'managed-preparation-v1'; status = 'incomplete'
    baselineCommit = $baseline; baselineTree = $baselineTree
    source = $null; toolchain = $null; build = [ordered]@{}
    stageFiles = @(); scope = 'Baseline preparation, not metrics implementation or a performance verdict.'
}
$script:dependencies = [ordered]@{ schema = 'managed-preparation-dependencies-v1'; restores = @() }
$script:stage = $null

function Write-Json([string] $Path, $Value) {
    $Value | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $Path -Encoding utf8
}
function Save-Manifest { Write-Json "$evidence/manifest.json" $script:manifest }
function Get-Identity([string] $Path) {
    $item = Get-Item -LiteralPath $Path
    if ($item.Length -le 0) { throw "Empty required file: $Path" }
    $relative = [IO.Path]::GetRelativePath($root, $item.FullName).Replace('\', '/')
    if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith('../')) {
        throw 'Evidence identity must stay inside the checkout.'
    }
    return [ordered]@{ path = $relative; bytes = $item.Length; sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
function Save-Stage { Write-Json "$evidence/stage-$($script:stage.name).json" $script:stage }
function Invoke-Stage([string] $Name, [scriptblock] $Action) {
    $script:stage = [ordered]@{ name = $Name; status = 'running'; commands = @(); details = $null }
    $script:manifest.stageFiles += "stage-$Name.json"
    Save-Manifest
    Save-Stage
    try {
        & $Action
        $script:stage.status = 'passed'
    } catch {
        $script:stage.status = 'failed'
        # No arbitrary exception text, local paths or environment data in artifacts.
        $script:stage.errorType = $_.Exception.GetType().FullName
        throw
    } finally {
        Save-Stage
        Save-Manifest
    }
}
function Invoke-Native([string] $File, [string[]] $Arguments) {
    $record = [ordered]@{ file = $File; arguments = @($Arguments | ForEach-Object { $_.Replace($root, '<workspace>') }); exitCode = $null }
    $script:stage.commands += $record
    Save-Stage
    $log = "$logs/$($script:stage.name)-$($script:stage.commands.Count).txt"
    # Tee-Object may never open its file when a successful command emits no lines.
    # Create an empty log first; CreateNew also refuses to overwrite an earlier log.
    [IO.File]::Open((Join-Path $root $log), [IO.FileMode]::CreateNew, [IO.FileAccess]::Write).Dispose()
    & $File @Arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
    $code = $LASTEXITCODE
    $record.exitCode = $code
    Save-Stage
    if ($code -ne 0) { throw "Stage $($script:stage.name): $File exited $code." }
    return @(Get-Content -LiteralPath $log)
}
function Save-Assets([string] $Name, [string[]] $Projects) {
    $snapshots = foreach ($project in $Projects) {
        $path = Join-Path (Split-Path $project) 'obj/project.assets.json'
        $assets = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -AsHashtable
        $libraries = foreach ($key in ($assets.libraries.Keys | Sort-Object)) {
            $entry = $assets.libraries[$key]
            [ordered]@{ identity = $key; type = $entry.type; sha512 = $entry['sha512'] }
        }
        $frameworks = foreach ($key in ($assets.project.frameworks.Keys | Sort-Object)) {
            $framework = $assets.project.frameworks[$key]
            [ordered]@{ target = $key; downloadDependencies = $framework['downloadDependencies']
                frameworkReferences = $framework['frameworkReferences'] }
        }
        [ordered]@{ project = $project; assets = (Get-Identity $path); targets = @($assets.targets.Keys | Sort-Object)
            libraries = @($libraries); frameworks = @($frameworks) }
    }
    $script:dependencies.restores += [ordered]@{ stage = $Name; projects = @($snapshots) }
    Write-Json "$evidence/dependencies.json" $script:dependencies
}
function Get-PeIdentity([string] $Path) {
    $identity = Get-Identity $Path
    $stream = [IO.File]::OpenRead((Join-Path $root $Path))
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'Required artifact is not an MZ image.' }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 0x40 -or $offset -gt $stream.Length - 6) { throw 'Invalid PE header offset.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw 'Required artifact is not a PE AMD64 image.'
        }
    } finally { $reader.Dispose() }
    $identity.machine = 'AMD64 (0x8664)'
    $identity.verification = 'MZ signature, bounded e_lfanew, PE signature and COFF Machine via BinaryReader'
    return $identity
}
function Invoke-Preflight {
    $arguments = @("$probeOutput/AetherUI.Metrics.Preflight.dll", '--output', "$evidence/preflight.json")
    $record = [ordered]@{ file = 'dotnet'; arguments = $arguments; exitCode = $null
        timeoutMilliseconds = 30000; timedOut = $false; termination = 'not-needed'; logsComplete = $false }
    $script:stage.commands += $record
    Save-Stage
    $start = [Diagnostics.ProcessStartInfo]::new('dotnet')
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.CreateNoWindow = $true
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $stdout = [IO.File]::Create((Join-Path $root "$logs/preflight.stdout.txt"))
    $stderr = [IO.File]::Create((Join-Path $root "$logs/preflight.stderr.txt"))
    $started = $false
    $terminationAttempted = $false
    try {
        if (-not $process.Start()) { throw 'Preflight did not start.' }
        $started = $true
        # Drain both pipes asynchronously to files while retaining the Process object.
        $outTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $errTask = $process.StandardError.BaseStream.CopyToAsync($stderr)
        $finished = $process.WaitForExit(30000)
        if (-not $finished) {
            $record.timedOut = $true
            $record.termination = 'uncertain'
            Save-Stage
            $terminationAttempted = $true
            $process.Kill($true)
            $finished = $process.WaitForExit(5000)
            if (-not $finished) { throw 'Preflight timed out; process-tree termination is uncertain.' }
            $record.exitCode = $process.ExitCode
            $record.termination = 'exited-after-tree-kill'
        } else {
            $record.exitCode = $process.ExitCode
        }
        Save-Stage
        if (-not $outTask.Wait(5000) -or -not $errTask.Wait(5000)) {
            throw 'Preflight stdout/stderr capture is incomplete.'
        }
        $record.logsComplete = $true
        if ($record.timedOut) { throw 'Preflight exceeded its 30 second limit.' }
        if ($record.exitCode -ne 0) { throw "Preflight exited $($record.exitCode)." }
        $result = Get-Content -Raw -LiteralPath "$evidence/preflight.json" | ConvertFrom-Json -AsHashtable
        if ($result.schema -ne 'managed-preparation-v1' -or $result.status -ne 'passed' -or
            $result.checks.Count -ne 19 -or @($result.checks | Where-Object { $_.passed -ne $true }).Count -ne 0 -or
            $result.source.commit -ne $script:manifest.source.commit -or
            $result.loadedNative.sha256 -ne $script:manifest.build.native.sha256) {
            throw 'Preflight evidence is missing, incomplete or inconsistent.'
        }
        $script:stage.details = Get-Identity "$evidence/preflight.json"
    } catch {
        # An unexpected capture/wrapper failure must not abandon a live child.
        # If a tree kill already failed, retain uncertainty instead of retrying it.
        if ($started -and -not $terminationAttempted -and -not $process.HasExited) {
            $record.termination = 'uncertain'
            $terminationAttempted = $true
            Save-Stage
            try {
                $process.Kill($true)
                if ($process.WaitForExit(5000)) {
                    $record.exitCode = $process.ExitCode
                    $record.termination = 'exited-after-wrapper-failure-tree-kill'
                }
            } catch { $record.terminationErrorType = $_.Exception.GetType().FullName }
        }
        throw
    } finally {
        $stdout.Dispose()
        $stderr.Dispose()
        $process.Dispose()
        Save-Stage
        # These are only this fixed public fixture's logs. They are not artifact inputs.
        Write-Host '--- preflight stdout ---'
        Get-Content -LiteralPath "$logs/preflight.stdout.txt" | Out-Host
        Write-Host '--- preflight stderr ---'
        Get-Content -LiteralPath "$logs/preflight.stderr.txt" | Out-Host
    }
}

Save-Manifest
try {
    Invoke-Stage 'source' {
        $commit = @(Invoke-Native 'git' @('rev-parse', 'HEAD'))[-1].Trim()
        $tree = @(Invoke-Native 'git' @('rev-parse', 'HEAD^{tree}'))[-1].Trim()
        $nativeTree = @(Invoke-Native 'git' @('rev-parse', 'HEAD:native'))[-1].Trim()
        $managedTree = @(Invoke-Native 'git' @('rev-parse', 'HEAD:bindings/dotnet/src'))[-1].Trim()
        $actualBaselineTree = @(Invoke-Native 'git' @('rev-parse', "$baseline`^{tree}"))[-1].Trim()
        if ($actualBaselineTree -ne $baselineTree -or $commit -ne $env:GITHUB_SHA) {
            throw 'The baseline tree or checked out push SHA is not the expected identity.'
        }
        $protected = @('native', 'bindings/dotnet/src', 'bindings/dotnet/tools/AetherUI.Validator',
            'bindings/dotnet/Directory.Build.props', 'bindings/dotnet/Directory.Packages.props', 'global.json')
        $null = Invoke-Native 'git' (@('diff', '--exit-code', $baseline, 'HEAD', '--') + $protected)
        $null = Invoke-Native 'git' (@('diff', '--exit-code', 'HEAD', '--') + $protected)
        $files = @('.github/workflows/validator-managed-diagnostics.yml', 'global.json',
            'bindings/dotnet/Directory.Build.props', 'bindings/dotnet/Directory.Packages.props',
            $core, $interop, $windows, $validator, $probe,
            'bindings/dotnet/src/AetherUI.Core/Layout/ITextMeasurer.cs',
            'bindings/dotnet/tests/AetherUI.Metrics.Preflight/Program.cs',
            'bindings/dotnet/tests/AetherUI.Metrics.Preflight/Prepare.ps1')
        $script:manifest.source = [ordered]@{ commit = $commit; tree = $tree; nativeTree = $nativeTree
            managedSourceTree = $managedTree; productMatchesBaseline = $true
            files = @($files | ForEach-Object { Get-Identity $_ }) }
    }
    Invoke-Stage 'toolchain' {
        $version = @(Invoke-Native 'dotnet' @('--version'))[-1].Trim()
        if ($version -ne '10.0.302' -or -not $IsWindows -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
            throw 'Preparation requires official SDK 10.0.302 on hosted Windows x64.'
        }
        $null = Invoke-Native 'dotnet' @('--info')
        $cmakeVersion = @(Invoke-Native 'cmake' @('--version'))[0].Trim()
        $script:manifest.toolchain = [ordered]@{ dotnetSdk = $version; dotnetSource = 'actions/setup-dotnet@v4 (Microsoft official SDK)'
            cmake = $cmakeVersion; os = [Runtime.InteropServices.RuntimeInformation]::OSDescription
            osArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
            processArchitecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
            imageOS = $env:ImageOS; imageVersion = $env:ImageVersion }
    }
    Invoke-Stage 'native-configure' {
        $null = Invoke-Native 'cmake' @('-S', 'native', '-B', $nativeBuild, '-A', 'x64', '-DAETHERUI_BUILD_TESTS=OFF',
            "-DCMAKE_INSTALL_PREFIX=$(Join-Path $root $nativeInstall)")
        if (-not (Select-String -LiteralPath "$nativeBuild/CMakeCache.txt" -Pattern '^AETHERUI_BUILD_TESTS:BOOL=OFF$' -Quiet)) {
            throw 'Native tests must be disabled.'
        }
        $compilerFiles = @(Get-ChildItem -Path "$nativeBuild/CMakeFiles/*/CMakeCXXCompiler.cmake")
        if ($compilerFiles.Count -ne 1) { throw 'Expected one CMake C++ compiler identity.' }
        $compilerText = Get-Content -Raw -LiteralPath $compilerFiles[0].FullName
        $id = [regex]::Match($compilerText, 'set\(CMAKE_CXX_COMPILER_ID "([^"]+)"\)').Groups[1].Value
        $version = [regex]::Match($compilerText, 'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)').Groups[1].Value
        if ($id -ne 'MSVC' -or [string]::IsNullOrEmpty($version)) { throw 'Expected the hosted MSVC C++ compiler.' }
        $script:manifest.toolchain.compiler = [ordered]@{ id = $id; version = $version; platform = 'x64' }
        $script:stage.details = [ordered]@{ tests = 'OFF'; cache = (Get-Identity "$nativeBuild/CMakeCache.txt") }
    }
    Invoke-Stage 'native-build' {
        $null = Invoke-Native 'cmake' @('--build', $nativeBuild, '--config', 'Release', '--target', 'aether_core', '--parallel', '3')
    }
    Invoke-Stage 'native-install' { $null = Invoke-Native 'cmake' @('--install', $nativeBuild, '--config', 'Release') }
    Invoke-Stage 'native-verify' {
        $script:manifest.build.native = Get-PeIdentity "$nativeInstall/bin/aether_core.dll"
        $script:stage.details = $script:manifest.build.native
    }
    Invoke-Stage 'core-restore' {
        $null = Invoke-Native 'dotnet' (@('restore', $core) + $common)
        Save-Assets 'core-restore' @($interop, $core)
    }
    Invoke-Stage 'core-build' {
        $null = Invoke-Native 'dotnet' (@('build', $core, '-c', 'Release', '--no-restore') + $common)
        # The referenced Interop library is RID-agnostic; its own output has no RID suffix.
        # Keep the exact observed build path, not a wildcard or a missing-file fallback.
        $script:manifest.build.core = @(Get-Identity 'bindings/dotnet/src/AetherUI.Core/bin/Release/net10.0/win-x64/AetherUI.Core.dll'
            Get-Identity 'bindings/dotnet/src/AetherUI.Core.Native.Interop/bin/Release/net10.0/AetherUI.Core.Native.Interop.dll')
    }
    Invoke-Stage 'validator-restore' {
        $null = Invoke-Native 'dotnet' (@('restore', $validator) + $validatorOptions)
        Save-Assets 'validator-restore' @($interop, $core, $windows, $validator)
    }
    Invoke-Stage 'validator-build' {
        $null = Invoke-Native 'dotnet' (@('build', $validator, '-c', 'Release', '--no-restore') + $validatorOptions)
        $bin = 'bindings/dotnet/tools/AetherUI.Validator/bin/Release/net10.0-windows10.0.19041.0/win-x64'
        $script:manifest.build.validator = @(
            Get-PeIdentity "$bin/AetherUI.Validator.exe"
            foreach ($file in @('AetherUI.Validator.dll', 'AetherUI.Windows.dll', 'AetherUI.Core.dll', 'AetherUI.Core.Native.Interop.dll')) {
                Get-Identity "$bin/$file"
            }
        )
    }
    Invoke-Stage 'preflight-restore' {
        $null = Invoke-Native 'dotnet' (@('restore', $probe) + $common)
        Save-Assets 'preflight-restore' @($interop, $core, $probe)
    }
    Invoke-Stage 'preflight-build' {
        $null = Invoke-Native 'dotnet' (@('build', $probe, '-c', 'Release', '--no-restore') + $common + @('-o', $probeOutput))
        $script:manifest.build.preflight = @(
            foreach ($file in @('AetherUI.Metrics.Preflight.dll', 'AetherUI.Core.dll', 'AetherUI.Core.Native.Interop.dll', 'aether_core.dll')) {
                Get-Identity "$probeOutput/$file"
            }
        )
    }
    Invoke-Stage 'preflight-run' { Invoke-Preflight }
    $script:manifest.status = 'preparation-steps-passed-awaiting-review'
} catch {
    $script:manifest.status = 'failed'
    throw
} finally { Save-Manifest }
