# Tests of the production policy functions. No build, browser, engine, network or
# product-worktree mutation. This suite never replaces the candidate source gate.
function Copy-ScopeTestMap($Source) {
    $copy = New-ScopeMap
    foreach ($key in $Source.Keys) {
        $value = $Source[$key]
        if ($value -is [byte[]]) { $copy.Add($key, $value.Clone()) }
        else {
            $entry = [ordered]@{}
            foreach ($field in $value.Keys) { $entry[$field] = $value[$field] }
            $copy.Add($key, $entry)
        }
    }
    return ,$copy
}
function Invoke-ScopeTestCase($State, [string] $Name, [string] $ExpectedCode, [scriptblock] $Action) {
    Assert-ScopeTime $State.Context
    $spy = @{ builds = 0 }; $caught = $null
    $buildCallback = { $spy.builds++ }.GetNewClosure()
    try {
        $null = & $Action
        if ($ExpectedCode.Length -gt 0) { & $buildCallback }
    } catch { $caught = $_.Exception.Message }
    if ($ExpectedCode.Length -eq 0) {
        if ($null -ne $caught) { throw "scope-tests:unexpected:$Name" }
    } elseif (-not (Test-ScopeEqual $caught "scope:$ExpectedCode") -or $spy.builds -ne 0) {
        throw "scope-tests:negative:$Name"
    }
    $State.Cases.Add([ordered]@{ name = $Name; expected = $ExpectedCode; passed = $true; buildSpy = $spy.builds })
    Assert-ScopeTime $State.Context
}
function New-ScopeTestBatch([string] $Id, [byte[]] $Blob) {
    $stream = [IO.MemoryStream]::new()
    try {
        $header = [Text.Encoding]::ASCII.GetBytes("$Id blob $($Blob.Length)`n")
        $stream.Write($header, 0, $header.Length); $stream.Write($Blob, 0, $Blob.Length); $stream.WriteByte(10)
        return ,$stream.ToArray()
    } finally { $stream.Dispose() }
}
function Write-ScopeTestBytes([string] $Root, [string] $Path, [byte[]] $Bytes) {
    $destination = Join-Path $Root $Path
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
    [IO.File]::WriteAllBytes($destination, $Bytes)
}
function Write-ScopeTestOwned([string] $Root, $Owned) {
    foreach ($path in $Owned.Keys) { Write-ScopeTestBytes $Root $path $Owned[$path] }
}
function Invoke-ScopeTestPipe([byte[]] $Bytes, [long] $Limit, [string] $LimitCode, $Context) {
    $pipeSource = [IO.MemoryStream]::new($Bytes, $false); $output = [IO.MemoryStream]::new()
    $buffer = [byte[]]::new(3); $eof = $false
    try {
        $task = $pipeSource.ReadAsync($buffer, 0, $buffer.Length)
        while (-not $eof) {
            Assert-ScopeTime $Context
            Receive-ScopePipe $pipeSource $output $buffer ([ref]$task) ([ref]$eof) $Limit $LimitCode
        }
        if (-not (Test-ScopeBytes $output.ToArray() $Bytes)) { Stop-Scope 'test-pipe' }
    } finally { $pipeSource.Dispose(); $output.Dispose() }
}
function Invoke-ScopeTests($Product, [string] $Root, $EvidenceStage) {
    if ($null -eq $EvidenceStage) { throw 'scope-tests:evidence-stage' }
    $integration = $null; $fixtureRoot = $null
    try {
    $allClock = [Diagnostics.Stopwatch]::StartNew()
    $pure = New-ScopeContext $Root 120
    $state = @{ Context = $pure; Cases = [Collections.Generic.List[object]]::new() }
    $ascii = [Text.Encoding]::ASCII
    $zero = '0000000000000000000000000000000000000000'
    $one = '1111111111111111111111111111111111111111'
    $entry = [ordered]@{ path = 'sample.cs'; mode = '100644'; type = 'blob'; blob = $one }
    $single = New-ScopeMap; $single.Add('sample.cs', $entry)
    Invoke-ScopeTestCase $state 'inventory-positive-full' '' { Assert-ScopeInventory $Product.expected $Product.expected }
    # Exercise every reviewed path, not just extensions or build globs. Small
    # independent maps test the same comparator without quadratic fixture copying.
    foreach ($path in $Product.expected.Keys) {
        $expected = New-ScopeMap; $expected.Add($path, $Product.expected[$path])
        foreach ($field in @('blob','mode','type','path')) {
            $actual = Copy-ScopeTestMap $expected
            $actual[$path][$field] = switch ($field) { 'blob' { $zero }; 'mode' { '100755' }; 'type' { 'commit' }; 'path' { "$path.other" } }
            Invoke-ScopeTestCase $state "entry-$field-$path" 'inventory' { Assert-ScopeInventory $expected $actual }
        }
        $empty = New-ScopeMap
        Invoke-ScopeTestCase $state "delete-$path" 'inventory' { Assert-ScopeInventory $expected $empty }
    }
    foreach ($path in @('native/extra.cpp','global.extra.json','bindings/Directory.Build.props',
        'bindings/dotnet/src/AetherUI.Core/New.cs','bindings/dotnet/src/AetherUI.Windows/New.cs',
        'bindings/dotnet/tests/AetherUI.Metrics.Export/New.cs','NuGet.Config','outside.txt')) {
        $actual = Copy-ScopeTestMap $Product.expected
        $actual.Add($path, [ordered]@{path=$path;mode='100644';type='blob';blob=$one})
        Invoke-ScopeTestCase $state "addition-$path" 'inventory' { Assert-ScopeInventory $Product.expected $actual }
    }
    $renamed = Copy-ScopeTestMap $single; $null = $renamed.Remove('sample.cs')
    $renamed.Add('Sample.cs', [ordered]@{path='Sample.cs';mode='100644';type='blob';blob=$one})
    Invoke-ScopeTestCase $state 'case-only-rename' 'inventory' { Assert-ScopeInventory $single $renamed }
    foreach ($header in @("100755 blob $one", "120000 blob $one", "160000 commit $one", "100644 tree $one", "100644 blob $($one.ToUpperInvariant()) extra")) {
        $bytes = $ascii.GetBytes("$header`tsample.cs`0")
        Invoke-ScopeTestCase $state "tree-header-$header" 'entry-format' { ConvertFrom-ScopeTree $bytes }
    }
    foreach ($path in @('/a','a//b','a/../b','a/./b','a\b','a:b','a<b','a>b','a"b','a|b','a?b','a*b',
        'a.','a ','CON','con.txt','PRN','AUX.x','NUL','COM1.x','Lpt9.ext',"a`tname", "a`nname", [string][char]233, ('x' * 256))) {
        Invoke-ScopeTestCase $state "path-$($state.Cases.Count)" 'path' { Assert-ScopePath $path }
    }
    Invoke-ScopeTestCase $state 'path-internal-space-positive' '' { Assert-ScopePath 'folder name/a file.cs' }
    Invoke-ScopeTestCase $state 'path-length' 'path' { Assert-ScopePath (('a/' * 513) + 'a') }
    $good = $ascii.GetBytes("100644 blob $one`tsample.cs`0")
    Invoke-ScopeTestCase $state 'tree-binary-positive' '' { Assert-ScopeInventory $single (ConvertFrom-ScopeTree $good) }
    Invoke-ScopeTestCase $state 'tree-nul-truncated' 'nul-eof' { ConvertFrom-ScopeTree $good[0..($good.Length-2)] }
    Invoke-ScopeTestCase $state 'tree-empty-record' 'empty-record' { ConvertFrom-ScopeTree ([byte[]]($good + 0)) }
    Invoke-ScopeTestCase $state 'tree-duplicate' 'path-collision' { ConvertFrom-ScopeTree ([byte[]]($good + $good)) }
    $alias = $ascii.GetBytes("100644 blob $one`tSample.cs`0")
    Invoke-ScopeTestCase $state 'tree-case-collision' 'path-collision' { ConvertFrom-ScopeTree ([byte[]]($good + $alias)) }
    Invoke-ScopeTestCase $state 'tree-invalid-utf8' 'utf8' { Get-ScopeRecords ([byte[]]@(255,0)) }
    Invoke-ScopeTestCase $state 'metadata-limit' 'metadata-limit' { Get-ScopeRecords ([byte[]]::new(8MB+1)) }
    Invoke-ScopeTestCase $state 'entry-limit' 'entry-limit' { Get-ScopeRecords ($ascii.GetBytes(('x'+[char]0)*2049)) }
    Invoke-ScopeTestCase $state 'index-positive' '' { Assert-ScopeInventory $single (ConvertFrom-ScopeTree ($ascii.GetBytes("100644 $one 0`tsample.cs`0")) $true) }
    foreach ($stage in @('1','2','3')) {
        Invoke-ScopeTestCase $state "index-unmerged-$stage" 'entry-format' { ConvertFrom-ScopeTree ($ascii.GetBytes("100644 $one $stage`tsample.cs`0")) $true }
    }
    foreach ($flag in @('h','S','s','M','R')) {
        Invoke-ScopeTestCase $state "index-flag-$flag" 'index-flag' { Assert-ScopeFlags ($ascii.GetBytes("$flag sample.cs`0")) $single }
    }
    Invoke-ScopeTestCase $state 'index-normal-H' '' { Assert-ScopeFlags ($ascii.GetBytes("H sample.cs`0")) $single }
    Invoke-ScopeTestCase $state 'index-flags-missing' 'index-flag' { Assert-ScopeFlags ([byte[]]@()) $single }
    $binary = [byte[]]@(0,10,13,255,65,0,10)
    Invoke-ScopeTestCase $state 'pipe-binary-memory-stream' '' { Invoke-ScopeTestPipe $binary 100 'git-output-limit' $pure }
    foreach ($limitCode in @('git-output-limit','git-error-limit','physical-size')) {
        Invoke-ScopeTestCase $state "pipe-limit-$limitCode" $limitCode { Invoke-ScopeTestPipe $binary 6 $limitCode $pure }
    }
    $pipeInput = [IO.MemoryStream]::new($binary, $false); $pipeOutput = [IO.MemoryStream]::new()
    $pipeBuffer = [byte[]]::new(8); $pipeEof = $false
    try {
        $pendingRead = [Threading.Tasks.TaskCompletionSource[int]]::new(); $pipeTask = $pendingRead.Task
        Invoke-ScopeTestCase $state 'pipe-pending-does-not-block' '' {
            Receive-ScopePipe $pipeInput $pipeOutput $pipeBuffer ([ref]$pipeTask) ([ref]$pipeEof) 100 'git-output-limit'
            if ($pipeOutput.Length -ne 0 -or $pipeEof) { Stop-Scope 'test-pipe' }
        }
        $pendingRead.SetException([IO.IOException]::new('Synthetic read failure.'))
        Invoke-ScopeTestCase $state 'pipe-read-failure' 'pipe-read' {
            Receive-ScopePipe $pipeInput $pipeOutput $pipeBuffer ([ref]$pipeTask) ([ref]$pipeEof) 100 'git-output-limit'
        }
    } finally { $pipeInput.Dispose(); $pipeOutput.Dispose() }
    $binaryId = Get-ScopeBlobId $binary
    $batch = New-ScopeTestBatch $binaryId $binary
    Invoke-ScopeTestCase $state 'batch-binary-positive' '' {
        $decoded = ConvertFrom-ScopeBatch $batch @($binaryId) $pure
        if (-not (Test-ScopeBytes $decoded[$binaryId] $binary)) { Stop-Scope 'test-binary' }
    }
    Invoke-ScopeTestCase $state 'batch-truncated' 'batch-truncated' { ConvertFrom-ScopeBatch $batch[0..($batch.Length-2)] @($binaryId) $pure }
    Invoke-ScopeTestCase $state 'batch-extra' 'batch-eof' { ConvertFrom-ScopeBatch ([byte[]]($batch + 10)) @($binaryId) $pure }
    $badDelimiter = $batch.Clone(); $badDelimiter[$badDelimiter.Length-1] = 0
    Invoke-ScopeTestCase $state 'batch-delimiter' 'batch-delimiter' { ConvertFrom-ScopeBatch $badDelimiter @($binaryId) $pure }
    Invoke-ScopeTestCase $state 'batch-wrong-order' 'batch-header' { ConvertFrom-ScopeBatch $batch @($zero) $pure }
    Invoke-ScopeTestCase $state 'batch-missing' 'batch-header' { ConvertFrom-ScopeBatch ($ascii.GetBytes("$binaryId missing`n")) @($binaryId) $pure }
    Invoke-ScopeTestCase $state 'batch-size-limit' 'blob-limit' { ConvertFrom-ScopeBatch ($ascii.GetBytes("$binaryId blob 2097153`n")) @($binaryId) $pure }
    Invoke-ScopeTestCase $state 'batch-type' 'batch-header' { ConvertFrom-ScopeBatch ($ascii.GetBytes("$binaryId tree 0`n`n")) @($binaryId) $pure }
    Invoke-ScopeTestCase $state 'batch-header-eof' 'batch-header' { ConvertFrom-ScopeBatch ($ascii.GetBytes($binaryId)) @($binaryId) $pure }
    $wrongBlob = $binary.Clone(); $wrongBlob[0] = 1
    Invoke-ScopeTestCase $state 'batch-blob-sha1' 'blob-identity' { ConvertFrom-ScopeBatch (New-ScopeTestBatch $binaryId $wrongBlob) @($binaryId) $pure }
    Invoke-ScopeTestCase $state 'batch-duplicate' 'batch-duplicate' { ConvertFrom-ScopeBatch ([byte[]]($batch + $batch)) @($binaryId,$binaryId) $pure }
    $lf = $ascii.GetBytes("a`nb`n"); $crlf = $ascii.GetBytes("a`r`nb`r`n"); $mixed = $ascii.GetBytes("a`r`nb`n")
    foreach ($extension in @('.ps1','.sln','.cmd')) {
        Invoke-ScopeTestCase $state "eol-lf-$extension" '' { Assert-ScopePhysicalBytes "a$extension" $lf $lf }
        Invoke-ScopeTestCase $state "eol-crlf-$extension" '' { Assert-ScopePhysicalBytes "a$extension" $lf $crlf }
        Invoke-ScopeTestCase $state "eol-mixed-$extension" 'physical-bytes' { Assert-ScopePhysicalBytes "a$extension" $lf $mixed }
        Invoke-ScopeTestCase $state "eol-bom-$extension" 'physical-bytes' { Assert-ScopePhysicalBytes "a$extension" $lf ([byte[]](@(239,187,191)+$lf)) }
    }
    foreach ($path in @('a.cs','a.PS1','fixture.html','image.png','image.ico')) {
        Invoke-ScopeTestCase $state "eol-strict-$path" 'physical-bytes' { Assert-ScopePhysicalBytes $path $lf $crlf }
    }
    $existingCr = $ascii.GetBytes("a`r`nb`n")
    Invoke-ScopeTestCase $state 'eol-existing-cr-preserved' '' { Assert-ScopePhysicalBytes 'a.ps1' $existingCr $crlf }
    $manifest = [ordered]@{status='incomplete';source=$null}
    $stage = [ordered]@{name='source';status='running';commands=@();details=$null}
    $owned = Get-ScopeOwned $manifest $stage
    Invoke-ScopeTestCase $state 'owned-snapshot' '' {
        $expectedBytes = [Text.UTF8Encoding]::new($false).GetBytes(($manifest | ConvertTo-Json -Depth 40)+[Environment]::NewLine)
        if (-not (Test-ScopeBytes $expectedBytes $owned['artifacts/validator-metrics/evidence/manifest.json'])) { Stop-Scope 'test-owned' }
    }
    $stage.commands = @('unexpected')
    Invoke-ScopeTestCase $state 'owned-commands-state' 'owned-state' { Get-ScopeOwned $manifest $stage }
    $stage.commands = @(); $stage.status = 'passed'
    Invoke-ScopeTestCase $state 'owned-running-state' 'owned-state' { Get-ScopeOwned $manifest $stage }
    $stage.status = 'running'; $stage.name = 'other'
    Invoke-ScopeTestCase $state 'owned-name-state' 'owned-state' { Get-ScopeOwned $manifest $stage }
    $stage.name = 'source'; $stage.details = @{}
    Invoke-ScopeTestCase $state 'owned-details-state' 'owned-state' { Get-ScopeOwned $manifest $stage }
    $stage.details = $null; $manifest.source = @{}
    Invoke-ScopeTestCase $state 'owned-source-state' 'owned-state' { Get-ScopeOwned $manifest $stage }
    $manifest.source = $null; $manifest.status = 'passed'
    Invoke-ScopeTestCase $state 'owned-manifest-state' 'owned-state' { Get-ScopeOwned $manifest $stage }
    Invoke-ScopeTestCase $state 'overlay-positive-full' '' { Get-ScopeExpected $Product.trees['M'] $Product.trees['C'] $Product.head $Product.policy }
    Invoke-ScopeTestCase $state 'pinned-hashes-positive' '' { Assert-ScopeHashes $Product.expected $Product.blobs $Product.policy }
    for ($i = 0; $i -lt $Product.policy.overlay.Count; $i++) {
        $policy = Get-ScopePolicy
        $policy.overlay[$i].status = if (Test-ScopeEqual $policy.overlay[$i].status 'A') { 'M' } else { 'A' }
        Invoke-ScopeTestCase $state "overlay-status-$i" 'overlay-status' { Get-ScopeExpected $Product.trees['M'] $Product.trees['C'] $Product.head $policy }
        foreach ($field in @('blob','mode','type')) {
            $capture = Copy-ScopeTestMap $Product.trees['C']; $path = $Product.policy.overlay[$i].path
            $capture[$path][$field] = switch ($field) { 'blob' { $zero }; 'mode' { '100755' }; 'type' { 'tree' } }
            Invoke-ScopeTestCase $state "overlay-capture-$field-$i" 'inventory' { Get-ScopeExpected $Product.trees['M'] $capture $Product.head $Product.policy }
        }
        $policy = Get-ScopePolicy; $policy.overlay[$i].sha256 = '0' * 64
        Invoke-ScopeTestCase $state "overlay-hash-$i" 'pinned-hash' { Assert-ScopeHashes $Product.expected $Product.blobs $policy }
        $policy = Get-ScopePolicy; $policy.overlay[$i].after.mode = '120000'
        Invoke-ScopeTestCase $state "overlay-mode-$i" 'overlay-after' { Get-ScopeExpected $Product.trees['M'] $Product.trees['C'] $Product.head $policy }
        if ($null -ne $Product.policy.overlay[$i].before) {
            $policy = Get-ScopePolicy; $policy.overlay[$i].before.blob = $zero
            Invoke-ScopeTestCase $state "overlay-before-$i" 'overlay-before' { Get-ScopeExpected $Product.trees['M'] $Product.trees['C'] $Product.head $policy }
        }
    }
    for ($i = 0; $i -lt 2; $i++) {
        $policy = Get-ScopePolicy; $policy.controls[$i].sha256 = '0' * 64
        Invoke-ScopeTestCase $state "control-hash-$i" 'pinned-hash' { Assert-ScopeHashes $Product.expected $Product.blobs $policy }
        $policy = Get-ScopePolicy; $policy.controls[$i].blob = $zero
        Invoke-ScopeTestCase $state "control-blob-$i" 'inventory' { Get-ScopeExpected $Product.trees['M'] $Product.trees['C'] $Product.head $policy }
        $policy = Get-ScopePolicy; $policy.controls[$i].blob = 'not-a-pin'
        Invoke-ScopeTestCase $state "control-format-$i" 'control-pin' { Get-ScopeExpected $Product.trees['M'] $Product.trees['C'] $Product.head $policy }
    }
    $policy = Get-ScopePolicy; $policy.overlay[1] = $policy.overlay[0]
    Invoke-ScopeTestCase $state 'overlay-duplicate' 'overlay-duplicate' { Get-ScopeExpected $Product.trees['M'] $Product.trees['C'] $Product.head $policy }
    $head = Copy-ScopeTestMap $Product.head; $head[$Product.policy.helper].mode = '120000'
    Invoke-ScopeTestCase $state 'helper-mode' 'helper-entry' { Get-ScopeExpected $Product.trees['M'] $Product.trees['C'] $head $Product.policy }
    Invoke-ScopeTestCase $state 'history-positive' '' { Assert-ScopeHistory $Product.trees['B'] $Product.trees['P'] $Product.trees['M'] }
    $metrics = Copy-ScopeTestMap $Product.trees['M']; $metrics['global.json'].blob = $zero
    Invoke-ScopeTestCase $state 'history-baseline-protected' 'baseline-production' { Assert-ScopeHistory $Product.trees['B'] $Product.trees['P'] $metrics }
    $metrics = Copy-ScopeTestMap $Product.trees['M']; $metrics.Add('unreviewed.txt', $entry)
    Invoke-ScopeTestCase $state 'history-P-extra' 'prior-delta' { Assert-ScopeHistory $Product.trees['B'] $Product.trees['P'] $metrics }
    $expired = New-ScopeContext $Root 0
    Invoke-ScopeTestCase $state 'source-budget-fail-closed' 'budget' { Assert-ScopeTime $expired }
    $writeCompletion = [Threading.Tasks.TaskCompletionSource[int]]::new()
    $writeCompletion.SetResult(17)
    Invoke-ScopeTestCase $state 'write-completion-emits-nothing' '' {
        $emitted = @(Complete-ScopeWrite $writeCompletion.Task)
        if ($emitted.Count -ne 0) { Stop-Scope 'test-write-output' }
    }
    $failedWrite = [Threading.Tasks.TaskCompletionSource[int]]::new()
    $failedWrite.SetException([IO.IOException]::new('Synthetic write failure.'))
    Invoke-ScopeTestCase $state 'write-completion-fails-closed' 'pipe-write' { Complete-ScopeWrite $failedWrite.Task }
    $oidBytes = $ascii.GetBytes("$one`n")
    Invoke-ScopeTestCase $state 'oid-bytes-positive' '' {
        if (-not (Test-ScopeEqual (ConvertFrom-ScopeOid $oidBytes) $one)) { Stop-Scope 'test-oid' }
    }
    Invoke-ScopeTestCase $state 'oid-secondary-output' 'identity-format' { ConvertFrom-ScopeOid ([object[]]@(17,$oidBytes)) }
    Invoke-ScopeTestCase $state 'oid-string-not-bytes' 'identity-format' { ConvertFrom-ScopeOid "$one`n" }
    Invoke-ScopeTestCase $state 'oid-missing-lf' 'identity-format' { ConvertFrom-ScopeOid ($ascii.GetBytes($one)) }
    Invoke-ScopeTestCase $state 'oid-crlf-rejected' 'identity-format' { ConvertFrom-ScopeOid ($ascii.GetBytes("$one`r`n")) }
    Invoke-ScopeTestCase $state 'oid-extra-lf' 'identity-format' { ConvertFrom-ScopeOid ($ascii.GetBytes("$one`n`n")) }
    Invoke-ScopeTestCase $state 'oid-uppercase-rejected' 'identity-format' { ConvertFrom-ScopeOid ($ascii.GetBytes(('A'*40)+"`n")) }
    Invoke-ScopeTestCase $state 'oid-nonhex-rejected' 'identity-format' { ConvertFrom-ScopeOid ($ascii.GetBytes(('g'*40)+"`n")) }
    Invoke-ScopeTestCase $state 'head-identity-positive' '' { Assert-ScopeHeadIdentity $one $one }
    Invoke-ScopeTestCase $state 'head-identity-mismatch' 'head-identity' { Assert-ScopeHeadIdentity $one $zero }
    Invoke-ScopeTestCase $state 'head-identity-format' 'head-identity' { Assert-ScopeHeadIdentity $one 'HEAD' }
    $record = @{timedOut=$false}
    Invoke-ScopeTestCase $state 'transport-time-positive' '' { Assert-ScopeTransportTime $pure 0 $record }
    Invoke-ScopeTestCase $state 'transport-timeout' 'git-timeout' { Assert-ScopeTransportTime $pure 10 $record }
    Invoke-ScopeTestCase $state 'transport-timeout-recorded' '' { if (-not $record.timedOut) { Stop-Scope 'test-timeout' } }
    $record = @{timedOut=$false}
    Invoke-ScopeTestCase $state 'transport-source-deadline' 'git-timeout' { Assert-ScopeTransportTime $expired 0 $record }
    Invoke-ScopeTestCase $state 'transport-exit-zero' '' { Assert-ScopeGitExit 0 }
    Invoke-ScopeTestCase $state 'transport-exit-one' 'git-exit' { Assert-ScopeGitExit 1 }
    Invoke-ScopeTestCase $state 'transport-exit-error' 'git-exit' { Assert-ScopeGitExit 128 }
    Invoke-ScopeTestCase $state 'helper-physical-not-exempt' 'physical-bytes' { Assert-ScopePhysicalBytes $Product.policy.helper $lf $binary }
    $exhausted = New-ScopeContext $Root 60; $exhausted.GitCount = 60
    Invoke-ScopeTestCase $state 'git-process-budget' 'git-count' { Invoke-ScopeGit $exhausted @('--version') }
    Invoke-ScopeTestCase $state 'git-budget-before-launch' '' { if ($exhausted.GitCount -ne 60 -or $exhausted.Commands.Count -ne 0) { Stop-Scope 'test-process-budget' } }
    Assert-ScopeTime $pure
    $pureSeconds = $pure.Clock.Elapsed.TotalSeconds
    # Real Git integration uses one synthetic seed plus eleven independent local
    # clones. Each dirty repository and its evidence is preserved, never cleaned.
    if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { throw 'scope-tests:runner-temp' }
    $tempRoot = [IO.Path]::GetFullPath($env:RUNNER_TEMP)
    $relative = [IO.Path]::GetRelativePath($Root, $tempRoot).Replace('\','/')
    if (-not ($relative.StartsWith('../',[StringComparison]::Ordinal) -or [IO.Path]::IsPathRooted($relative))) { throw 'scope-tests:fixture-location' }
    $fixtureRoot = Join-Path $tempRoot ('metrics-scope-tests-' + [Guid]::NewGuid().ToString('N'))
    $null = [IO.Directory]::CreateDirectory($fixtureRoot)
    $seed = Join-Path $fixtureRoot 'seed'; $null = [IO.Directory]::CreateDirectory($seed)
    $integration = New-ScopeContext $seed 180
    $integration.MaxGitCount = 60
    $state.Context = $integration
    $null = Invoke-ScopeGit $integration @('init','--initial-branch=main','--quiet')
    Write-ScopeTestBytes $seed '.gitattributes' ($ascii.GetBytes("* -text`n*.ps1 text eol=crlf`n*.cs text eol=lf`nfixture.html -text`n"))
    Write-ScopeTestBytes $seed '.gitignore' ($ascii.GetBytes("obj/`nignored.props`n"))
    Write-ScopeTestBytes $seed 'file.cs' $lf
    Write-ScopeTestBytes $seed 'script.ps1' $lf
    Write-ScopeTestBytes $seed 'fixture.html' $existingCr
    Write-ScopeTestBytes $seed 'binary.dat' $binary
    $null = Invoke-ScopeGit $integration @('add','--','.gitattributes','.gitignore','file.cs','script.ps1','fixture.html','binary.dat')
    $null = Invoke-ScopeGit $integration @('-c','user.name=Scope Fixture','-c','user.email=scope-fixture@example.invalid','-c','commit.gpgSign=false','commit','--quiet','-m','synthetic scope fixture')
    $seedCommit = Get-ScopeOid $integration 'HEAD^{commit}'; $seedTree = Get-ScopeOid $integration 'HEAD^{tree}'
    Invoke-ScopeTestCase $state 'git-wrapper-byte-array-contract' '' {
        $actualBytes = Invoke-ScopeGit $integration @('rev-parse','--verify','HEAD^{commit}')
        if ($actualBytes -isnot [byte[]] -or $actualBytes.Length -ne 41 -or
            -not (Test-ScopeEqual (ConvertFrom-ScopeOid $actualBytes) $seedCommit)) { Stop-Scope 'test-git-bytes' }
    }
    $fixtureExpected = Get-ScopeTree $integration $seedTree
    [string[]]$ids = @($fixtureExpected.Values | ForEach-Object { $_.blob } | Sort-Object -Unique)
    $raw = Invoke-ScopeGit $integration @('cat-file','--batch') ($ascii.GetBytes(($ids -join "`n") + "`n"))
    $fixtureBlobs = ConvertFrom-ScopeBatch $raw $ids $integration
    Write-ScopeTestOwned $seed $owned
    Invoke-ScopeTestCase $state 'git-checkout-positive' '' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
    Invoke-ScopeTestCase $state 'git-index-positive' '' { Assert-ScopeInventory $fixtureExpected (ConvertFrom-ScopeTree (Invoke-ScopeGit $integration @('ls-files','--stage','-z')) $true) }
    Invoke-ScopeTestCase $state 'git-flags-positive' '' { Assert-ScopeFlags (Invoke-ScopeGit $integration @('ls-files','-v','-z')) $fixtureExpected }
    Invoke-ScopeTestCase $state 'git-diff-positive' '' { Invoke-ScopeGit $integration @('diff','--no-ext-diff','--no-textconv','--exit-code','HEAD','--') }
    Invoke-ScopeTestCase $state 'git-diff-cached-positive' '' { Invoke-ScopeGit $integration @('diff','--no-ext-diff','--no-textconv','--cached','--exit-code','HEAD','--') }
    Invoke-ScopeTestCase $state 'git-identity-positive' '' { Assert-ScopeIdentity $integration $seedCommit $seedTree }
    Invoke-ScopeTestCase $state 'git-identity-missing' 'git-exit' { Assert-ScopeIdentity $integration $zero $seedTree }
    Invoke-ScopeTestCase $state 'git-identity-wrong-tree' 'identity' { Assert-ScopeIdentity $integration $seedCommit $zero }
    Invoke-ScopeTestCase $state 'git-ancestor-positive' '' { Assert-ScopeAncestor $integration $seedCommit $seedCommit }
    $orphanBytes = Invoke-ScopeGit $integration @('-c','user.name=Scope Fixture','-c','user.email=scope-fixture@example.invalid','-c','commit.gpgSign=false','commit-tree',$seedTree,'-m','independent synthetic root')
    $orphan = $ascii.GetString($orphanBytes).TrimEnd([char]10)
    Invoke-ScopeTestCase $state 'git-ancestor-negative' 'ancestry' { Assert-ScopeAncestor $integration $seedCommit $orphan }
    Invoke-ScopeTestCase $state 'git-nonzero' 'git-exit' { Invoke-ScopeGit $integration @('rev-parse','--verify','refs/heads/absent-scope-fixture') }
    $repos = 1
    foreach ($scenario in @('staged','assume','skip','untracked','ignored','eol','missing','reparse','owned','logs','gitfile')) {
        Assert-ScopeTime $integration
        $integration.Root = $fixtureRoot
        $target = Join-Path $fixtureRoot $scenario
        $null = Invoke-ScopeGit $integration @('clone','--quiet','--local','--no-hardlinks','--',$seed,$target)
        $repos++
        if ($repos -gt 12 -or $integration.GitCount -gt 60) { throw 'scope-tests:integration-budget' }
        $integration.Root = $target
        Write-ScopeTestOwned $target $owned
        switch ($scenario) {
            'staged' {
                Write-ScopeTestBytes $target 'file.cs' ($ascii.GetBytes("changed`n"))
                $null = Invoke-ScopeGit $integration @('add','--','file.cs')
                Invoke-ScopeTestCase $state 'git-staged-index' 'inventory' { Assert-ScopeInventory $fixtureExpected (ConvertFrom-ScopeTree (Invoke-ScopeGit $integration @('ls-files','--stage','-z')) $true) }
                Invoke-ScopeTestCase $state 'git-staged-diff' 'git-exit' { Invoke-ScopeGit $integration @('diff','--no-ext-diff','--no-textconv','--cached','--exit-code','HEAD','--') }
            }
            'assume' {
                $null = Invoke-ScopeGit $integration @('update-index','--assume-unchanged','--','file.cs')
                Write-ScopeTestBytes $target 'file.cs' ($ascii.GetBytes("hidden`n"))
                Invoke-ScopeTestCase $state 'git-assume-flag' 'index-flag' { Assert-ScopeFlags (Invoke-ScopeGit $integration @('ls-files','-v','-z')) $fixtureExpected }
                Invoke-ScopeTestCase $state 'git-assume-physical' 'physical-bytes' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
            }
            'skip' {
                $null = Invoke-ScopeGit $integration @('update-index','--skip-worktree','--','file.cs')
                Write-ScopeTestBytes $target 'file.cs' ($ascii.GetBytes("hidden`n"))
                Invoke-ScopeTestCase $state 'git-skip-flag' 'index-flag' { Assert-ScopeFlags (Invoke-ScopeGit $integration @('ls-files','-v','-z')) $fixtureExpected }
                Invoke-ScopeTestCase $state 'git-skip-physical' 'physical-bytes' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
            }
            'untracked' {
                Write-ScopeTestBytes $target 'new.cs' $lf
                Invoke-ScopeTestCase $state 'git-untracked-source' 'physical-set' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
                $null = [IO.Directory]::CreateDirectory((Join-Path $target 'nested/.git'))
                Invoke-ScopeTestCase $state 'git-nested-admin' 'git-directory' { Get-ScopePhysical $integration }
            }
            'ignored' {
                Write-ScopeTestBytes $target 'ignored.props' $lf
                Invoke-ScopeTestCase $state 'git-ignored-import' 'physical-set' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
                # Preserve the first rejected fixture; an additional ignored obj is
                # independently asserted visible to the real filesystem enumerator.
                Write-ScopeTestBytes $target 'obj/generated.cs' $lf
                Invoke-ScopeTestCase $state 'git-ignored-obj-visible' '' {
                    $physical = Get-ScopePhysical $integration
                    if (-not $physical.ContainsKey('obj/generated.cs')) { Stop-Scope 'test-ignored' }
                }
                Invoke-ScopeTestCase $state 'git-ignored-obj' 'physical-set' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
            }
            'eol' {
                Write-ScopeTestBytes $target 'script.ps1' $lf
                Invoke-ScopeTestCase $state 'git-eol-lf' '' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
                Write-ScopeTestBytes $target 'script.ps1' $crlf
                Invoke-ScopeTestCase $state 'git-eol-crlf' '' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
                Write-ScopeTestBytes $target 'fixture.html' $crlf
                Invoke-ScopeTestCase $state 'git-fixture-minus-text' 'physical-bytes' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
            }
            'missing' {
                # Move rather than delete the synthetic source; keep rejected bytes.
                [IO.File]::Move((Join-Path $target 'file.cs'), (Join-Path $fixtureRoot 'missing-file.cs'))
                Invoke-ScopeTestCase $state 'git-missing-source' 'physical-set' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
            }
            'reparse' {
                $outside = Join-Path $fixtureRoot 'reparse-target'; $null = [IO.Directory]::CreateDirectory($outside)
                $null = New-Item -ItemType Junction -Path (Join-Path $target 'linked') -Target $outside -ErrorAction Stop
                Invoke-ScopeTestCase $state 'git-reparse' 'reparse' { Get-ScopePhysical $integration }
            }
            'owned' {
                $stagePath = 'artifacts/validator-metrics/evidence/stage-source.json'
                Write-ScopeTestBytes $target $stagePath ($ascii.GetBytes('{}'))
                Invoke-ScopeTestCase $state 'git-owned-stage-json' 'owned-bytes' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
                # Retain the rejected JSON outside the fixture before testing the
                # other exact owned file, so one defect cannot mask the next.
                [IO.File]::Move((Join-Path $target $stagePath), (Join-Path $fixtureRoot 'rejected-stage-source.json'))
                Write-ScopeTestBytes $target $stagePath $owned[$stagePath]
                Write-ScopeTestBytes $target 'artifacts/validator-metrics/evidence/manifest.json' ($ascii.GetBytes('{}'))
                Invoke-ScopeTestCase $state 'git-owned-json' 'owned-bytes' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
            }
            'logs' {
                Write-ScopeTestBytes $target 'artifacts/validator-metrics/process-logs/previous.txt' $lf
                Invoke-ScopeTestCase $state 'git-extra-log' 'physical-set' { Assert-ScopeCheckout $integration $fixtureExpected $fixtureBlobs $owned }
                [IO.Directory]::Move((Join-Path $target '.git'), (Join-Path $target 'admin-case-preserved'))
                [IO.Directory]::Move((Join-Path $target 'admin-case-preserved'), (Join-Path $target '.GIT'))
                Invoke-ScopeTestCase $state 'git-root-case-alias' 'git-directory' { Get-ScopePhysical $integration }
            }
            'gitfile' {
                [IO.Directory]::Move((Join-Path $target '.git'), (Join-Path $fixtureRoot 'gitfile-admin-preserved'))
                Write-ScopeTestBytes $target '.git' ($ascii.GetBytes('gitdir: ../gitfile-admin-preserved'))
                Invoke-ScopeTestCase $state 'git-external-admin-file' 'git-directory' { Get-ScopePhysical $integration }
            }
        }
    }
    Assert-ScopeTime $integration
    if ($integration.GitCount -gt 60 -or $repos -gt 12 -or $allClock.Elapsed.TotalSeconds -gt 300) { throw 'scope-tests:integration-budget' }
    return [ordered]@{ schema='metrics-scope-tests-v1'; status='passed'; powershell=$PSVersionTable.PSVersion.ToString()
        pureSeconds=$pureSeconds; integrationSeconds=$integration.Clock.Elapsed.TotalSeconds
        repositories=$repos; gitProcesses=$integration.GitCount; caseCount=$state.Cases.Count; cases=@($state.Cases.ToArray())
        buildSpy=0; execution='Real PowerShell production functions; real local Git fixtures; no builds or product mutations.' }
    } finally {
        if ($null -ne $integration) {
            foreach ($command in $integration.Commands) {
                $sanitized = [ordered]@{}
                foreach ($key in $command.Keys) { $sanitized[$key] = $command[$key] }
                $sanitized.arguments = @($command.arguments | ForEach-Object {
                    $_.Replace($fixtureRoot, '<scope-fixtures>')
                })
                $sanitized.scope = 'synthetic-scope-fixture'
                $EvidenceStage.commands += $sanitized
            }
        }
    }
}
