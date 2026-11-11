# Independent synthetic headless first-load diagnostic. Never a visual capture route.
[CmdletBinding()]
param([string] $Root = '', [string] $AnchorPath = $env:AETHER_HEADLESS_BUDGET_ANCHOR, [switch] $LibraryOnly)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:FfdUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:FfdFixtureHash = 'eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837'
$script:FfdFixture = 'bindings/dotnet/tools/AetherUI.Validator/Pages/flexbox_runtime_validation.html'
$script:FfdTool = 'bindings/dotnet/tools/AetherUI.Validator.HeadlessDiagnostics'
$script:FfdNames = @('AetherUI.Core.dll','AetherUI.Core.Native.Interop.dll','aether_core.dll','AetherUI.Validator.HeadlessDiagnostics.dll')
function Stop-Ffd([string] $Code) { throw [InvalidOperationException]::new("ffd:$Code") }
function Get-FfdCode($ErrorRecord) {
    $m = $ErrorRecord.Exception.Message
    if ($m -cmatch '^ffd:([a-z0-9-]+)$') { return $Matches[1] }
    return 'operation-failed'
}
function Assert-FfdKeys($Value, [string[]] $Keys) {
    if ($Value -isnot [Collections.IDictionary] -or $Value.Count -ne $Keys.Count) { Stop-Ffd 'schema-fields' }
    foreach ($key in $Keys) { if (-not $Value.Contains($key)) { Stop-Ffd 'schema-fields' } }
}
function Assert-FfdNumber($Value, [switch] $Integer, [switch] $Signed) {
    if ($Value -is [bool] -or $Value -is [string] -or $null -eq $Value -or
        $Value -isnot [ValueType]) { Stop-Ffd 'invalid-number' }
    $n = [double]$Value
    if (-not [double]::IsFinite($n) -or (-not $Signed -and $n -lt 0) -or
        ($Integer -and ($n -ne [math]::Truncate($n) -or $n -gt [long]::MaxValue))) { Stop-Ffd 'invalid-number' }
}
function Get-FfdHash([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    try { return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant() }
    finally { $stream.Dispose() }
}
function Get-FfdBytesHash([byte[]] $Bytes) { return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant() }
function Assert-FfdPlainFile([string] $Path, [long] $Maximum = 2097152) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item -isnot [IO.FileInfo] -or $item.Length -le 0 -or $item.Length -gt $Maximum -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Stop-Ffd 'file-shape' }
    return $item
}
function Read-FfdJson([string] $Path, [long] $Maximum = 2097152) {
    $null = Assert-FfdPlainFile $Path $Maximum
    return ConvertFrom-FfdJson ($script:FfdUtf8.GetString([IO.File]::ReadAllBytes($Path)))
}
function ConvertFrom-FfdJson([string] $Text) {
    # JsonDocument rejects non-standard numbers; recursively reject duplicate names before
    # ConvertFrom-Json could overwrite them. Ordinal keys are deliberate.
    $doc = $null
    try {
        $doc = [Text.Json.JsonDocument]::Parse($Text)
        $stack = [Collections.Generic.Stack[Text.Json.JsonElement]]::new(); $stack.Push($doc.RootElement)
        while ($stack.Count) {
            $v = $stack.Pop()
            if ($v.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
                $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
                foreach ($p in $v.EnumerateObject()) {
                    if (-not $seen.Add($p.Name)) { Stop-Ffd 'json-duplicate-key' }; $stack.Push($p.Value)
                }
            } elseif ($v.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
                foreach ($e in $v.EnumerateArray()) { $stack.Push($e) }
            } elseif ($v.ValueKind -eq [Text.Json.JsonValueKind]::Number) {
                if (-not [double]::IsFinite($v.GetDouble())) { Stop-Ffd 'invalid-number' }
            }
        }
        return ConvertFrom-Json -InputObject $Text -AsHashtable -Depth 64
    } catch { if ($_.Exception.Message.StartsWith('ffd:')) { throw }; Stop-Ffd 'json-invalid' }
    finally { if ($null -ne $doc) { $doc.Dispose() } }
}
function Write-FfdJson([string] $Path, $Value) {
    $bytes = $script:FfdUtf8.GetBytes(($Value | ConvertTo-Json -Depth 64 -Compress))
    if ($bytes.Length -gt 2097152) { Stop-Ffd 'result-byte-limit' }
    [IO.File]::WriteAllBytes($Path, $bytes)
}
function New-FfdBudget($Anchor, [string] $RunId, [string] $Attempt, [string] $Job,
    [scriptblock] $Tick = { [Environment]::TickCount64 }, [scriptblock] $Local = $null) {
    Assert-FfdKeys $Anchor @('schema','runId','runAttempt','jobId','tickCount64','utc')
    if ($Anchor.schema -cne 'headless-budget-anchor-v1' -or $Anchor.runId -cne $RunId -or
        $Anchor.runAttempt -cne $Attempt -or $Anchor.jobId -cne $Job -or
        $RunId -cnotmatch '^[0-9]+$' -or $Attempt -cnotmatch '^[0-9]+$' -or [string]::IsNullOrEmpty($Job)) { Stop-Ffd 'budget-anchor-invalid' }
    Assert-FfdNumber $Anchor.tickCount64 -Integer
    $now = & $Tick
    Assert-FfdNumber $now -Integer
    if ($now -lt $Anchor.tickCount64 -or $now - $Anchor.tickCount64 -gt 1200000) { Stop-Ffd 'budget-anchor-invalid' }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    if ($null -eq $Local) { $Local = { $watch.Elapsed.TotalMilliseconds }.GetNewClosure() }
    return @{ anchor=$Anchor.tickCount64; tick=$Tick; local=$Local; phaseStart=0.0; phaseLimit=0.0; phase='admission' }
}
function Read-FfdBudgetAnchor([string] $Path,[string] $RunId,[string] $Attempt,[string] $Job,
    [scriptblock] $Tick={ [Environment]::TickCount64 },[scriptblock] $Local=$null) {
    try{
        $anchor=Read-FfdJson $Path 4096
        return New-FfdBudget $anchor $RunId $Attempt $Job $Tick $Local
    }catch{Stop-Ffd 'budget-anchor-invalid'}
}
function Assert-FfdBudget($Budget, [double] $RemainingSeconds, [switch] $Finalizing) {
    $now = & $Budget.tick; $local = & $Budget.local
    Assert-FfdNumber $now -Integer; Assert-FfdNumber $local
    if ($now -lt $Budget.anchor) { Stop-Ffd 'budget-anchor-invalid' }
    if (($now - $Budget.anchor) + $RemainingSeconds * 1000 -gt 900000 -or
        ($RemainingSeconds -ne 540 -and $local + [math]::Max(0, ($RemainingSeconds - 60)) * 1000 -gt 480000)) { Stop-Ffd 'budget-not-admitted' }
    if ($local -gt 480000 -or (-not $Finalizing -and $local -gt 465000)) { Stop-Ffd 'step-timeout' }
}
function Start-FfdPhase($Budget, [string] $Name, [double] $Limit, [double] $Remaining) {
    Assert-FfdBudget $Budget $Remaining -Finalizing:($Name -eq 'finalize')
    $Budget.phase = $Name; $Budget.phaseStart = $(if ($Name -eq 'verification') { 0.0 } else { & $Budget.local }); $Budget.phaseLimit = $Limit * 1000
}
function Assert-FfdDeadline($Budget) {
    if ($null -eq $Budget) { return }
    $local = & $Budget.local
    if ($local -gt 480000 -or ($Budget.phase -ne 'finalize' -and $local -gt 465000)) { Stop-Ffd 'step-timeout' }
    if ($local - $Budget.phaseStart -gt $Budget.phaseLimit) { Stop-Ffd 'phase-timeout' }
    $now = & $Budget.tick
    if ($now -lt $Budget.anchor) { Stop-Ffd 'budget-anchor-invalid' }
    if ($now - $Budget.anchor -gt 840000) { Stop-Ffd 'observable-timeout' }
}
function Receive-FfdPipe($Stream, $File, [byte[]] $Buffer, [ref] $Task, [ref] $Eof, [ref] $Count, [long] $Limit) {
    if ($Eof.Value -or -not $Task.Value.IsCompleted) { return }
    $n = $Task.Value.GetAwaiter().GetResult()
    if ($n -eq 0) { $Eof.Value = $true; return }
    if ($Count.Value + $n -gt $Limit) { Stop-Ffd 'pipe-byte-limit' }
    $File.Write($Buffer,0,$n); $Count.Value += $n
    $Task.Value = $Stream.ReadAsync($Buffer,0,$Buffer.Length)
}
function Invoke-FfdProcess([string] $Executable, [string[]] $Arguments, [string] $Directory,
    [string] $LogPrefix, [double] $TimeoutSeconds, [double] $KillSeconds, $Budget = $null,
    [scriptblock] $OnProgress = $null, [scriptblock] $TestOutputGate = $null, [scriptblock] $TestAfterKill = $null) {
    $r = [ordered]@{ started=$false; exitCode=$null; timedOut=$false; termination='not-needed';
        logsComplete=$false; stdoutBytes=0L; stderrBytes=0L; elapsedMs=$null; status='incomplete'; code=$null }
    $p = [Diagnostics.Process]::new(); $info = [Diagnostics.ProcessStartInfo]::new($Executable)
    $info.WorkingDirectory=$Directory; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true; $info.RedirectStandardInput=$true
    foreach ($arg in $Arguments) { $info.ArgumentList.Add($arg) }; $p.StartInfo=$info
    $o=$null; $e=$null; $killAttempted=$false; $watch=[Diagnostics.Stopwatch]::StartNew()
    try {
        Assert-FfdDeadline $Budget
        $o=[IO.File]::Open("$LogPrefix.stdout",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
        $e=[IO.File]::Open("$LogPrefix.stderr",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
        if (-not $p.Start()) { Stop-Ffd 'process-start' }; $r.started=$true; $p.StandardInput.Close()
        if ($null -ne $OnProgress) { & $OnProgress $r }
        $ob=[byte[]]::new(16384); $eb=[byte[]]::new(16384); $oeof=$false; $eeof=$false; $oc=0L; $ec=0L
        $ot=$p.StandardOutput.BaseStream.ReadAsync($ob,0,$ob.Length)
        $et=$p.StandardError.BaseStream.ReadAsync($eb,0,$eb.Length)
        if($null -ne $TestOutputGate){$ot=& $TestOutputGate $ot}
        while ($true) {
            Assert-FfdDeadline $Budget
            if ($watch.Elapsed.TotalSeconds -gt $TimeoutSeconds) { $r.timedOut=$true; Stop-Ffd 'process-timeout' }
            $ready=(-not $oeof -and $ot.IsCompleted) -or (-not $eeof -and $et.IsCompleted)
            Receive-FfdPipe $p.StandardOutput.BaseStream $o $ob ([ref]$ot) ([ref]$oeof) ([ref]$oc) 1MB
            Receive-FfdPipe $p.StandardError.BaseStream $e $eb ([ref]$et) ([ref]$eeof) ([ref]$ec) 1MB
            $r.stdoutBytes=$oc; $r.stderrBytes=$ec
            if ($p.HasExited) { $r.exitCode=$p.ExitCode; if ($oeof -and $eeof) { break } }
            if (-not $ready) { [Threading.Thread]::Sleep(1) }
        }
        $r.logsComplete=$true
        if ($r.exitCode -ne 0) { Stop-Ffd 'process-nonzero' }
        $r.status='completed'
    } catch {
        $r.code=Get-FfdCode $_
        if ($r.started -and -not $p.HasExited -and -not $killAttempted) {
            $killAttempted=$true; $r.termination='uncertain'
            if ($null -ne $OnProgress) { & $OnProgress $r }
            try {
                $p.Kill($true)
                if($null -ne $TestAfterKill){& $TestAfterKill $p}
                # Termination allowance is bounded by both phase and local deadlines.
                $remaining=$KillSeconds
                if ($null -ne $Budget) {
                    $local=& $Budget.local
                    $remaining=[math]::Min($remaining,[math]::Max(0,($Budget.phaseStart+$Budget.phaseLimit-$local)/1000))
                    $remaining=[math]::Min($remaining,[math]::Max(0,(465000-$local)/1000))
                }
                if ($p.WaitForExit([int]($remaining*1000))) { $r.termination='exited-after-tree-kill'; $r.exitCode=$p.ExitCode }
            } catch { $r.termination='uncertain' }
        }
    } finally {
        if ($null -ne $o) { $o.Dispose() }; if ($null -ne $e) { $e.Dispose() }
        $r.elapsedMs=$watch.Elapsed.TotalMilliseconds; $p.Dispose()
        if ($null -ne $OnProgress) { & $OnProgress $r }
    }
    return $r
}
function Assert-FfdProcess($Record) {
    if ($Record.status -cne 'completed' -or -not $Record.started -or $Record.exitCode -ne 0 -or
        $Record.timedOut -or -not $Record.logsComplete -or $Record.termination -cne 'not-needed') { Stop-Ffd 'process-incomplete' }
}
function Save-FfdPartialEvents([string] $Path,[string] $PublicPath,[string] $Session) {
    $safe=[Collections.Generic.List[string]]::new(); $last=$null; $complete=$true
    try {
        $item=Get-Item -LiteralPath $Path
        if ($item.Length -gt 1MB) { Stop-Ffd 'pipe-byte-limit' }
        $text=$script:FfdUtf8.GetString([IO.File]::ReadAllBytes($Path))
        $lines=$text.Split("`n"); if (-not $text.EndsWith("`n")) { $complete=$false }
        for($i=0;$i -lt $lines.Length-1;$i++) {
            try {
                $e=ConvertFrom-FfdJson $lines[$i]
                Assert-FfdKeys $e @('schema','event','session','sequence','data')
                if ($e.schema -cne 'headless-events-v1' -or $e.session -cne $Session -or
                    $e.event -cnotin @('session','input-config','phase-start','metrics-snapshot','result','metrics-final','session-end','error')) { Stop-Ffd 'event-identity' }
                Assert-FfdNumber $e.sequence -Integer
                $data=[ordered]@{}
                # Failed-stream retention is an explicit projection, not arbitrary stdout publication.
                # Full raw bounded logs stay private. Fields without a validated schema are omitted.
                if ($e.event -ceq 'error') {
                    if ($e.data.code -cnotmatch '^[a-z0-9-]{1,80}$' -or $e.data.exceptionType -cnotmatch '^[A-Za-z0-9_.]{1,100}$') { Stop-Ffd 'error-shape' }
                    $data.code=$e.data.code; $data.exceptionType=$e.data.exceptionType
                    if($e.data.Contains('lastDomain') -and $null -ne $e.data.lastDomain){if($e.data.lastDomain -cnotin @('boxes','chunks','commands','counts','dom','paintOrder','viewport','initial')){Stop-Ffd 'error-domain'};$data.lastDomain=$e.data.lastDomain}
                    if($e.data.Contains('canonicalBytes') -and $null -ne $e.data.canonicalBytes){Assert-FfdNumber $e.data.canonicalBytes -Integer;$data.canonicalBytes=$e.data.canonicalBytes}
                } elseif ($e.event -ceq 'phase-start') {
                    if ($e.data.phase -cnotin @('read','constructor','render','verification','dispose')) { Stop-Ffd 'phase-shape' };$data.phase=$e.data.phase
                } elseif ($e.event -cin @('result','metrics-final','session-end')) {
                    if ($e.data.status -cnotin @('measured','completed')) { Stop-Ffd 'event-status' };$data.status=$e.data.status
                }
                $safe.Add((@{schema=$e.schema;event=$e.event;session=$Session;sequence=$e.sequence;data=$data;retention='sanitized-partial-projection'} | ConvertTo-Json -Compress -Depth 10))
                $last=$e.event
            } catch { $complete=$false; break }
        }
    } catch { $complete=$false }
    [IO.File]::WriteAllText($PublicPath,($safe -join "`n")+"`n",$script:FfdUtf8)
    return @{lastCompleteEvent=$last;complete=$complete}
}
function Read-FfdEvents([string] $Path, [string] $Session, [switch] $Synthetic) {
    $null=Assert-FfdPlainFile $Path 1MB
    $text=$script:FfdUtf8.GetString([IO.File]::ReadAllBytes($Path))
    if (-not $text.EndsWith("`n")) { Stop-Ffd 'event-truncated' }
    $events=[Collections.Generic.List[object]]::new(); $sequence=0; $phaseIndex=0; $state='session'
    $phases=@('read','constructor','render','verification','dispose')
    foreach ($line in ($text.Split("`n") | Select-Object -SkipLast 1)) {
        if ([string]::IsNullOrWhiteSpace($line)) { Stop-Ffd 'event-empty' }
        $v=ConvertFrom-FfdJson $line
        Assert-FfdKeys $v @('schema','event','session','sequence','data')
        Assert-FfdNumber $v.sequence -Integer
        if ($v.schema -cne 'headless-events-v1' -or $v.session -cne $Session -or $v.sequence -ne $sequence++) { Stop-Ffd 'event-identity' }
        if ($v.event -ceq 'error') { Stop-Ffd 'event-error' }
        if ($v.event -ceq 'phase-start') {
            if ($state -notin @('input','phase','snapshot','dispose') -or $phaseIndex -ge 5 -or
                $v.data.phase -cne $phases[$phaseIndex]) { Stop-Ffd 'event-order' }
            Assert-FfdKeys $v.data @('phase'); if ($state -eq 'input' -and $phaseIndex -ne 0) { Stop-Ffd 'event-order' }; $phaseIndex++
        } elseif ($v.event -ceq 'session') {
            if ($state -cne 'session') { Stop-Ffd 'event-order' }; Assert-FfdKeys $v.data @('schema'); if($v.data.schema -cne 'headless-full-fixture-v1'){Stop-Ffd 'event-schema'}; $state='input'
        } elseif ($v.event -ceq 'input-config') {
            if ($state -cne 'input' -or $phaseIndex -ne 1) { Stop-Ffd 'event-order' }; $state='phase'
            if($Synthetic){Assert-FfdKeys $v.data @('synthetic');if($v.data.synthetic -ne $true){Stop-Ffd 'event-schema'}}else{Assert-FfdKeys $v.data @('inputSha256','sourceCommit','sourceTree','files','configuration')}
        } elseif ($v.event -ceq 'metrics-snapshot') {
            if ($state -cne 'phase' -or $phaseIndex -ne 3) { Stop-Ffd 'event-order' }; $state='snapshot'
            if($Synthetic){Assert-FfdKeys $v.data @('synthetic');if($v.data.synthetic -ne $true){Stop-Ffd 'event-schema'}}else{Assert-FfdKeys $v.data @('counts','metrics','timestampTicks')}
        } elseif ($v.event -ceq 'result') {
            if ($state -cne 'snapshot' -or $phaseIndex -ne 4) { Stop-Ffd 'event-order' }
            Assert-FfdKeys $v.data @('status'); if ($v.data.status -cne 'measured') { Stop-Ffd 'event-status' }; $state='final'
        } elseif ($v.event -ceq 'metrics-final') {
            if ($state -cne 'final') { Stop-Ffd 'event-order' }
            Assert-FfdKeys $v.data @('status'); if ($v.data.status -cne 'measured') { Stop-Ffd 'event-status' }; $state='dispose'
        } elseif ($v.event -ceq 'session-end') {
            if ($state -cne 'dispose' -or $phaseIndex -ne 5) { Stop-Ffd 'event-order' }
            Assert-FfdKeys $v.data @('status','reportingMs'); Assert-FfdNumber $v.data.reportingMs; if ($v.data.status -cne 'completed') { Stop-Ffd 'event-status' }; $state='end'
        } else { Stop-Ffd 'event-unknown' }
        $events.Add($v)
    }
    if ($state -cne 'end') { Stop-Ffd 'event-incomplete' }
    return ,$events.ToArray()
}
function Assert-FfdDetailedResult($Result,$Manifest) {
    $cfg=$Result.configuration
    Assert-FfdKeys $cfg @('width','height','dpiScale','textMeasurer','imageResourcePolicy','svgReferences','decodedImages','projectedBoxesOnly')
    foreach($key in @('width','height','svgReferences','decodedImages')){Assert-FfdNumber $cfg[$key] -Integer}
    Assert-FfdNumber $cfg.dpiScale
    if($cfg.projectedBoxesOnly -isnot [bool]){Stop-Ffd 'configuration-type'}
    if ($cfg.width -ne 1440 -or $cfg.height -ne 900 -or $cfg.dpiScale -ne 1 -or $cfg.textMeasurer -cne 'ApproximateTextMeasurer' -or
        $cfg.imageResourcePolicy -cne 'unresolved' -or $cfg.svgReferences -ne 3 -or $cfg.decodedImages -ne 0 -or $cfg.projectedBoxesOnly -ne $true) { Stop-Ffd 'configuration' }
    Assert-FfdKeys $Result.counts @('StyleResolutions','StyleUpdatedElements','LayoutBuilds','LayoutProjections','DisplayListBuilds','PaintChunkBuilds',
        'ScrollCompositions','RenderCalls','IncrementalRenderCalls','VisualStyleMapBuilds','VisualStyleMapUpdates','LastStyleImpact',
        'backendRenderCalls','constructorCalls','setViewportCalls','publicRenderCalls','textCallbacks','callbackFailures','NeedsRender','HasActiveTransitions')
    Assert-FfdNumber $Result.counts.LastStyleImpact -Integer
    if($Result.counts.LastStyleImpact -gt 127){Stop-Ffd 'style-impact-mask'}
    if ($Result.timestampTicks.Count -ne 5) { Stop-Ffd 'timestamp-shape' }
    foreach ($tick in $Result.timestampTicks) { Assert-FfdNumber $tick -Integer }
    if ($Result.loadedIdentities.Count -ne 4) { Stop-Ffd 'loaded-identity-set' }
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($id in $Result.loadedIdentities) {
        Assert-FfdKeys $id @('file','sha256','fullName','mvid','native')
        if ($id.file -cnotin $script:FfdNames -or -not $seen.Add($id.file) -or $id.sha256 -cne $Manifest.files[$id.file]) { Stop-Ffd 'loaded-identity' }
        if($id.native -isnot [bool]){Stop-Ffd 'loaded-identity-type'}
        if ($id.file -ceq 'aether_core.dll') {
            if ($id.native -ne $true -or $null -ne $id.fullName -or $null -ne $id.mvid) { Stop-Ffd 'loaded-native' }
        } else {
            $guid=[Guid]::Empty
            if ($id.native -ne $false -or $id.mvid -cnotmatch '^[0-9a-f-]{36}$' -or -not [Guid]::TryParseExact($id.mvid,'D',[ref]$guid) -or
                $id.fullName -cnotmatch '^AetherUI\.[A-Za-z.]+, Version=[0-9.]+, Culture=neutral, PublicKeyToken=(null|[0-9a-f]+)$') { Stop-Ffd 'loaded-managed' }
            if(($id.fullName.Split(',')[0]+'.dll') -cne $id.file){Stop-Ffd 'loaded-managed'}
        }
    }
    $r=$Result.resources
    Assert-FfdKeys $r @('processCpuMs','observedCpuMs','peakWorkingSetBytes','privateBytes','managedBytes','gcBefore','gcAfter','logicalCpuCount','os','architecture','runtime','cpuModel','cpuModelAvailability','scope')
    foreach ($name in @('processCpuMs','observedCpuMs')) { Assert-FfdNumber $r[$name] }
    foreach ($name in @('peakWorkingSetBytes','privateBytes','managedBytes','logicalCpuCount')) { Assert-FfdNumber $r[$name] -Integer }
    if($r.logicalCpuCount -lt 1){Stop-Ffd 'resource-shape'}
    if ($r.gcBefore.Count -ne 3 -or $r.gcAfter.Count -ne 3 -or $r.architecture -cne 'X64' -or $r.cpuModelAvailability -cne 'not-collected' -or $null -ne $r.cpuModel) { Stop-Ffd 'resource-shape' }
    for($i=0;$i -lt 3;$i++){ Assert-FfdNumber $r.gcBefore[$i] -Integer;Assert-FfdNumber $r.gcAfter[$i] -Integer;if($r.gcAfter[$i] -lt $r.gcBefore[$i]){Stop-Ffd 'resource-gc'} }
    foreach ($key in @('os','runtime','scope')) { if ($r[$key] -isnot [string] -or $r[$key].Length -gt 300 -or $r[$key] -match '[\r\n\\/]') { Stop-Ffd 'resource-text' } }
    if ($Result.limitations.Count -ne 6) { Stop-Ffd 'limitations-shape' }
    foreach($line in $Result.limitations) { if ($line -isnot [string] -or $line.Length -gt 200 -or $line -match '[\r\n\\/]') { Stop-Ffd 'limitations-shape' } }
    $w=$Result.witnesses
    Assert-FfdKeys $w @('schema','sentinels','canonicalBase64','sha256','bytes')
    if ($w.schema -cne 'WITNESS-1' -or $w.sentinels.Count -ne 9 -or $w.sha256 -cnotmatch '^[0-9a-f]{64}$') { Stop-Ffd 'witness-shape' }
    $bytes=[Convert]::FromBase64String($w.canonicalBase64)
    if ($bytes.Length -gt 65536 -or $bytes.Length -ne $w.bytes -or (Get-FfdBytesHash $bytes) -cne $w.sha256) { Stop-Ffd 'witness-hash' }
    $decoded=Read-FfdCanonical $bytes
    if ($decoded.tag -cne 'record' -or $decoded.value['schema'].value -cne 'WITNESS-1') { Stop-Ffd 'witness-decode' }
    $decodedSentinels=$decoded.value['sentinels'].value
    if ($decodedSentinels.Count -ne 9) { Stop-Ffd 'witness-count' }
    foreach($key in $Result.digest.cardinalities.Keys) {
        if ($decoded.value['counts'].value[$key].value -ne $Result.digest.cardinalities[$key]) { Stop-Ffd 'witness-cardinality' }
    }
    $names=@('FLEX-001','FLEX-001/stage','FLEX-001/stage/flex-target','FLEX-200','FLEX-200/stage','FLEX-200/stage/flex-target','FLEX-403','FLEX-403/stage','FLEX-403/stage/flex-target')
    $nodes=[Collections.Generic.HashSet[long]]::new();$boxes=[Collections.Generic.HashSet[long]]::new()
    for($i=0;$i -lt 9;$i++) {
        $s=$w.sentinels[$i];$d=$decodedSentinels[$i].value
        Assert-FfdKeys $s @('name','node','box','parent','borderBox','contentBox')
        if ($s.name -cne $names[$i] -or $d['name'].value -cne $s.name) { Stop-Ffd 'witness-name' }
        foreach($key in @('node','box')) {
            Assert-FfdNumber $s[$key] -Integer
            if ($d[$key].tag -cne 'int64' -or $d[$key].value -ne $s[$key]) { Stop-Ffd 'witness-index' }
        }
        if ($s.node -ge $Result.digest.cardinalities.dom -or $s.box -ge $Result.digest.cardinalities.boxes -or
            -not $nodes.Add($s.node) -or -not $boxes.Add($s.box)) { Stop-Ffd 'witness-index' }
        if ($null -ne $s.parent) { Assert-FfdNumber $s.parent -Integer;if($s.parent -ge $Result.digest.cardinalities.boxes){Stop-Ffd 'witness-parent'} }
        if ($d['parent'].value -ne $s.parent) { Stop-Ffd 'witness-parent' }
        foreach($pair in @(@('borderBox','BorderBox'),@('contentBox','ContentBox'))) {
            $rect=$s[$pair[0]];Assert-FfdKeys $rect @('X','Y','Width','Height')
            foreach($key in @('X','Y','Width','Height')) {
                Assert-FfdNumber $rect[$key] -Signed
                $v=$d[$pair[1]].value[$key]
                if ($v.tag -cne 'float32' -or [single]$rect[$key] -ne $v.value) { Stop-Ffd 'witness-geometry' }
            }
        }
        if ($s.borderBox.Width -le 0 -or $s.borderBox.Height -le 0) { Stop-Ffd 'witness-area' }
    }
}
function Assert-FfdResult($Result, [string] $Session, $Manifest) {
    Assert-FfdKeys $Result @('schema','status','session','sourceCommit','sourceTree','inputSha256','files','configuration','metrics','counts','resources','digest','witnesses','limitations','loadedIdentities','timestampTicks')
    if ($Result.schema -cne 'headless-full-fixture-v1' -or $Result.status -cne 'completed' -or
        $Result.session -cne $Session -or $Result.sourceCommit -cne $Manifest.sourceCommit -or
        $Result.sourceTree -cne $Manifest.sourceTree -or $Result.inputSha256 -cne $script:FfdFixtureHash) { Stop-Ffd 'result-identity' }
    Assert-FfdDetailedResult $Result $Manifest
    Assert-FfdKeys $Result.files $script:FfdNames
    foreach ($name in $script:FfdNames) {
        if ($Result.files[$name] -cne $Manifest.files[$name] -or $Result.files[$name] -cnotmatch '^[0-9a-f]{64}$') { Stop-Ffd 'binary-identity' }
    }
    $metricNames=@('constructorMs','firstRenderWallMs','styleResolutionMs','layoutInteropMs','layoutProjectionMs','displayListBuildMs','renderMs','readDecodeMs','verificationMs','disposeMs','checksumMs','canonicalMs')
    Assert-FfdKeys $Result.metrics $metricNames
    foreach ($key in $metricNames) { Assert-FfdNumber $Result.metrics[$key] }
    $ones=@('RenderCalls','LayoutBuilds','LayoutProjections','StyleResolutions','DisplayListBuilds','VisualStyleMapBuilds',
        'backendRenderCalls','constructorCalls','setViewportCalls','publicRenderCalls')
    $zeros=@('IncrementalRenderCalls','PaintChunkBuilds','ScrollCompositions','VisualStyleMapUpdates','callbackFailures')
    foreach ($key in $ones) { if (-not $Result.counts.Contains($key) -or $Result.counts[$key] -ne 1) { Stop-Ffd 'unexpected-multipass' } }
    foreach ($key in $zeros) { if (-not $Result.counts.Contains($key) -or $Result.counts[$key] -ne 0) { Stop-Ffd 'unexpected-work' } }
    foreach ($key in @('StyleUpdatedElements','textCallbacks')) {
        if (-not $Result.counts.Contains($key)) { Stop-Ffd 'count-missing' }; Assert-FfdNumber $Result.counts[$key] -Integer
        if ($Result.counts[$key] -le 0) { Stop-Ffd 'work-missing' }
    }
    foreach ($key in $Result.counts.Keys) {
        if ($key -notin @('NeedsRender','HasActiveTransitions','LastStyleImpact')) { Assert-FfdNumber $Result.counts[$key] -Integer }
    }
    if ($Result.counts.NeedsRender -isnot [bool] -or $Result.counts.NeedsRender) { Stop-Ffd 'pending-work' }
    if ($Result.counts.HasActiveTransitions -isnot [bool]) { Stop-Ffd 'count-shape' }
    Assert-FfdKeys $Result.digest @('schema','sha256','bytes','scope','cardinalities','commandTypes','fieldCounts')
    if ($Result.digest.schema -cne 'FFD-CANON-1' -or $Result.digest.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $Result.digest.scope -cne 'declared-full-stream-sha256; stream-not-retained') { Stop-Ffd 'digest-invalid' }
    Assert-FfdNumber $Result.digest.bytes -Integer
    if ($Result.digest.bytes -le 11 -or $Result.digest.bytes -gt 64MB) { Stop-Ffd 'digest-incomplete' }
    Assert-FfdKeys $Result.digest.cardinalities @('dom','boxes','paintOrder','chunks','commands')
    foreach ($key in $Result.digest.cardinalities.Keys) { Assert-FfdNumber $Result.digest.cardinalities[$key] -Integer }
    foreach ($key in @('dom','boxes','commands')) { if ($Result.digest.cardinalities[$key] -le 0) { Stop-Ffd 'domain-empty' } }
    $textCount=0; $fillCount=0; $total=0
    $commands=@('FillRoundedRectangleCommand','DrawBoxShadowCommand','FillRoundedRectangleGradientCommand','StrokeRoundedRectangleCommand',
        'FillBorderSideCommand','DrawTextCommand','DrawTextGradientCommand','DrawTextShadowCommand','ApplyBackdropFilterCommand',
        'DrawBackgroundImageCommand','PushClipCommand','PopClipCommand','PushTransformCommand','PopTransformCommand','PushScrollOffsetCommand',
        'PopScrollOffsetCommand','DrawScrollbarChromeCommand')
    foreach ($key in $Result.digest.commandTypes.Keys) {
        if ($key -cnotin $commands) { Stop-Ffd 'unsupported-command' }; $n=$Result.digest.commandTypes[$key]; Assert-FfdNumber $n -Integer; $total+=$n
        if ($key -cin @('DrawTextCommand','DrawTextGradientCommand','DrawTextShadowCommand')) { $textCount+=$n }
        if ($key -cin @('FillRoundedRectangleCommand','FillRoundedRectangleGradientCommand','FillBorderSideCommand')) { $fillCount+=$n }
    }
    if($Result.digest.fieldCounts -isnot [Collections.IDictionary] -or $Result.digest.fieldCounts.Count -eq 0){Stop-Ffd 'field-counts-missing'}
    foreach($field in $Result.digest.fieldCounts.Keys){
        if($field -cnotmatch '^[A-Za-z][A-Za-z0-9.]*$'){Stop-Ffd 'field-count-name'}
        Assert-FfdNumber $Result.digest.fieldCounts[$field] -Integer
        if($Result.digest.fieldCounts[$field] -le 0){Stop-Ffd 'field-count-value'}
    }
    if ($total -ne $Result.digest.cardinalities.commands -or $textCount -le 0 -or $fillCount -le 0) { Stop-Ffd 'commands-incomplete' }
}
function Get-FfdAggregate([object[]] $Results) {
    if ($Results.Count -ne 6) { Stop-Ffd 'sample-set' }
    $names=@('warmup','sample-1','sample-2','sample-3','sample-4','sample-5')
    $baseline=$Results[0]; $stats=[ordered]@{}
    for ($i=0;$i -lt 6;$i++) {
        if ($Results[$i].session -cne $names[$i] -or $Results[$i].status -cne 'completed' -or
            $Results[$i].digest.sha256 -cne $baseline.digest.sha256 -or $Results[$i].digest.bytes -ne $baseline.digest.bytes) { Stop-Ffd 'sample-homogeneity' }
        foreach ($key in $baseline.digest.cardinalities.Keys) {
            if ($Results[$i].digest.cardinalities[$key] -ne $baseline.digest.cardinalities[$key]) { Stop-Ffd 'sample-homogeneity' }
        }
    }
    foreach ($name in $baseline.metrics.Keys) {
        $raw=@($Results[1..5] | ForEach-Object { Assert-FfdNumber $_.metrics[$name]; [double]$_.metrics[$name] })
        $ordered=@($raw | Sort-Object)
        $stats[$name]=[ordered]@{ values=$raw; median=$ordered[2]; min=$ordered[0]; max=$ordered[4] }
    }
    return $stats
}
function Read-FfdSample($Record,[string] $EventsPath,[string] $ResultPath,[string] $Session,$Manifest) {
    Assert-FfdProcess $record
    if ($record.stderrBytes -ne 0) { Stop-Ffd 'unexpected-stderr' }
    $events=Read-FfdEvents $EventsPath $session
    $result=Read-FfdJson $ResultPath
    Assert-FfdResult $result $session $manifest
    $inputEvent=@($events | Where-Object {$_.event -ceq 'input-config'})[0].data
    foreach($key in @('inputSha256','sourceCommit','sourceTree')){if($inputEvent[$key] -cne $result[$key]){Stop-Ffd 'event-result-identity'}}
    foreach($key in $result.files.Keys){if($inputEvent.files[$key] -cne $result.files[$key]){Stop-Ffd 'event-result-identity'}}
    Assert-FfdKeys $inputEvent.files $script:FfdNames
    Assert-FfdKeys $inputEvent.configuration @($result.configuration.Keys)
    foreach($key in $result.configuration.Keys){if($inputEvent.configuration[$key] -cne $result.configuration[$key]){Stop-Ffd 'event-result-configuration'}}
    $snapshotEvent=@($events | Where-Object {$_.event -ceq 'metrics-snapshot'})[0].data
    Assert-FfdKeys $snapshotEvent.counts @($result.counts.Keys)
    foreach($key in $result.counts.Keys){if($snapshotEvent.counts[$key] -cne $result.counts[$key]){Stop-Ffd 'event-result-count'}}
    Assert-FfdKeys $snapshotEvent.metrics @('constructorMs','firstRenderWallMs','styleResolutionMs','layoutInteropMs','layoutProjectionMs','displayListBuildMs','renderMs','readDecodeMs')
    foreach($key in $snapshotEvent.metrics.Keys){Assert-FfdNumber $snapshotEvent.metrics[$key];if($snapshotEvent.metrics[$key] -ne $result.metrics[$key]){Stop-Ffd 'event-result-metric'}}
    if($snapshotEvent.timestampTicks.Count -ne 5){Stop-Ffd 'event-ticks'}
    for($i=0;$i -lt 5;$i++){if($snapshotEvent.timestampTicks[$i] -ne $result.timestampTicks[$i]){Stop-Ffd 'event-ticks'}}
    return @{events=$events;result=$result}
}
function Complete-FfdSummary($Summary,[object[]] $Results,$Budget) {
    if($Budget.phase -cne 'finalize'){Stop-Ffd 'finalization-phase'}
    Assert-FfdDeadline $Budget
    $statistics=Get-FfdAggregate $Results
    Assert-FfdDeadline $Budget
    # This closes only the local diagnostic, never the future external upload/review.
    $Summary.statistics=$statistics;$Summary.status='completed';$Summary.phase='complete'
}
function Get-FfdInputIdentity([string] $Path, [string] $Name) {
    $item=Assert-FfdPlainFile $Path 64MB
    return [ordered]@{file=$Name; bytes=$item.Length; sha256=(Get-FfdHash $Path)}
}
function Assert-FfdSource($Source, [string] $Root, $Budget) {
    if ($Source.commit -cne $env:GITHUB_SHA -or $Source.commit -cnotmatch '^[0-9a-f]{40}$' -or
        $Source.tree -cnotmatch '^[0-9a-f]{40}$' -or $Source.files.Count -eq 0 -or $Source.files.Count -gt 2048) { Stop-Ffd 'source-identity' }
    if ($Source.inventory.Count -ne $Source.files.Count -or $Source.identities.Count -ne 4 -or $Source.overlay.Count -ne 19 -or
        $Source.diagnostics.Count -ne 5 -or $Source.controls.Count -ne 2) { Stop-Ffd 'source-inventory-count' }
    $inv=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach($entry in $Source.inventory) {
        Assert-FfdKeys $entry @('path','mode','type','blob')
        if($entry.mode -cne '100644' -or $entry.type -cne 'blob' -or $entry.blob -cnotmatch '^[0-9a-f]{40}$' -or $inv.ContainsKey($entry.path)){Stop-Ffd 'source-inventory'}
        $inv.Add($entry.path,$entry)
    }
    for($i=0;$i -lt 4;$i++){
        if($Source.identities[$i].name -cne @('B','P','M','C')[$i] -or $Source.identities[$i].commit -cnotmatch '^[0-9a-f]{40}$' -or $Source.identities[$i].tree -cnotmatch '^[0-9a-f]{40}$'){Stop-Ffd 'source-history'}
    }
    $dPaths=@("$script:FfdTool/AetherUI.Validator.HeadlessDiagnostics.csproj","$script:FfdTool/Program.cs","$script:FfdTool/Run.ps1","$script:FfdTool/Contract.Tests.ps1",'.github/workflows/validator-managed-diagnostics.yml')
    for($i=0;$i -lt 5;$i++){
        $entry=$Source.diagnostics[$i]
        if($entry.path -cne $dPaths[$i] -or $entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $entry.after.blob -cne $inv[$entry.path].blob -or
            $entry.after.mode -cne '100644' -or $entry.after.type -cne 'blob'){Stop-Ffd 'source-diagnostics'}
        if($i -lt 4){if($entry.status -cne 'A' -or $null -ne $entry.before){Stop-Ffd 'source-diagnostics'}}
        elseif($entry.status -cne 'M' -or $entry.before.blob -cnotmatch '^[0-9a-f]{40}$' -or $entry.beforeSha256 -cnotmatch '^[0-9a-f]{64}$'){Stop-Ffd 'source-diagnostics'}
    }
    foreach($control in $Source.controls){
        if($control.path -cnotin @('bindings/dotnet/tests/AetherUI.Metrics.Preflight/Prepare.ps1','bindings/dotnet/tests/AetherUI.Metrics.Preflight/Scope.Tests.ps1') -or
            $control.blob -cne $inv[$control.path].blob -or $control.sha256 -cnotmatch '^[0-9a-f]{64}$'){Stop-Ffd 'source-control'}
    }
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in $Source.files) {
        Assert-FfdDeadline $Budget
        $path=$entry.path
        if ($path -cnotmatch '^[A-Za-z0-9_.\-/]+$' -or $path.StartsWith('/') -or $path.Split('/') -contains '..' -or
            -not $seen.Add($path)) { Stop-Ffd 'source-path' }
        if(-not $inv.ContainsKey($path) -or $inv[$path].blob -cne $entry.blob -or $entry.blobSha256 -cnotmatch '^[0-9a-f]{64}$' -or $entry.physicalSha256 -cnotmatch '^[0-9a-f]{64}$'){Stop-Ffd 'source-inventory'}
        foreach($pin in @($Source.diagnostics)+@($Source.controls)){if($pin.path -ceq $path -and $pin.sha256 -cne $entry.blobSha256){Stop-Ffd 'source-pin'}}
        $full=[IO.Path]::GetFullPath((Join-Path $Root $path))
        $file=Get-Item -LiteralPath $full -Force
        if ($file -isnot [IO.FileInfo] -or $file.Length -gt 4MB -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Stop-Ffd 'source-file-shape' }
        if ($file.Length -ne $entry.bytes -or (Get-FfdHash $full) -cne $entry.physicalSha256) { Stop-Ffd 'source-rehash' }
    }
    foreach ($path in @("$script:FfdTool/Program.cs","$script:FfdTool/Run.ps1","$script:FfdTool/Contract.Tests.ps1",
        "$script:FfdTool/AetherUI.Validator.HeadlessDiagnostics.csproj",$script:FfdFixture,'.github/workflows/validator-managed-diagnostics.yml','bindings/dotnet/tests/AetherUI.Metrics.Preflight/Scope.ps1','bindings/dotnet/tests/AetherUI.Metrics.Preflight/Scope.Tests.ps1')) {
        if (-not $seen.Contains($path)) { Stop-Ffd 'source-new-layer-missing' }
    }
}
function Assert-FfdCtCommand($Command,[bool] $Source) {
    if ($Command.exitCode -ne 0) { Stop-Ffd 'ct-exit' }
    if ($Source) {
        if ($Command.file -cne 'git' -or -not $Command.Contains('complete') -or -not $Command.complete -or
            $Command.timedOut -or $Command.termination -cne 'not-needed') { Stop-Ffd 'ct-process-incomplete' }
    } elseif ($Command.Contains('timedOut')) {
        if ($Command.timedOut -or -not $Command.Contains('logsComplete') -or -not $Command.logsComplete -or
            $Command.termination -cne 'not-needed') { Stop-Ffd 'ct-process-incomplete' }
    }
}
function Assert-FfdCt([string] $Root, $Budget) {
    $base=Join-Path $Root 'artifacts/validator-metrics/evidence'
    $m=Read-FfdJson "$base/manifest.json" 8MB
    if ($m.schema -cne 'managed-phase-validation-v2' -or $m.status -cne 'focal-C-T-steps-passed-awaiting-independent-review' -or
        $m.toolchain.dotnetSdk -cne '10.0.302') { Stop-Ffd 'ct-incomplete' }
    $names=@('source','toolchain','native-configure','native-build','native-install','native-verify','core-restore','core-build',
        'validator-restore','validator-build','preflight-restore','preflight-build','preflight-run','tests-restore','tests-build','tests-run',
        'export-source','export-baseline-restore','export-baseline-build','export-candidate-restore','export-candidate-build',
        'export-baseline-run','export-candidate-run','export-compare')
    if ($m.stageFiles.Count -ne $names.Count) { Stop-Ffd 'ct-stage-set' }
    for ($i=0;$i -lt $names.Count;$i++) {
        Assert-FfdDeadline $Budget
        $name="stage-$($names[$i]).json"
        if ($m.stageFiles[$i] -cne $name) { Stop-Ffd 'ct-stage-set' }
        $stage=Read-FfdJson "$base/$name" 8MB
        if ($stage.name -cne $names[$i] -or $stage.status -cne 'passed') { Stop-Ffd 'ct-stage-incomplete' }
        foreach ($cmd in $stage.commands) {
            Assert-FfdCtCommand $cmd ($names[$i] -ceq 'source')
        }
    }
    $sourceStage=Read-FfdJson "$base/stage-source.json" 8MB
    if($sourceStage.details.source -cne 'passed' -or $sourceStage.details.tests.schema -cne 'metrics-scope-tests-v1' -or
        $sourceStage.details.tests.status -cne 'passed' -or $sourceStage.details.tests.caseCount -ne $sourceStage.details.tests.cases.Count -or
        $sourceStage.details.tests.caseCount -le 0 -or $sourceStage.details.tests.buildSpy -ne 0){Stop-Ffd 'source-tests-incomplete'}
    foreach($case in $sourceStage.details.tests.cases){if($case.passed -ne $true -or $case.buildSpy -ne 0){Stop-Ffd 'source-tests-incomplete'}}
    Assert-FfdSource $m.source $Root $Budget
    $export=Read-FfdJson "$base/export-candidate.json" 8MB
    if ($export.schema -cne 'managed-results-export-v1' -or $export.status -cne 'passed') { Stop-Ffd 'ct-export' }
    $hashes=[ordered]@{}
    foreach ($name in $script:FfdNames[0..2]) {
        $relative="artifacts/validator-metrics/export-candidate-bin/$name"
        $matches=@($m.build.exportCandidate | Where-Object { $_.path -ceq $relative })
        if ($matches.Count -ne 1) { Stop-Ffd 'ct-binary-set' }
        $id=Get-FfdInputIdentity (Join-Path $Root $relative) $name
        if ($id.sha256 -cne $matches[0].sha256 -or $id.bytes -ne $matches[0].bytes) { Stop-Ffd 'ct-binary-hash' }
        if ($name -ceq 'aether_core.dll') {
            if ($id.sha256 -cne $export.loadedNative.sha256 -or $id.sha256 -cne $m.build.native.sha256) { Stop-Ffd 'ct-loaded-native' }
        } else {
            $loaded=@($export.assemblies | Where-Object { $_.file -ceq $name })
            if ($loaded.Count -ne 1 -or $loaded[0].sha256 -cne $id.sha256) { Stop-Ffd 'ct-loaded-assembly' }
        }
        $hashes[$name]=$id.sha256
    }
    return @{manifest=$m; files=$hashes; stageSource=(Read-FfdJson "$base/stage-source.json" 8MB)}
}
function Get-FfdImportInventory([string] $Text) {
    $settings=[Xml.XmlReaderSettings]::new();$settings.DtdProcessing=[Xml.DtdProcessing]::Prohibit;$settings.XmlResolver=$null
    $input=[IO.StringReader]::new($Text);$reader=[Xml.XmlReader]::Create($input,$settings)
    try{$xml=[Xml.XmlDocument]::new();$xml.XmlResolver=$null;$xml.Load($reader)}catch{Stop-Ffd 'preprocess-xml'}finally{$reader.Dispose();$input.Dispose()}
    if($xml.SelectNodes('//*[local-name()="Import"]').Count -ne 0){Stop-Ffd 'unexpanded-import'}
    $paths=[Collections.Generic.List[string]]::new();$stack=[Collections.Generic.Stack[string]]::new();$opens=0;$rootSeen=$false
    # Inspect EVERY comment, not a regex subset of paths with familiar extensions.
    # MSBuild -pp boundary comments must use this closed grammar; a new/unknown
    # formatter fails closed rather than silently omitting an import from evidence.
    foreach($comment in $xml.SelectNodes('//comment()')){
        $body=$comment.Value
        $hasMarker=$body -match '(?i)<\s*/?\s*Import\b'
        $hasBoundary=$body -match '(?m)^\s*={10,}\s*$'
        if(-not $hasMarker -and -not $hasBoundary){continue}
        $boundary=[regex]::Match($body,'\A\s*={10,}\r?\n(?<body>[\s\S]*?)\r?\n={10,}\s*\z')
        if(-not $boundary.Success){Stop-Ffd 'import-boundary-unrecognized'}
        $inner=$boundary.Groups['body'].Value.Trim();$kind='root';$path=$inner
        if($inner -match '\A</Import>\s*(?<path>[^\r\n]+)\z'){$kind='close';$path=$Matches.path.Trim()}
        elseif($inner.StartsWith('<Import',[StringComparison]::Ordinal)){
            $opening=[regex]::Match($inner,'\A(?<marker><Import\b[\s\S]*?>)\s*(?<path>[^\r\n]+)\z')
            if(-not $opening.Success){Stop-Ffd 'import-boundary-unrecognized'}
            $marker=$opening.Groups['marker'].Value
            if(-not $marker.EndsWith('/>',[StringComparison]::Ordinal)){$marker=$marker.Substring(0,$marker.Length-1)+'/>'}
            try{$element=[Xml.XmlDocument]::new();$element.XmlResolver=$null;$element.LoadXml($marker)}catch{Stop-Ffd 'import-marker-invalid'}
            if($element.DocumentElement.Name -cne 'Import' -or -not $element.DocumentElement.HasAttribute('Project')){Stop-Ffd 'import-marker-invalid'}
            foreach($attribute in $element.DocumentElement.Attributes){if($attribute.Name -cnotin @('Project','Condition','Sdk','Version','MinimumVersion','Label')){Stop-Ffd 'import-marker-invalid'}}
            $kind='open';$path=$opening.Groups['path'].Value.Trim()
        }
        if($path -cnotmatch '^[A-Za-z]:[\\/][^<>:"|?*\r\n]+$' -or $path -match '[$%`]' -or $path.Split([char[]]'\/') -contains '..') { Stop-Ffd 'import-path-invalid' }
        $extension=[IO.Path]::GetExtension($path)
        if($extension -cnotin @('.props','.targets','.csproj')){Stop-Ffd 'import-extension-invalid'}
        if($kind -ceq 'root'){
            if($rootSeen -or $hasMarker){Stop-Ffd 'import-boundary-unrecognized'}
            $rootSeen=$true;$stack.Push($path);$paths.Add($path)
        }elseif($kind -ceq 'open'){
            if(-not $rootSeen){Stop-Ffd 'import-root-missing'}
            $stack.Push($path);$paths.Add($path);$opens++
        }else{
            if($stack.Count -le 1){Stop-Ffd 'import-close-unmatched'}
            $null=$stack.Pop()
            if(-not $path.Equals($stack.Peek(),[StringComparison]::OrdinalIgnoreCase)){Stop-Ffd 'import-parent-mismatch'}
        }
    }
    if(-not $rootSeen -or $opens -eq 0 -or $stack.Count -ne 1){Stop-Ffd 'import-inventory-incomplete'}
    return ,$paths.ToArray()
}
function Assert-FfdImports([string] $Expanded,[string] $Project,[string] $SdkRoot,$Budget) {
    $null=Assert-FfdPlainFile $Expanded 32MB
    $text=$script:FfdUtf8.GetString([IO.File]::ReadAllBytes($Expanded))
    $paths=Get-FfdImportInventory $text
    $sdk=[IO.Path]::GetFullPath($SdkRoot).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    $projectPath=[IO.Path]::GetFullPath($Project)
    $obj=[IO.Path]::GetFullPath((Join-Path (Split-Path $Project) 'obj'))
    $generated=@([IO.Path]::Combine($obj,'AetherUI.Validator.HeadlessDiagnostics.csproj.nuget.g.props'),[IO.Path]::Combine($obj,'AetherUI.Validator.HeadlessDiagnostics.csproj.nuget.g.targets'))
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $identities=[Collections.Generic.List[object]]::new();$projectSeen=$false
    foreach($importPath in $paths){
        Assert-FfdDeadline $Budget
        $path=[IO.Path]::GetFullPath($importPath)
        if(-not $seen.Add($path)){continue}
        $label=$null
        if($path.Equals($projectPath,[StringComparison]::OrdinalIgnoreCase)){$label='probe.csproj';$projectSeen=$true}
        elseif($path.StartsWith($sdk,[StringComparison]::OrdinalIgnoreCase)){$label='sdk/'+[IO.Path]::GetRelativePath($sdk,$path).Replace('\','/')}
        elseif($path -iin $generated){$label='obj/'+[IO.Path]::GetFileName($path)}
        else{Stop-Ffd 'external-msbuild-import'}
        $identities.Add((Get-FfdInputIdentity $path $label))
    }
    if(-not $projectSeen -or $identities.Count -lt 5){Stop-Ffd 'import-inventory-incomplete'}
    return @{expanded=(Get-FfdInputIdentity $Expanded 'preprocessed-probe.xml');imports=$identities.ToArray();
        scope='MSBuild preprocessing of only the probe; imported files limited to official SDK 10.0.302 and generated package-free probe obj files.'}
}
function Assert-FfdDependencies([string] $ProjectDirectory, [string] $Output, $ExpectedFiles, $Budget) {
    Assert-FfdDeadline $Budget
    $assetsPath=Join-Path $ProjectDirectory 'obj/project.assets.json'
    $assets=Read-FfdJson $assetsPath 8MB
    if ($assets.libraries.Count -ne 0) { Stop-Ffd 'dependency-package-or-project' }
    foreach ($framework in $assets.project.frameworks.Values) {
        if ($framework.Contains('dependencies') -and $framework.dependencies.Count -ne 0) { Stop-Ffd 'dependency-package' }
        foreach ($ref in $framework.frameworkReferences.Keys) {
            if ($ref -cne 'Microsoft.NETCore.App') { Stop-Ffd 'dependency-framework' }
        }
    }
    foreach ($restoreFramework in $assets.project.restore.frameworks.Values) {
        if ($restoreFramework.projectReferences.Count -ne 0) { Stop-Ffd 'dependency-project' }
    }
    $runtimePath=Join-Path $Output 'AetherUI.Validator.HeadlessDiagnostics.runtimeconfig.json'
    $runtime=Read-FfdJson $runtimePath
    if ($runtime.runtimeOptions.tfm -cne 'net10.0' -or $runtime.runtimeOptions.framework.name -cne 'Microsoft.NETCore.App' -or
        $runtime.runtimeOptions.framework.version -cnotmatch '^10\.0\.') { Stop-Ffd 'runtime-framework' }
    $depsPath=Join-Path $Output 'AetherUI.Validator.HeadlessDiagnostics.deps.json'
    $deps=Read-FfdJson $depsPath
    $allowed=@('AetherUI.Validator.HeadlessDiagnostics','AetherUI.Core','AetherUI.Core.Native.Interop')
    foreach ($key in $deps.libraries.Keys) {
        if ($key.Split('/')[0] -cnotin $allowed -or $deps.libraries[$key].type -cnotin @('project','reference')) { Stop-Ffd 'dependency-closure' }
    }
    $actual=[ordered]@{}
    foreach ($name in $script:FfdNames) {
        Assert-FfdDeadline $Budget
        $id=Get-FfdInputIdentity (Join-Path $Output $name) $name
        if ($ExpectedFiles.Contains($name) -and $id.sha256 -cne $ExpectedFiles[$name]) { Stop-Ffd 'copied-binary-identity' }
        $actual[$name]=$id.sha256
    }
    # No Windows-specific or arbitrary runtime DLL is allowed alongside this host.
    foreach ($item in Get-ChildItem -LiteralPath $Output -File) {
        if ($item.Extension -ceq '.dll' -and $item.Name -cnotin $script:FfdNames) { Stop-Ffd 'unexpected-output-assembly' }
    }
    return @{files=$actual; evidence=[ordered]@{schema='headless-dependencies-v1'; framework='Microsoft.NETCore.App';
        assets=(Get-FfdInputIdentity $assetsPath 'project.assets.json'); runtimeconfig=(Get-FfdInputIdentity $runtimePath 'probe.runtimeconfig.json');
        deps=(Get-FfdInputIdentity $depsPath 'probe.deps.json'); libraries=@($deps.libraries.Keys); files=$actual;
        projectReferences=0; packages=0; buildScope='Probe only; binary references to verified current C/T candidate.'}}
}
function Invoke-FfdMain([string] $Root, [string] $AnchorPath) {
    $stepWatch=[Diagnostics.Stopwatch]::StartNew()
    $stepClock={ $stepWatch.Elapsed.TotalMilliseconds }.GetNewClosure()
    $rootPath=[IO.Path]::GetFullPath($Root)
    $public=Join-Path $rootPath 'artifacts/validator-headless-diagnostics/evidence'
    $output=Join-Path $rootPath 'artifacts/validator-headless-diagnostics/bin'
    $temp=Join-Path $env:RUNNER_TEMP 'aether-headless-diagnostics-private'
    if (Test-Path -LiteralPath $public) { Stop-Ffd 'evidence-exists' }
    if (Test-Path -LiteralPath $temp) { Stop-Ffd 'private-evidence-exists' }
    $null=New-Item -ItemType Directory -Path $public,$temp
    $summary=[ordered]@{schema='headless-summary-v1';status='incomplete';phase='admission';code=$null;statistics=$null;
        upload='not-observed-by-producer'; fullStream='declared-full-stream-sha256; stream-not-retained';
        interpretation='Synthetic first load; approximate text, unresolved images, projected managed boxes only. Spans are not a CPU partition; no visual conformity or performance comparison.';
        budget='900 seconds from first-step TickCount64, not actual job start; external timeout/upload completion not guaranteed.'}
    Write-FfdJson "$public/summary.json" $summary
    if (-not [string]::IsNullOrEmpty($env:GITHUB_ENV)) { [IO.File]::AppendAllText($env:GITHUB_ENV,"AETHER_HEADLESS_EVIDENCE_INITIALIZED=true`n",$script:FfdUtf8) }
    $processes=[Collections.Generic.List[object]]::new(); $results=[Collections.Generic.List[object]]::new(); $budget=$null
    try {
        if ([string]::IsNullOrEmpty($AnchorPath) -or [IO.Path]::GetFullPath($AnchorPath) -cne
            [IO.Path]::GetFullPath((Join-Path $env:RUNNER_TEMP 'aether-headless-budget-anchor.json'))) { Stop-Ffd 'budget-anchor-invalid' }
        $budget=Read-FfdBudgetAnchor $AnchorPath $env:GITHUB_RUN_ID $env:GITHUB_RUN_ATTEMPT $env:GITHUB_JOB -Local $stepClock
        Start-FfdPhase $budget 'verification' 30 540
        $summary.phase='verification'; Write-FfdJson "$public/summary.json" $summary
        if (-not $IsWindows -or [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -ne 'X64') { Stop-Ffd 'platform' }
        $ct=Assert-FfdCt $rootPath $budget
        Write-FfdJson "$public/source-manifest.json" $ct.manifest.source
        Write-FfdJson "$public/stage-source.json" $ct.stageSource
        $fixture=Join-Path $rootPath $script:FfdFixture
        $fixtureFile=Assert-FfdPlainFile $fixture 474495
        if ($fixtureFile.Length -ne 474495 -or (Get-FfdHash $fixture) -cne $script:FfdFixtureHash) { Stop-Ffd 'fixture-identity' }
        $null=$script:FfdUtf8.GetString([IO.File]::ReadAllBytes($fixture))
        $dotnetCommands=@(Get-Command dotnet -CommandType Application -ErrorAction Stop)
        if ($dotnetCommands.Count -eq 0) { Stop-Ffd 'dotnet-missing' }
        $dotnet=[IO.Path]::GetFullPath($dotnetCommands[0].Source)
        $expectedHost=[IO.Path]::GetFullPath((Join-Path $env:DOTNET_ROOT 'dotnet.exe'))
        if (-not $dotnet.Equals($expectedHost,[StringComparison]::OrdinalIgnoreCase)) { Stop-Ffd 'dotnet-host' }
        $versionProcess=Invoke-FfdProcess $dotnet @('--version') $rootPath "$temp/sdk-version" 5 1 $budget
        Assert-FfdProcess $versionProcess
        $version=$script:FfdUtf8.GetString([IO.File]::ReadAllBytes("$temp/sdk-version.stdout")).Trim()
        if ($version -cne '10.0.302') { Stop-Ffd 'sdk-version' }
        $ids=[ordered]@{schema='headless-identities-v1';sourceCommit=$ct.manifest.source.commit;sourceTree=$ct.manifest.source.tree;
            fixture=[ordered]@{path=$script:FfdFixture;bytes=474495;sha256=$script:FfdFixtureHash};
            sdk=$version;runtimeHost=(Get-FfdInputIdentity $dotnet 'dotnet.exe'); configuration='Release';rid='win-x64';
            os=[Runtime.InteropServices.RuntimeInformation]::OSDescription;architecture='X64';logicalCpuCount=[Environment]::ProcessorCount;
            cpuModel=$null;cpuModelAvailability='not-collected';budget=$summary.budget;ctStatus=$ct.manifest.status}
        Write-FfdJson "$public/identities.json" $ids
        $project=Join-Path $rootPath "$script:FfdTool/AetherUI.Validator.HeadlessDiagnostics.csproj"
        $candidate=Join-Path $rootPath 'artifacts/validator-metrics/export-candidate-bin'
        $props=@("-p:CandidateBin=$candidate",'-p:SelfContained=false','-p:ImportDirectoryBuildProps=false','-p:ImportDirectoryBuildTargets=false','-p:ImportDirectoryPackagesProps=false')
        Start-FfdPhase $budget 'restore' 60 510
        $summary.phase='restore'; Write-FfdJson "$public/summary.json" $summary
        $restore=Invoke-FfdProcess $dotnet (@('restore',$project,'-r','win-x64')+$props) $rootPath "$temp/restore" 55 5 $budget
        $processes.Add(@{name='restore';process=$restore}); Write-FfdJson "$public/process-results.json" @($processes)
        Assert-FfdProcess $restore
        Start-FfdPhase $budget 'build' 90 450
        $summary.phase='build'; Write-FfdJson "$public/summary.json" $summary
        $expanded=Join-Path $temp 'preprocessed-probe.xml'
        $preprocess=Invoke-FfdProcess $dotnet (@('msbuild',$project,'-nologo',"-pp:$expanded",'-p:Configuration=Release','-p:RuntimeIdentifier=win-x64')+$props) $rootPath "$temp/preprocess" 5 1 $budget
        $processes.Add(@{name='preprocess';process=$preprocess});Assert-FfdProcess $preprocess
        $imports=Assert-FfdImports $expanded $project (Join-Path $env:DOTNET_ROOT 'sdk/10.0.302') $budget
        $build=Invoke-FfdProcess $dotnet (@('build',$project,'--no-restore','-c','Release','-r','win-x64','-o',$output)+$props) $rootPath "$temp/build" 75 5 $budget
        $processes.Add(@{name='build';process=$build}); Write-FfdJson "$public/process-results.json" @($processes)
        Assert-FfdProcess $build
        $dependencies=Assert-FfdDependencies (Split-Path $project) $output $ct.files $budget
        $dependencies.evidence['imports']=$imports
        Write-FfdJson "$public/dependencies.json" $dependencies.evidence
        # Rehash the original outputs as well: a matching copied file alone is insufficient.
        foreach ($name in $ct.files.Keys) { if ((Get-FfdHash (Join-Path $candidate $name)) -cne $ct.files[$name]) { Stop-Ffd 'candidate-changed' } }
        $manifest=[ordered]@{schema='headless-input-v1';sourceCommit=$ct.manifest.source.commit;sourceTree=$ct.manifest.source.tree;files=$dependencies.files}
        Write-FfdJson "$temp/input.json" $manifest
        Start-FfdPhase $budget 'contracts' 45 360
        $summary.phase='contracts'; Write-FfdJson "$public/summary.json" $summary
        . (Join-Path $PSScriptRoot 'Contract.Tests.ps1')
        $contracts=Invoke-FfdContracts -Dotnet $dotnet -Probe (Join-Path $output $script:FfdNames[3]) -Root $rootPath -Private $temp -Public $public -Budget $budget
        Write-FfdJson "$public/contract-tests.json" $contracts
        if ($contracts.status -cne 'passed') { Stop-Ffd 'contract-tests-failed' }
        Start-FfdPhase $budget 'samples' 240 315
        $summary.phase='samples'; Write-FfdJson "$public/summary.json" $summary
        foreach ($session in @('warmup','sample-1','sample-2','sample-3','sample-4','sample-5')) {
            Assert-FfdDeadline $budget
            $progress={param($record) Write-FfdJson "$public/process-results.json" @{processes=@($processes);active=@{session=$session;process=$record}}}.GetNewClosure()
            $record=Invoke-FfdProcess $dotnet @((Join-Path $output $script:FfdNames[3]),'--manifest',"$temp/input.json",'--fixture',$fixture,'--session',$session,'--output',"$temp/$session.result.json") $rootPath "$temp/$session" 30 5 $budget $progress
            $processes.Add(@{name=$session;process=$record}); Write-FfdJson "$public/process-results.json" @{processes=@($processes);active=$null}
            $partial=Save-FfdPartialEvents "$temp/$session.stdout" "$public/$session.events.jsonl" $session
            $record['lastCompleteEvent']=$partial.lastCompleteEvent; $record['eventStreamComplete']=$partial.complete
            $sample=Read-FfdSample $record "$temp/$session.stdout" "$temp/$session.result.json" $session $manifest
            $events=$sample.events;$result=$sample.result
            # The parser and result checks must complete before any process data is public.
            Write-FfdJson "$public/$session.result.json" $result
            $lines=($events | ForEach-Object { $_ | ConvertTo-Json -Compress -Depth 64 }) -join "`n"
            [IO.File]::WriteAllText("$public/$session.events.jsonl",$lines+"`n",$script:FfdUtf8)
            $record['reportingMs']=$events[-1].data.reportingMs
            $results.Add($result)
        }
        Start-FfdPhase $budget 'finalize' 15 75
        $summary.phase='finalize'; Write-FfdJson "$public/summary.json" $summary
        Assert-FfdSource $ct.manifest.source $rootPath $budget
        if ((Get-FfdHash $fixture) -cne $script:FfdFixtureHash) { Stop-Ffd 'fixture-changed' }
        foreach ($name in $script:FfdNames) {
            if ((Get-FfdHash (Join-Path $output $name)) -cne $manifest.files[$name]) { Stop-Ffd 'output-changed' }
        }
        $null=Get-FfdAggregate $results.ToArray()
        Write-FfdJson "$public/witnesses.json" @{schema='WITNESS-1';processes=@($results | ForEach-Object { @{session=$_.session;witnesses=$_.witnesses} })}
        $total=0L
        foreach ($item in Get-ChildItem -LiteralPath $public -File) { $total+=$item.Length }
        if ($total -gt 32MB) { Stop-Ffd 'public-byte-limit' }
        Assert-FfdDeadline $budget
        Complete-FfdSummary $summary $results.ToArray() $budget
    } catch {
        $summary.status='incomplete'; $summary.code=Get-FfdCode $_
        Write-FfdJson "$public/summary.json" $summary
        throw [InvalidOperationException]::new("Headless diagnostic incomplete: $($summary.code)")
    } finally {
        Write-FfdJson "$public/process-results.json" @{processes=@($processes);active=$null}
        Write-FfdJson "$public/summary.json" $summary
    }
}
if (-not $LibraryOnly) {
    if ([string]::IsNullOrEmpty($Root)) { $Root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..')) }
    Invoke-FfdMain $Root $AnchorPath
}
