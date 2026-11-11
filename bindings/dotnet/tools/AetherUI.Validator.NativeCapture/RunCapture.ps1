# Fixed Aether-only entrypoints. No shell command supplied by a caller is executed.
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('self-test','capture')][string]$Mode)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
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
$dotnet = (Get-Command dotnet -CommandType Application -ErrorAction Stop).Source
$start = [Diagnostics.ProcessStartInfo]::new($dotnet)
$start.WorkingDirectory=$root
$start.UseShellExecute=$false
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
$start.CreateNoWindow=$true
$arguments = @((Join-Path $bin 'AetherUI.Validator.NativeCapture.dll'),$Mode,'--output',$output)
if ($Mode -eq 'capture') {
    $arguments += @('--page',(Join-Path $root 'bindings/dotnet/tools/AetherUI.Validator/Pages/flexbox_runtime_validation.html'),
        '--expected-sha256','eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837','--viewport','1440x900','--dpi','96','--driver','warp')
}
foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
$process=[Diagnostics.Process]::new()
$process.StartInfo=$start
$stdout=$null; $stderr=$null; $outTask=$null; $errTask=$null; $started=$false; $killAttempted=$false
try {
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
