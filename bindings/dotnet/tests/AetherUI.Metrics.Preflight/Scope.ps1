# Closed metrics source policy. Scope itself is an externally reviewed trust root,
# not a self-authenticating hash. Importing this file performs no verification or IO.
function Stop-Scope([string] $Code) { throw [InvalidOperationException]::new("scope:$Code") }
function Test-ScopeEqual([string] $Left, [string] $Right) {
    return [string]::Equals($Left, $Right, [StringComparison]::Ordinal)
}
function New-ScopeMap { return ,([Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)) }
function New-ScopeContext([string] $Root, [int] $Seconds = 60) {
    return @{ Root = $Root; Clock = [Diagnostics.Stopwatch]::StartNew(); Seconds = $Seconds
        Commands = [Collections.Generic.List[object]]::new(); GitCount = 0; MaxGitCount = 60 }
}
function Assert-ScopeTime($Context) {
    if ($null -eq $Context) { return }
    if ($Context.Clock.Elapsed.TotalSeconds -ge $Context.Seconds) { Stop-Scope 'budget' }
}
function Get-ScopeHash([byte[]] $Bytes, [string] $Algorithm = 'SHA256') {
    $hash = [Security.Cryptography.HashAlgorithm]::Create($Algorithm)
    try { return [Convert]::ToHexString($hash.ComputeHash($Bytes)).ToLowerInvariant() }
    finally { $hash.Dispose() }
}
function Get-ScopeBlobId([byte[]] $Bytes) {
    $prefix = [Text.Encoding]::ASCII.GetBytes("blob $($Bytes.Length)`0")
    $hash = [Security.Cryptography.SHA1]::Create()
    try {
        $null = $hash.TransformBlock($prefix, 0, $prefix.Length, $null, 0)
        $null = $hash.TransformFinalBlock($Bytes, 0, $Bytes.Length)
        return [Convert]::ToHexString($hash.Hash).ToLowerInvariant()
    } finally { $hash.Dispose() }
}
function Assert-ScopePath([string] $Path) {
    if ($Path.Length -lt 1 -or $Path.Length -gt 1024 -or $Path -cmatch '[^\x20-\x7e]' -or
        $Path -cmatch '[\\:<>"|?*]') { Stop-Scope 'path' }
    foreach ($part in $Path.Split('/')) {
        if ($part.Length -lt 1 -or $part.Length -gt 255 -or $part -cmatch '[. ]$' -or
            [regex]::IsMatch($part, '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)',
                [Text.RegularExpressions.RegexOptions]::IgnoreCase -bor [Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
            Stop-Scope 'path'
        }
    }
}
function Get-ScopeRecords([byte[]] $Bytes, $Context = $null) {
    if ($Bytes.Length -gt 8MB) { Stop-Scope 'metadata-limit' }
    if ($Bytes.Length -eq 0) { return ,([string[]]@()) }
    if ($Bytes[$Bytes.Length - 1] -ne 0) { Stop-Scope 'nul-eof' }
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $records = [Collections.Generic.List[string]]::new()
    $start = 0
    for ($i = 0; $i -lt $Bytes.Length; $i++) {
        if (($i -band 1023) -eq 0) { Assert-ScopeTime $Context }
        if ($Bytes[$i] -eq 0) {
            if ($i -eq $start) { Stop-Scope 'empty-record' }
            try { $record = $utf8.GetString($Bytes, $start, $i - $start) }
            catch { Stop-Scope 'utf8' }
            $records.Add($record); $start = $i + 1
            if ($records.Count -gt 2048) { Stop-Scope 'entry-limit' }
        }
    }
    return ,$records.ToArray()
}
function ConvertFrom-ScopeTree([byte[]] $Bytes, [bool] $Index = $false, $Context = $null) {
    $map = New-ScopeMap
    $caseNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in (Get-ScopeRecords $Bytes $Context)) {
        Assert-ScopeTime $Context
        $pattern = if ($Index) { '\A100644 ([0-9a-f]{40}) 0\t(.+)\z' } else { '\A100644 blob ([0-9a-f]{40})\t(.+)\z' }
        $match = [regex]::Match($record, $pattern)
        if (-not $match.Success) { Stop-Scope 'entry-format' }
        $path = $match.Groups[2].Value
        Assert-ScopePath $path
        if ($map.ContainsKey($path) -or -not $caseNames.Add($path)) { Stop-Scope 'path-collision' }
        $map.Add($path, [ordered]@{ path = $path; mode = '100644'; type = 'blob'; blob = $match.Groups[1].Value })
    }
    return ,$map
}
function Assert-ScopeFlags([byte[]] $Bytes, $Expected, $Context = $null) {
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($record in (Get-ScopeRecords $Bytes $Context)) {
        Assert-ScopeTime $Context
        if (-not $record.StartsWith('H ', [StringComparison]::Ordinal)) { Stop-Scope 'index-flag' }
        $path = $record.Substring(2); Assert-ScopePath $path
        if (-not $Expected.ContainsKey($path) -or -not $seen.Add($path)) { Stop-Scope 'index-flag' }
    }
    if ($seen.Count -ne $Expected.Count) { Stop-Scope 'index-flag' }
}
function Assert-ScopeInventory($Expected, $Actual, $Context = $null) {
    if ($Expected.Count -ne $Actual.Count) { Stop-Scope 'inventory' }
    foreach ($path in $Expected.Keys) {
        Assert-ScopeTime $Context
        if (-not $Actual.ContainsKey($path)) { Stop-Scope 'inventory' }
        foreach ($field in @('path','mode','type','blob')) {
            if (-not (Test-ScopeEqual $Expected[$path][$field] $Actual[$path][$field])) { Stop-Scope 'inventory' }
        }
    }
}
function Get-ScopeCrlf([byte[]] $Bytes, $Context = $null) {
    $output = [IO.MemoryStream]::new()
    try {
        for ($i = 0; $i -lt $Bytes.Length; $i++) {
            if (($i -band 1023) -eq 0) { Assert-ScopeTime $Context }
            if ($Bytes[$i] -eq 10 -and ($i -eq 0 -or $Bytes[$i - 1] -ne 13)) { $output.WriteByte(13) }
            $output.WriteByte($Bytes[$i])
        }
        return ,$output.ToArray()
    } finally { $output.Dispose() }
}
function Test-ScopeBytes([byte[]] $Left, [byte[]] $Right) {
    if ($Left.Length -ne $Right.Length) { return $false }
    # Both length and cryptographic digest are compared; no text decoding/filters.
    return (Test-ScopeEqual (Get-ScopeHash $Left) (Get-ScopeHash $Right))
}
function Assert-ScopePhysicalBytes([string] $Path, [byte[]] $Blob, [byte[]] $Physical, $Context = $null) {
    Assert-ScopeTime $Context
    if (Test-ScopeBytes $Blob $Physical) { return 'blob-exact' }
    $extension = [IO.Path]::GetExtension($Path)
    if ((Test-ScopeEqual $extension '.ps1') -or (Test-ScopeEqual $extension '.sln') -or (Test-ScopeEqual $extension '.cmd')) {
        if (Test-ScopeBytes (Get-ScopeCrlf $Blob $Context) $Physical) { return 'declared-crlf' }
    }
    Stop-Scope 'physical-bytes'
}
function ConvertFrom-ScopeBatch([byte[]] $Bytes, [string[]] $Ids, $Context) {
    $result = New-ScopeMap; $offset = 0; [long]$total = 0
    foreach ($id in $Ids) {
        Assert-ScopeTime $Context
        $start = $offset
        while ($offset -lt $Bytes.Length -and $Bytes[$offset] -ne 10 -and $offset - $start -le 80) { $offset++ }
        if ($offset -ge $Bytes.Length -or $offset - $start -gt 80) { Stop-Scope 'batch-header' }
        $header = [Text.Encoding]::ASCII.GetString($Bytes, $start, $offset - $start)
        $match = [regex]::Match($header, '\A([0-9a-f]{40}) blob (0|[1-9][0-9]{0,7})\z')
        if (-not $match.Success -or -not (Test-ScopeEqual $match.Groups[1].Value $id)) { Stop-Scope 'batch-header' }
        $size = [int]$match.Groups[2].Value
        if ($size -gt 2MB) { Stop-Scope 'blob-limit' }
        $total += $size
        if ($total -gt 64MB) { Stop-Scope 'total-limit' }
        $offset++
        if ([long]$offset + $size -ge $Bytes.Length) { Stop-Scope 'batch-truncated' }
        $blob = [byte[]]::new($size)
        [Array]::Copy($Bytes, $offset, $blob, 0, $size); $offset += $size
        if ($Bytes[$offset] -ne 10) { Stop-Scope 'batch-delimiter' }; $offset++
        if (-not (Test-ScopeEqual (Get-ScopeBlobId $blob) $id)) { Stop-Scope 'blob-identity' }
        if ($result.ContainsKey($id)) { Stop-Scope 'batch-duplicate' }
        $result.Add($id, $blob)
    }
    if ($offset -ne $Bytes.Length) { Stop-Scope 'batch-eof' }
    return ,$result
}
function Assert-ScopeHeadIdentity([string] $Head, [string] $PushSha) {
    if ($Head -cnotmatch '\A[0-9a-f]{40}\z' -or $PushSha -cnotmatch '\A[0-9a-f]{40}\z' -or
        -not (Test-ScopeEqual $Head $PushSha)) { Stop-Scope 'head-identity' }
}
function Assert-ScopeTransportTime($Context, [double] $ProcessSeconds, $Record) {
    if ($ProcessSeconds -ge 10 -or $Context.Clock.Elapsed.TotalSeconds -ge $Context.Seconds) {
        $Record.timedOut = $true; Stop-Scope 'git-timeout'
    }
}
function Assert-ScopeGitExit([int] $ExitCode) {
    if ($ExitCode -ne 0) { Stop-Scope 'git-exit' }
}
# One byte-pipe consumer shared by Git stdout/stderr and physical file reads.
# The same function is tested with synthetic MemoryStreams and real Git streams.
function Receive-ScopePipe([IO.Stream] $Stream, [IO.MemoryStream] $Output, [byte[]] $Buffer,
    [ref] $Task, [ref] $Eof, [long] $Limit, [string] $LimitCode) {
    if ($Eof.Value -or -not $Task.Value.IsCompleted) { return }
    try { $count = $Task.Value.GetAwaiter().GetResult() }
    catch { Stop-Scope 'pipe-read' }
    if ($Output.Length + $count -gt $Limit) { Stop-Scope $LimitCode }
    if ($count -eq 0) { $Eof.Value = $true; return }
    $Output.Write($Buffer, 0, $count)
    $Task.Value = $Stream.ReadAsync($Buffer, 0, $Buffer.Length)
}
function Complete-ScopeWrite($Task) {
    # PowerShell binds the runtime Task<T> awaiter, even when WriteAsync's
    # declared return type is Task. Never emit its auxiliary completion value.
    try { $null = $Task.GetAwaiter().GetResult() }
    catch { Stop-Scope 'pipe-write' }
}
# Bounded asynchronous byte pipes. No shell, line-oriented PowerShell native IO,
# text filters, retries, repository writes, environment dumps or process log files.
function Invoke-ScopeGit($Context, [string[]] $Arguments, [byte[]] $InputBytes = @(), [int] $OutputLimit = 8388608) {
    Assert-ScopeTime $Context
    if ($Context.GitCount -ge $Context.MaxGitCount) { Stop-Scope 'git-count' }
    $Context.GitCount++
    $record = [ordered]@{ file = 'git'; arguments = $Arguments; exitCode = $null; timedOut = $false
        stdoutBytes = 0; stderrBytes = 0; complete = $false; termination = 'not-needed' }
    $Context.Commands.Add($record)
    $psi = [Diagnostics.ProcessStartInfo]::new('git')
    $psi.WorkingDirectory = $Context.Root; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $psi.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $psi
    $out = [IO.MemoryStream]::new(); $err = [IO.MemoryStream]::new()
    $ob = [byte[]]::new(8192); $eb = [byte[]]::new(8192)
    $started = $false; $kill = $false; $closed = $false
    $watch = [Diagnostics.Stopwatch]::StartNew()
    try {
        if (-not $process.Start()) { Stop-Scope 'git-start' }; $started = $true
        $ot = $process.StandardOutput.BaseStream.ReadAsync($ob, 0, $ob.Length)
        $et = $process.StandardError.BaseStream.ReadAsync($eb, 0, $eb.Length)
        $it = $process.StandardInput.BaseStream.WriteAsync($InputBytes, 0, $InputBytes.Length)
        $oeof = $false; $eeof = $false
        while ($true) {
            Assert-ScopeTransportTime $Context $watch.Elapsed.TotalSeconds $record
            if (-not $closed -and $it.IsCompleted) { Complete-ScopeWrite $it; $process.StandardInput.Close(); $closed = $true }
            Receive-ScopePipe $process.StandardOutput.BaseStream $out $ob ([ref]$ot) ([ref]$oeof) $OutputLimit 'git-output-limit'
            Receive-ScopePipe $process.StandardError.BaseStream $err $eb ([ref]$et) ([ref]$eeof) 8MB 'git-error-limit'
            if ($process.HasExited) {
                if ($null -eq $record.exitCode) { $record.exitCode = $process.ExitCode }
                if ($oeof -and $eeof -and $closed) { break }
            }
            [Threading.Thread]::Sleep(1)
        }
        $record.complete = $true
        Assert-ScopeGitExit $record.exitCode
        return ,$out.ToArray()
    } finally {
        if ($started -and -not $process.HasExited -and -not $kill) {
            $kill = $true; $record.termination = 'uncertain'
            try { $process.Kill($true); if ($process.WaitForExit(5000)) { $record.exitCode = $process.ExitCode; $record.termination = 'exited-after-tree-kill' } }
            catch { $record.termination = 'uncertain' }
        }
        $record.stdoutBytes = $out.Length; $record.stderrBytes = $err.Length
        $out.Dispose(); $err.Dispose(); $process.Dispose()
    }
}
function ConvertFrom-ScopeOid([object] $Bytes) {
    # Enforce the transport's single byte[] result without coercing other output.
    if ($Bytes -isnot [byte[]] -or $Bytes.Length -ne 41 -or $Bytes[40] -ne 10) { Stop-Scope 'identity-format' }
    $value = [Text.Encoding]::ASCII.GetString($Bytes, 0, 40)
    if ($value -cnotmatch '\A[0-9a-f]{40}\z') { Stop-Scope 'identity-format' }
    return $value
}
function Get-ScopeOid($Context, [string] $Reference) {
    $bytes = Invoke-ScopeGit $Context @('rev-parse','--verify',$Reference)
    # rev-parse emits exactly one ASCII object ID and LF (not arbitrary Trim()).
    return (ConvertFrom-ScopeOid $bytes)
}
function Assert-ScopeIdentity($Context, [string] $Commit, [string] $Tree) {
    $actual = Get-ScopeOid $Context "$Commit`^{commit}"
    $actualTree = Get-ScopeOid $Context "$Commit`^{tree}"
    if (-not (Test-ScopeEqual $Commit $actual) -or -not (Test-ScopeEqual $Tree $actualTree)) { Stop-Scope 'identity' }
}
function Assert-ScopeAncestor($Context, [string] $Parent, [string] $Child) {
    try { $bytes = Invoke-ScopeGit $Context @('merge-base','--is-ancestor',$Parent,$Child) }
    catch { if (Test-ScopeEqual $_.Exception.Message 'scope:git-exit') { Stop-Scope 'ancestry' }; throw }
    if ($bytes.Length -ne 0) { Stop-Scope 'ancestry-output' }
}
function Get-ScopeTree($Context, [string] $Reference) {
    return ,(ConvertFrom-ScopeTree (Invoke-ScopeGit $Context @('ls-tree','-rz','--full-tree',$Reference)) $false $Context)
}
function Assert-ScopeOrdinary($Item) {
    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        ($Item.Attributes -band [IO.FileAttributes]::Device) -ne 0 -or $null -ne $Item.LinkTarget) { Stop-Scope 'reparse' }
}
function Get-ScopePhysical($Context) {
    $rootInfo = [IO.DirectoryInfo]::new($Context.Root)
    $ancestor = $rootInfo
    while ($null -ne $ancestor) { Assert-ScopeOrdinary $ancestor; $ancestor = $ancestor.Parent }
    $git = [IO.DirectoryInfo]::new((Join-Path $Context.Root '.git'))
    if (-not $git.Exists) { Stop-Scope 'git-directory' }; Assert-ScopeOrdinary $git
    $files = New-ScopeMap; $caseNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $pending = [Collections.Generic.Stack[IO.DirectoryInfo]]::new(); $pending.Push($rootInfo)
    $entries = 0
    while ($pending.Count -gt 0) {
        Assert-ScopeTime $Context
        $directory = $pending.Pop()
        foreach ($item in $directory.EnumerateFileSystemInfos()) {
            Assert-ScopeTime $Context
            $path = [IO.Path]::GetRelativePath($Context.Root, $item.FullName).Replace('\','/')
            if ([string]::Equals($item.Name, '.git', [StringComparison]::OrdinalIgnoreCase)) {
                if (-not (Test-ScopeEqual $path '.git') -or $item -isnot [IO.DirectoryInfo]) { Stop-Scope 'git-directory' }
                Assert-ScopeOrdinary $item; continue
            }
            Assert-ScopePath $path; Assert-ScopeOrdinary $item
            $entries++
            if ($entries -gt 2048) { Stop-Scope 'physical-entry-limit' }
            if (-not $caseNames.Add($path)) { Stop-Scope 'physical-collision' }
            if ($item -is [IO.DirectoryInfo]) { $pending.Push($item) }
            elseif ($item -is [IO.FileInfo]) { $files.Add($path, $item) }
            else { Stop-Scope 'physical-type' }
        }
    }
    return ,$files
}
function Read-ScopePhysical($Context, $Item, [ref] $Total) {
    Assert-ScopeTime $Context; Assert-ScopeOrdinary $Item
    if ($Item.Length -gt 4MB) { Stop-Scope 'physical-size' } # max 2MiB blob with declared CRLF
    $Total.Value += $Item.Length
    if ($Total.Value -gt 64MB) { Stop-Scope 'total-limit' }
    $stream = [IO.FileStream]::new($Item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::Read, 8192, [IO.FileOptions]::Asynchronous)
    $output = [IO.MemoryStream]::new(); $buffer = [byte[]]::new(8192)
    try {
        $task = $stream.ReadAsync($buffer, 0, $buffer.Length); $eof = $false
        while (-not $eof) {
            Assert-ScopeTime $Context
            Receive-ScopePipe $stream $output $buffer ([ref]$task) ([ref]$eof) 4MB 'physical-size'
            if (-not $eof -and -not $task.IsCompleted) { [Threading.Thread]::Sleep(1) }
        }
        if ($output.Length -ne $Item.Length) { Stop-Scope 'physical-size' }
        return ,$output.ToArray()
    } finally { $stream.Dispose(); $output.Dispose() }
}
function Assert-ScopeCheckout($Context, $Expected, $Blobs, $Owned) {
    $physical = Get-ScopePhysical $Context
    if ($physical.Count -ne $Expected.Count + $Owned.Count) { Stop-Scope 'physical-set' }
    foreach ($path in $physical.Keys) {
        if (-not $Expected.ContainsKey($path) -and -not $Owned.ContainsKey($path)) { Stop-Scope 'physical-set' }
    }
    $evidence = [Collections.Generic.List[object]]::new(); [long]$total = 0
    foreach ($path in $Expected.Keys) {
        Assert-ScopeTime $Context
        if (-not $physical.ContainsKey($path)) { Stop-Scope 'physical-set' }
        $bytes = Read-ScopePhysical $Context $physical[$path] ([ref]$total)
        $blob = $Blobs[$Expected[$path].blob]
        $representation = Assert-ScopePhysicalBytes $path $blob $bytes $Context
        $evidence.Add([ordered]@{ path = $path; blob = $Expected[$path].blob; blobSha256 = (Get-ScopeHash $blob)
            physicalSha256 = (Get-ScopeHash $bytes); representation = $representation; bytes = $bytes.Length })
    }
    foreach ($path in $Owned.Keys) {
        if (-not $physical.ContainsKey($path)) { Stop-Scope 'owned-set' }
        $bytes = Read-ScopePhysical $Context $physical[$path] ([ref]$total)
        if (-not (Test-ScopeBytes $Owned[$path] $bytes)) { Stop-Scope 'owned-bytes' }
    }
    return ,$evidence.ToArray()
}
function Get-ScopeOwned($Manifest, $Stage) {
    if (-not (Test-ScopeEqual $Manifest.status 'incomplete') -or $null -ne $Manifest.source -or
        -not (Test-ScopeEqual $Stage.name 'source') -or -not (Test-ScopeEqual $Stage.status 'running') -or
        $Stage.commands.Count -ne 0 -or $null -ne $Stage.details) { Stop-Scope 'owned-state' }
    $owned = New-ScopeMap; $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $owned.Add('artifacts/validator-metrics/evidence/manifest.json',
        $utf8.GetBytes(($Manifest | ConvertTo-Json -Depth 40) + [Environment]::NewLine))
    $owned.Add('artifacts/validator-metrics/evidence/stage-source.json',
        $utf8.GetBytes(($Stage | ConvertTo-Json -Depth 40) + [Environment]::NewLine))
    return ,$owned
}

function Get-ScopePolicy {
    # Literals from independently reviewed B/P/M and C final. No latest/fallback.
    $identities = @(
        @{ name='B'; commit='1229a9544daff36ae5d4c5b53767a232db24dfe0'; tree='f82c52cd615afc7540fe70ae4e2c495055037087' },
        @{ name='P'; commit='667c4441a299d5dd6c2c313697a5154389978ed7'; tree='07c6767ef77ca3e8ee899668f2fe61498643d51a' },
        @{ name='M'; commit='99c336187bd264dade2f177c32f420172b3cf777'; tree='52130f5c427ff305c1aa776743b34c8a21016d9e' },
        @{ name='C'; commit='1842e540a9d7a793a5463f6f31bad6562a70f707'; tree='180956beb77ce550e95484e31b24061eea98934d' }
    )
    $overlay = @(
        @{ path='.github/workflows/validator-native-capture.yml'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='4dd98d363560a1d1ff3ec3742b99f109b23a8d66' }; sha256='7f59f62ba705ce1b2ed901e91e079ab49d328d08e94c829491991e31d28b57a8' },
        @{ path='bindings/dotnet/src/AetherUI.Windows/Graphics/Direct2DRenderBackend.CaptureDiagnostics.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='a12d187fa517191e0215825efaab549ab05d32a4' }; sha256='4964896828f71001d210da084e664807f86d85b5265e76dc22e5c750b02efa31' },
        @{ path='bindings/dotnet/src/AetherUI.Windows/Graphics/Direct2DRenderBackend.Incremental.cs'; status='M'; before=@{ mode='100644'; type='blob'; blob='db9c98eaa38241578767abc1c3e765862d032ca4' }
            after=@{ mode='100644'; type='blob'; blob='e22f2f1eacfbf461ba89fb750c9d1318032522f8' }; sha256='f37881852ab9f1dcf58d03e2585b42a7c71155407b8c0dca50e8735a516a5afc' },
        @{ path='bindings/dotnet/src/AetherUI.Windows/Graphics/Direct2DRenderBackend.cs'; status='M'; before=@{ mode='100644'; type='blob'; blob='677bb8466d78c1afe03d8b262d2741a660fcf820' }
            after=@{ mode='100644'; type='blob'; blob='99d629a0a5fbbd83b0eebceead7f03fa6df64557' }; sha256='0074ef98c73266316ad6f1e99aad611bccd7d70363480f05f349988dcc531bf0' },
        @{ path='bindings/dotnet/src/AetherUI.Windows/Properties/AssemblyInfo.cs'; status='M'; before=@{ mode='100644'; type='blob'; blob='899639e583d1a507853dc9a56aa3e5e3376a3c68' }
            after=@{ mode='100644'; type='blob'; blob='95b3c567fb0ca3404e9e64e1888d03d3972f7061' }; sha256='aae192bc567dd7231b122609126d53055ecbbd5be59a48d6a509945867b7031d' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/AetherUI.Validator.NativeCapture.csproj'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='b7dbe01d534d5b116f49936614c00eb4caf83ee4' }; sha256='7e24df275b7311216a3c11c1b4268d08ebcdf24e6b8fd967e65d2bc3e5658fe6' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/ArtifactManifest.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='3e4d894baee5a503b0c670cd34bebe1fb5dd328b' }; sha256='011778e31e602ac03f588bf603551b795db26c4b7cd8bce929bbeafa2e79f59b' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/CAPTURE-NOTES.md'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='0f3715747b7c9d9c130a448271877ef5e620eb46' }; sha256='406440c0174c8a8f16e8ccc7c476fcd8787d4f7a2e24c965374b6c6949fbafa9' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/CapturePngEncoder.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='8f676bc1f27a6b025d3232dfd30dc84ca8570ad7' }; sha256='820bdb42823df9f81cfd8545db5a6c563648f3616f72527793b7dcea56148695' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/CaptureSelfTests.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='a5ca77651e21be4fbecc7feefc449b2b0f85ef3a' }; sha256='ed1994132bd499fa9a71d66563ac5b77002174584a46b628cf03f7d2970e2f7e' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/HiddenCaptureWindow.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='b74f109a8371f81b8021f986a4fa2f35e1b0fae9' }; sha256='8f0ab98d2071ec3ba696cc757bf7914d38948c889ed224ecb46013db3e0e0e0e' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/NativeCaptureRunner.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='837cff9d7751a0cb5fb6de03695423cb46611e4f' }; sha256='ddb9f5feac4840eacdb356f0cfeb470e519aa859e8035a3dad34f880418d4a9c' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/PrepareCapture.ps1'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='00b0adfe5d77f4a4a158c145d5942e6d7c5c8f48' }; sha256='324989253fd9b0ca3d5c14bae643b3a337a94f927bcd7c0be1f74dbdbbf7c8d4' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/Program.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='69e19453f0e5630e8f5925d74ed96d2ce367cbbb' }; sha256='22a38f6c356944dafc1173f524a91ca5af4e55e8b54a505cddfed5bc0d471423' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/QueuedStaContext.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='a1d4d491c3d5e9224d5d6ea5efa08de3beda195d' }; sha256='749c95f823407851c3bcd3b1fea997c0994cfa6cb75756c767b6a379b59ccc7d' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/RunCapture.ps1'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='062817bb472c4edfb9b883787139db787b2451bd' }; sha256='6d8c54cb71c671b9f3979664f83727dd98f8e091da141224888abf6858b6673e' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/RuntimeGuard.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='094a5abfefdadebee394b91f8d8207dbea5f4f01' }; sha256='a77495f2ff3390ca27ecfc5d25491805d2414c2fe4ca0abe16b3ebc6a0cf1865' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/TrackedImageResolver.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='5d541ae95d91e98fd371ab6ba4bb8da8e02e8c79' }; sha256='fd201208c606ade5087a04664f87f9d2aa4f6357f061bc78a396a3e01d78e7fc' },
        @{ path='bindings/dotnet/tools/AetherUI.Validator.NativeCapture/TrackedLoader.cs'; status='A'; before=$null
            after=@{ mode='100644'; type='blob'; blob='2c01ffa1e3feb6f7ccdbbe8d35d3f76cd7444e62' }; sha256='e7e81ff567276fa114010c68b5466e03a3cece92282442a94ea49ff889704419' }
    )
    $controls = @(
        @{ path='bindings/dotnet/tests/AetherUI.Metrics.Preflight/Prepare.ps1'; blob='978f28c4145b4fe9c77d4bc1786f567cd3250743'; sha256='3bce21acbc685a5b26a9b0e77e4879bee1e114fe2835d8d395aed4f7e9a5166e' },
        @{ path='bindings/dotnet/tests/AetherUI.Metrics.Preflight/Scope.Tests.ps1'; blob='cd51954fe7dde92687b76a9415c2b33c0aa1b292'; sha256='3236d2ce44e1da8f45b601c177317955ea02c277d2906928c5c16bf96b143315' }
    )
    return @{ identities=$identities; overlay=$overlay; controls=$controls
        helper='bindings/dotnet/tests/AetherUI.Metrics.Preflight/Scope.ps1' }
}
function Copy-ScopeInventory($InputMap, $Context = $null) {
    $copy = New-ScopeMap
    foreach ($path in $InputMap.Keys) { Assert-ScopeTime $Context; $copy.Add($path, $InputMap[$path]) }
    return ,$copy
}
function Get-ScopeExpected($Metrics, $Capture, $Head, $Policy, $Context = $null) {
    $expected = Copy-ScopeInventory $Metrics $Context
    if ($Policy.overlay.Count -ne 19 -or $Policy.controls.Count -ne 2) { Stop-Scope 'policy-count' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in $Policy.overlay) {
        $path = $entry.path; Assert-ScopePath $path
        if (-not $seen.Add($path)) { Stop-Scope 'overlay-duplicate' }
        if (Test-ScopeEqual $entry.status 'A') {
            if ($null -ne $entry.before -or $expected.ContainsKey($path)) { Stop-Scope 'overlay-status' }
        } elseif (Test-ScopeEqual $entry.status 'M') {
            if ($null -eq $entry.before -or -not $expected.ContainsKey($path)) { Stop-Scope 'overlay-status' }
            foreach ($field in @('mode','type','blob')) {
                if (-not (Test-ScopeEqual $expected[$path][$field] $entry.before[$field])) { Stop-Scope 'overlay-before' }
            }
        } else { Stop-Scope 'overlay-status' }
        if (-not (Test-ScopeEqual $entry.after.mode '100644') -or -not (Test-ScopeEqual $entry.after.type 'blob') -or
            $entry.after.blob -cnotmatch '\A[0-9a-f]{40}\z' -or $entry.sha256 -cnotmatch '\A[0-9a-f]{64}\z') { Stop-Scope 'overlay-after' }
        $expected[$path] = @{ path=$path; mode=$entry.after.mode; type=$entry.after.type; blob=$entry.after.blob }
    }
    Assert-ScopeInventory $expected $Capture $Context
    foreach ($control in $Policy.controls) {
        if ($control.blob -cnotmatch '\A[0-9a-f]{40}\z' -or $control.sha256 -cnotmatch '\A[0-9a-f]{64}\z') { Stop-Scope 'control-pin' }
        $expected[$control.path] = @{ path=$control.path; mode='100644'; type='blob'; blob=$control.blob }
    }
    if (-not $Head.ContainsKey($Policy.helper) -or $expected.ContainsKey($Policy.helper) -or
        -not (Test-ScopeEqual $Head[$Policy.helper].mode '100644') -or -not (Test-ScopeEqual $Head[$Policy.helper].type 'blob')) { Stop-Scope 'helper-entry' }
    # One explicit externally reviewed root; physical bytes still checked against HEAD.
    $expected.Add($Policy.helper, $Head[$Policy.helper])
    Assert-ScopeInventory $expected $Head $Context
    return ,$expected
}
function Assert-ScopeHashes($Expected, $Blobs, $Policy, $Context = $null) {
    foreach ($entry in @($Policy.overlay) + @($Policy.controls)) {
        Assert-ScopeTime $Context
        if (-not $Expected.ContainsKey($entry.path)) { Stop-Scope 'pinned-hash' }
        $oid = $Expected[$entry.path].blob
        if (-not $Blobs.ContainsKey($oid) -or -not (Test-ScopeEqual (Get-ScopeHash $Blobs[$oid]) $entry.sha256)) { Stop-Scope 'pinned-hash' }
    }
}
function Get-ScopeDelta($Before, $After, $Context = $null) {
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in $Before.Keys) { $null = $names.Add($path) }
    foreach ($path in $After.Keys) { $null = $names.Add($path) }
    $delta = New-ScopeMap
    foreach ($path in $names) {
        Assert-ScopeTime $Context
        $beforeEntry = if ($Before.ContainsKey($path)) { $Before[$path] } else { $null }
        $afterEntry = if ($After.ContainsKey($path)) { $After[$path] } else { $null }
        $different = $null -eq $beforeEntry -or $null -eq $afterEntry
        if (-not $different) {
            foreach ($field in @('mode','type','blob')) {
                if (-not (Test-ScopeEqual $beforeEntry[$field] $afterEntry[$field])) { $different = $true }
            }
        }
        if ($different) { $delta.Add($path, @{ path=$path; before=$beforeEntry; after=$afterEntry }) }
    }
    return ,$delta
}
function Assert-ScopeHistory($Baseline, $Prior, $Metrics, $Context = $null) {
    $production = @('bindings/dotnet/src/AetherUI.Core/Runtime/UiDocumentHost.cs',
        'bindings/dotnet/src/AetherUI.Core/Runtime/UiDocumentHostMetrics.cs',
        'bindings/dotnet/tools/AetherUI.Validator/PerfReporter.cs')
    $allowed = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in $production) { $null = $allowed.Add($path) }
    $delta = Get-ScopeDelta $Baseline $Metrics $Context
    $protectedCount = 0
    foreach ($path in $delta.Keys) {
        $protected = $false
        foreach ($prefix in @('native/','bindings/dotnet/src/','bindings/dotnet/tools/AetherUI.Validator/')) {
            if ($path.StartsWith($prefix, [StringComparison]::Ordinal)) { $protected = $true }
        }
        foreach ($exact in @('native','bindings/dotnet/src','bindings/dotnet/tools/AetherUI.Validator',
            'global.json','bindings/dotnet/Directory.Build.props','bindings/dotnet/Directory.Packages.props')) {
            if (Test-ScopeEqual $path $exact) { $protected = $true }
        }
        if ($protected) {
            $protectedCount++
            if (-not $allowed.Contains($path)) { Stop-Scope 'baseline-production' }
        }
    }
    if ($protectedCount -ne 3) { Stop-Scope 'baseline-production' }
    foreach ($path in @('.github/workflows/validator-managed-diagnostics.yml',
        'bindings/dotnet/tests/AetherUI.Metrics.Preflight/Prepare.ps1',
        'bindings/dotnet/tests/AetherUI.Metrics.Preflight/Validate.ps1',
        'bindings/dotnet/tests/AetherUI.Metrics.Tests/AetherUI.Metrics.Tests.csproj',
        'bindings/dotnet/tests/AetherUI.Metrics.Tests/Program.cs',
        'bindings/dotnet/tests/AetherUI.Metrics.Export/AetherUI.Metrics.Export.csproj',
        'bindings/dotnet/tests/AetherUI.Metrics.Export/Program.cs',
        'bindings/dotnet/docs/host-phase-metrics.md')) { $null = $allowed.Add($path) }
    $priorDelta = Get-ScopeDelta $Prior $Metrics $Context
    if ($priorDelta.Count -ne 11) { Stop-Scope 'prior-delta' }
    foreach ($path in $priorDelta.Keys) { if (-not $allowed.Contains($path)) { Stop-Scope 'prior-delta' } }
    return @{ production=@($production); sinceP=@($priorDelta.Values) }
}
function Invoke-MetricsScope([string] $Root, $Manifest, $Stage) {
    # Snapshot before any Git instrumentation can alter Stage.commands or JSON.
    $owned = Get-ScopeOwned $Manifest $Stage
    $context = New-ScopeContext $Root 60
    $policy = Get-ScopePolicy
    try {
        # Refuse gitdir files, reparse ancestors and administrative case aliases before Git can follow them.
        $null = Get-ScopePhysical $context
        $head = Get-ScopeOid $context 'HEAD^{commit}'
        Assert-ScopeHeadIdentity $head $env:GITHUB_SHA
        $headTree = Get-ScopeOid $context 'HEAD^{tree}'
        foreach ($identity in $policy.identities) { Assert-ScopeIdentity $context $identity.commit $identity.tree }
        for ($i = 0; $i -lt 3; $i++) { Assert-ScopeAncestor $context $policy.identities[$i].commit $policy.identities[$i + 1].commit }
        Assert-ScopeAncestor $context $policy.identities[3].commit $head
        $trees = New-ScopeMap
        foreach ($identity in $policy.identities) { $trees.Add($identity.name, (Get-ScopeTree $context $identity.tree)) }
        $headInventory = Get-ScopeTree $context $headTree
        $history = Assert-ScopeHistory $trees['B'] $trees['P'] $trees['M'] $context
        $expected = Get-ScopeExpected $trees['M'] $trees['C'] $headInventory $policy $context
        $index = ConvertFrom-ScopeTree (Invoke-ScopeGit $context @('ls-files','--stage','-z')) $true $context
        Assert-ScopeInventory $expected $index $context
        Assert-ScopeFlags (Invoke-ScopeGit $context @('ls-files','-v','-z')) $expected $context
        $ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($entry in $expected.Values) { $null = $ids.Add($entry.blob) }
        [string[]]$orderedIds = @($ids); [Array]::Sort($orderedIds, [StringComparer]::Ordinal)
        $inputBytes = [Text.Encoding]::ASCII.GetBytes(($orderedIds -join "`n") + "`n")
        # A single binary batch, with per-blob limits and exact EOF validated below.
        $batch = Invoke-ScopeGit $context @('cat-file','--batch') $inputBytes (64MB + 8MB)
        $blobs = ConvertFrom-ScopeBatch $batch $orderedIds $context
        Assert-ScopeHashes $expected $blobs $policy $context
        $physical = Assert-ScopeCheckout $context $expected $blobs $owned
        foreach ($arguments in @(@('diff','--no-ext-diff','--no-textconv','--exit-code','HEAD','--'),
            @('diff','--no-ext-diff','--no-textconv','--cached','--exit-code','HEAD','--'))) {
            $diff = Invoke-ScopeGit $context $arguments
            if ($diff.Length -ne 0) { Stop-Scope 'diff-output' }
        }
        $nativeTree = Get-ScopeOid $context 'HEAD:native'
        $managedTree = Get-ScopeOid $context 'HEAD:bindings/dotnet/src'
        $actualSinceP = Get-ScopeDelta $trees['P'] $headInventory $context
        $actualSinceB = Get-ScopeDelta $trees['B'] $headInventory $context
        Assert-ScopeTime $context
        return @{ context=$context; policy=$policy; expected=$expected; blobs=$blobs; trees=$trees; head=$headInventory
            source=[ordered]@{ commit=$head; tree=$headTree; nativeTree=$nativeTree; managedSourceTree=$managedTree
                productMatchesBaseline=$false; reviewedMetricsProductionDelta=$history.production
                reviewedMetricsSinceP=$history.sinceP; changedSinceP=@($actualSinceP.Values); changedSinceB=@($actualSinceB.Values)
                nativeAndDependencyInputsMatchBaseline=$true; identities=$policy.identities; overlay=$policy.overlay
                inventory=@($expected.Values); files=$physical; sourceSeconds=$context.Clock.Elapsed.TotalSeconds
                scopeTrust='External independent review of final commit; no self-hash claim; physical helper matches HEAD.'
                captureStatus='Exact reviewed source overlay only; no visual or runtime acceptance.'
                limits='Non-concurrent checkout audit; external host, Git, MSBuild and NuGet configuration are not certified.' }
        }
    } finally {
        # Append sanitized fixed-command metadata only after initial owned snapshots.
        foreach ($record in $context.Commands) { $Stage.commands += $record }
    }
}
