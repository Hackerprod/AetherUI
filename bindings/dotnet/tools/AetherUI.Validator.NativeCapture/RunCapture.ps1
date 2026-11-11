# Fixed Aether-only entrypoints. No shell command supplied by a caller is executed.
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('self-test','capture')][string]$Mode)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference=$false # Record the fixed SDK probe exit immediately.
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
Set-Location $root
$evidence = Join-Path $root 'artifacts/validator-native-capture/evidence'
$logs = Join-Path $root 'artifacts/validator-native-capture/process-logs'
$bin = Join-Path $root 'bindings/dotnet/tools/AetherUI.Validator.NativeCapture/bin/Release/net10.0-windows10.0.19041.0/win-x64'
$output = Join-Path $evidence $Mode
$limit = if ($Mode -eq 'self-test') { 60000 } else { 180000 }
$record = [ordered]@{ schema='aether-native-process-v1'; mode=$Mode; status='starting'; timeoutMilliseconds=$limit; exitCode=$null; timedOut=$false; termination='not-needed'; descendantsObserved=@(); monitor='descendants only; polling may miss ephemeral processes; static graph and IL gates also required'; logsComplete=$false }
$recordPath = Join-Path $evidence "$Mode-process.json"
if ((Test-Path $output) -or (Test-Path $recordPath)) { throw 'Output already exists; retries and overwrite are not permitted.' }
if (-not (Test-Path (Join-Path $evidence 'build.json'))) { throw 'Build and dependency gate evidence is required.' }
$build = Get-Content -Raw -LiteralPath (Join-Path $evidence 'build.json') | ConvertFrom-Json
if ($build.status -ne 'passed') { throw 'Build and dependency gates did not pass.' }
if ($Mode -eq 'capture') {
    $self = Get-Content -Raw -LiteralPath (Join-Path $evidence 'self-test-process.json') | ConvertFrom-Json
    if ($self.status -ne 'passed' -or $self.exitCode -ne 0 -or -not $self.logsComplete) { throw 'Self-test must pass before fixture execution.' }
}
function Save-Record { $record | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $recordPath -Encoding utf8 }
Save-Record
$start = [Diagnostics.ProcessStartInfo]::new()
$start.WorkingDirectory=$root
$start.UseShellExecute=$false
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
$start.CreateNoWindow=$true
[string[]]$arguments = @((Join-Path $bin 'AetherUI.Validator.NativeCapture.dll'),$Mode,'--output',$output)
if ($Mode -eq 'capture') {
    $arguments += @('--page',(Join-Path $root 'bindings/dotnet/tools/AetherUI.Validator/Pages/flexbox_runtime_validation.html'),
        '--expected-sha256','eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837','--viewport','1440x900','--dpi','96','--driver','warp')
}
foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
$process=[Diagnostics.Process]::new()
$process.StartInfo=$start
$stdout=$null; $stderr=$null; $outTask=$null; $errTask=$null; $started=$false; $killAttempted=$false
try {
    # Never enumerate PATH here: Get-Command can return multiple installations, which
    # PowerShell stringifies into one invalid executable name. No global SDK fallback.
    if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP) -or [string]::IsNullOrWhiteSpace($env:DOTNET_ROOT)) {
        throw 'The isolated Actions SDK location is unavailable.'
    }
    [string]$isolatedRoot=[IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath((Join-Path $env:RUNNER_TEMP 'aetherui-dotnet-10.0.302')))
    [string]$configuredRoot=[IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath($env:DOTNET_ROOT))
    if (-not [string]::Equals($isolatedRoot,$configuredRoot,[StringComparison]::OrdinalIgnoreCase) -or $build.sdk -ne '10.0.302') {
        throw 'SDK root or prior build SDK differs from the approved isolated installation.'
    }
    $hostFile=Get-Item -LiteralPath (Join-Path $isolatedRoot 'dotnet.exe') -ErrorAction Stop
    $sdkFile=Get-Item -LiteralPath (Join-Path $isolatedRoot 'sdk/10.0.302/dotnet.dll') -ErrorAction Stop
    if ($hostFile -isnot [IO.FileInfo] -or $sdkFile -isnot [IO.FileInfo] -or $hostFile.Length -le 0 -or $sdkFile.Length -le 0) {
        throw 'The approved isolated host and SDK files must exist and be nonempty.'
    }
    [string]$dotnet=$hostFile.FullName
    $versionLines=@(& $dotnet '--version')
    $versionExit=$LASTEXITCODE
    $record.sdkProbeExitCode=$versionExit
    if ($versionExit -ne 0 -or $versionLines.Count -ne 1 -or $versionLines[0].Trim() -ne '10.0.302') {
        throw 'The exact isolated host did not select SDK 10.0.302.'
    }
    $record.hostIdentity=[ordered]@{source='actions/setup-dotnet@v4 isolated installation';file='dotnet.exe';sdk='10.0.302';
        hostSha256=(Get-FileHash -LiteralPath $dotnet -Algorithm SHA256).Hash.ToLowerInvariant();
        sdkSha256=(Get-FileHash -LiteralPath $sdkFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    # FileName is a single absolute path. ArgumentList preserves each path (including
    # spaces) as one argument; no shell, joined command line, or alternate executable.
    $start.FileName=$dotnet
    Save-Record
    $stdout=[IO.File]::Open((Join-Path $logs "$Mode.stdout.txt"),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    $stderr=[IO.File]::Open((Join-Path $logs "$Mode.stderr.txt"),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    if (-not $process.Start()) { throw 'The fixed native capture executable failed to start.' }
    $started=$true
    $outTask=$process.StandardOutput.BaseStream.CopyToAsync($stdout)
    $errTask=$process.StandardError.BaseStream.CopyToAsync($stderr)
    $record.status='running'; Save-Record
    $watch=[Diagnostics.Stopwatch]::StartNew()
    # The executable has no sanctioned child process. Explicit allowlist is empty.
    # Query only this known PID's children, not arbitrary user process metadata.
    while (-not $process.HasExited) {
        $children=@(Get-CimInstance -ClassName Win32_Process -Filter "ParentProcessId = $($process.Id)" -OperationTimeoutSec 2 -ErrorAction Stop)
        if ($children.Count -gt 0) {
            $record.descendantsObserved=@($children | ForEach-Object { [ordered]@{ name=$_.Name; pid=$_.ProcessId } })
            throw 'Unexpected descendant: no child processes are allowed.'
        }
        if ($watch.ElapsedMilliseconds -ge $limit) {
            $record.timedOut=$true; $record.status='timeout'; $record.exitCode=124
            throw 'Native capture exceeded its fixed process deadline.'
        }
        $null=$process.WaitForExit(25)
    }
    # Inspect parent-scoped descendant relation once more after exit (CIM failures fail closed).
    $children=@(Get-CimInstance -ClassName Win32_Process -Filter "ParentProcessId = $($process.Id)" -OperationTimeoutSec 2 -ErrorAction Stop)
    if ($children.Count -ne 0) { throw 'Unexpected descendant survived the capture process.' }
    $record.exitCode=$process.ExitCode
    if (-not $outTask.Wait(5000) -or -not $errTask.Wait(5000)) { throw 'Process logs are incomplete.' }
    $record.logsComplete=$true
    if ($record.exitCode -ne 0) { throw "Native diagnostic exited $($record.exitCode); incomplete results are not green." }
    $record.status='passed'
} catch {
    if (-not $record.timedOut) { $record.status='failed' }
    $record.errorType=$_.Exception.GetType().FullName
    if ($started -and -not $process.HasExited -and -not $killAttempted) {
        $killAttempted=$true; $record.termination='termination-uncertain'; Save-Record
        try {
            $process.Kill($true)
            if ($process.WaitForExit(10000)) { $record.termination='confirmed-exited-after-tree-kill' }
        } catch { $record.terminationErrorType=$_.Exception.GetType().FullName }
    }
    if ($started -and $process.HasExited -and $null -ne $outTask -and $null -ne $errTask) {
        try { $record.logsComplete=$outTask.Wait(5000) -and $errTask.Wait(5000) } catch { $record.logsComplete=$false }
    }
    throw
} finally {
    Save-Record
    if ($null -ne $stdout) { $stdout.Dispose() }
    if ($null -ne $stderr) { $stderr.Dispose() }
    $process.Dispose()
    # Raw stdout/stderr are retained only locally. Artifact upload has an explicit allowlist.
}
