# Dot-sourced by Prepare.ps1 only after the candidate's real Core/Validator/preflight builds.
# Functions retain the same fail-closed stage/exits/30-second child-process contract.
$tests = 'bindings/dotnet/tests/AetherUI.Metrics.Tests/AetherUI.Metrics.Tests.csproj'
$testsOutput = 'artifacts/validator-metrics/tests-bin'
$exportRelative = 'bindings/dotnet/tests/AetherUI.Metrics.Export'
$exportProject = "$exportRelative/AetherUI.Metrics.Export.csproj"
$baselineRoot = 'artifacts/validator-metrics/baseline-product'
$exportBaselineOutput = 'artifacts/validator-metrics/export-baseline-bin'
$exportCandidateOutput = 'artifacts/validator-metrics/export-candidate-bin'

Invoke-Stage 'tests-restore' {
    $null = Invoke-Native 'dotnet' (@('restore', $tests) + $common)
    Save-Assets 'tests-restore' @($interop, $core, $tests)
}
Invoke-Stage 'tests-build' {
    $null = Invoke-Native 'dotnet' (@('build', $tests, '-c', 'Release', '--no-restore') + $common + @('-o', $testsOutput))
    $script:manifest.build.tests = @(
        foreach ($file in @('AetherUI.Metrics.Tests.dll', 'AetherUI.Core.dll', 'AetherUI.Core.Native.Interop.dll', 'aether_core.dll')) {
            Get-Identity "$testsOutput/$file"
        }
    )
}
Invoke-Stage 'tests-run' {
    Invoke-Bounded 'tests' @("$testsOutput/AetherUI.Metrics.Tests.dll", '--output', "$evidence/tests.json")
    $result = Get-Content -Raw -LiteralPath "$evidence/tests.json" | ConvertFrom-Json -AsHashtable
    if ($result.schema -ne 'host-phase-tests-v1' -or $result.status -ne 'passed' -or $result.checks.Count -eq 0 -or
        @($result.checks | Where-Object { $_.passed -ne $true }).Count -ne 0 -or
        ($result.cases -join ',') -ne 'T1,T2,T3,T6' -or
        $result.native.loadedSha256 -ne $script:manifest.build.native.sha256) {
        throw 'Required focal T1/T2/T3/T6 evidence is incomplete or inconsistent.'
    }
    $script:stage.details = Get-Identity "$evidence/tests.json"
}
Invoke-Stage 'export-source' {
    # Full history is already checked out without persisted credentials. No fetch, branch or push.
    if (Test-Path $baselineRoot) { throw 'Baseline arm already exists; do not reuse outputs.' }
    $null = Invoke-Native 'git' @('worktree', 'add', '--detach', $baselineRoot, $baseline)
    $armCommit = @(Invoke-Native 'git' @('-C', $baselineRoot, 'rev-parse', 'HEAD'))[-1].Trim()
    $armTree = @(Invoke-Native 'git' @('-C', $baselineRoot, 'rev-parse', 'HEAD^{tree}'))[-1].Trim()
    $armNativeTree = @(Invoke-Native 'git' @('-C', $baselineRoot, 'rev-parse', 'HEAD:native'))[-1].Trim()
    if ($armCommit -ne $baseline -or $armTree -ne $baselineTree -or $armNativeTree -ne $script:manifest.source.nativeTree) {
        throw 'T4 baseline checkout identity or native source differs.'
    }
    # Exact exporter sources, same relative ProjectReference, but isolated checkout/obj/assets/output.
    New-Item -ItemType Directory -Path "$baselineRoot/$exportRelative", "$baselineRoot/$nativeInstall/bin" | Out-Null
    $sources = @('AetherUI.Metrics.Export.csproj', 'Program.cs')
    foreach ($file in $sources) {
        Copy-Item -LiteralPath "$exportRelative/$file" -Destination "$baselineRoot/$exportRelative/$file"
        if ((Get-FileHash "$exportRelative/$file").Hash -ne (Get-FileHash "$baselineRoot/$exportRelative/$file").Hash) {
            throw 'The two exporter source files differ.'
        }
    }
    Copy-Item -LiteralPath "$nativeInstall/bin/aether_core.dll" -Destination "$baselineRoot/$nativeInstall/bin/aether_core.dll"
    $baselineNative = Get-PeIdentity "$baselineRoot/$nativeInstall/bin/aether_core.dll"
    if ($baselineNative.sha256 -ne $script:manifest.build.native.sha256) { throw 'Shared native copy differs.' }
    $script:manifest.comparison = [ordered]@{ baselineCommit = $armCommit; baselineTree = $armTree
        candidateCommit = $script:manifest.source.commit; candidateTree = $script:manifest.source.tree
        nativeTree = $armNativeTree; nativeSha256 = $baselineNative.sha256
        exporterSources = @($sources | ForEach-Object { Get-Identity "$exportRelative/$_" })
        baselineExporterSources = @($sources | ForEach-Object { Get-Identity "$baselineRoot/$exportRelative/$_" })
        isolation = 'Separate baseline/candidate checkouts, project obj/assets and executable outputs; same freshly built native DLL.' }
    $script:stage.details = $script:manifest.comparison
}
Invoke-Stage 'export-baseline-restore' {
    $null = Invoke-Native 'dotnet' (@('restore', "$baselineRoot/$exportProject") + $common)
    Save-Assets 'export-baseline-restore' @("$baselineRoot/$interop", "$baselineRoot/$core", "$baselineRoot/$exportProject")
}
Invoke-Stage 'export-baseline-build' {
    $null = Invoke-Native 'dotnet' (@('build', "$baselineRoot/$exportProject", '-c', 'Release', '--no-restore') + $common + @('-o', $exportBaselineOutput))
    $script:manifest.build.exportBaseline = @(
        foreach ($file in @('AetherUI.Metrics.Export.dll', 'AetherUI.Core.dll', 'AetherUI.Core.Native.Interop.dll', 'aether_core.dll')) {
            Get-Identity "$exportBaselineOutput/$file"
        }
    )
}
Invoke-Stage 'export-candidate-restore' {
    $null = Invoke-Native 'dotnet' (@('restore', $exportProject) + $common)
    Save-Assets 'export-candidate-restore' @($interop, $core, $exportProject)
}
Invoke-Stage 'export-candidate-build' {
    $null = Invoke-Native 'dotnet' (@('build', $exportProject, '-c', 'Release', '--no-restore') + $common + @('-o', $exportCandidateOutput))
    $script:manifest.build.exportCandidate = @(
        foreach ($file in @('AetherUI.Metrics.Export.dll', 'AetherUI.Core.dll', 'AetherUI.Core.Native.Interop.dll', 'aether_core.dll')) {
            Get-Identity "$exportCandidateOutput/$file"
        }
    )
}
Invoke-Stage 'export-baseline-run' {
    Invoke-Bounded 'export-baseline' @("$exportBaselineOutput/AetherUI.Metrics.Export.dll", '--output',
        "$evidence/export-baseline.json", '--native', "$nativeInstall/bin/aether_core.dll")
    $script:stage.details = Get-Identity "$evidence/export-baseline.json"
}
Invoke-Stage 'export-candidate-run' {
    Invoke-Bounded 'export-candidate' @("$exportCandidateOutput/AetherUI.Metrics.Export.dll", '--output',
        "$evidence/export-candidate.json", '--native', "$nativeInstall/bin/aether_core.dll")
    $script:stage.details = Get-Identity "$evidence/export-candidate.json"
}

# Leaf/path comparison preserves array order and every active value. It is not a count/hash-only
# equality test. Both entire canonical exports are retained, plus precise paths/values on failure.
function Add-CanonicalLeaves($Value, [string] $Path, [Collections.Generic.SortedDictionary[string,string]] $Leaves) {
    if ($Value -is [System.Collections.IDictionary]) {
        $Leaves.Add("$Path/@kind", 'object')
        foreach ($key in $Value.Keys) { Add-CanonicalLeaves $Value[$key] "$Path/$key" $Leaves }
    } elseif ($Value -is [System.Collections.IList]) {
        $Leaves.Add("$Path/@kind", 'array')
        $Leaves.Add("$Path/@length", $Value.Count.ToString([Globalization.CultureInfo]::InvariantCulture))
        for ($index = 0; $index -lt $Value.Count; $index++) { Add-CanonicalLeaves $Value[$index] "$Path/$index" $Leaves }
    } else {
        $Leaves.Add($Path, (ConvertTo-Json -InputObject $Value -Compress -Depth 100))
    }
}
function Compare-Canonical($LeftCanonical, $RightCanonical) {
    $leftLeaves = [Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    $rightLeaves = [Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::Ordinal)
    Add-CanonicalLeaves $LeftCanonical '' $leftLeaves
    Add-CanonicalLeaves $RightCanonical '' $rightLeaves
    $keys = @(@($leftLeaves.Keys) + @($rightLeaves.Keys) | Sort-Object -Unique -CaseSensitive)
    $differences = @(foreach ($key in $keys) {
        $hasLeft = $leftLeaves.ContainsKey($key); $hasRight = $rightLeaves.ContainsKey($key)
        if (-not $hasLeft -or -not $hasRight -or $leftLeaves[$key] -cne $rightLeaves[$key]) {
            [ordered]@{ path = $key; baselinePresent = $hasLeft; candidatePresent = $hasRight
                baseline = $(if ($hasLeft) { $leftLeaves[$key] } else { $null })
                candidate = $(if ($hasRight) { $rightLeaves[$key] } else { $null }) }
        }
    })
    return [pscustomobject]@{ comparedLeaves = $keys.Count; differences = $differences }
}
function Test-ComparatorRejection([string] $Name, $Canonical, [scriptblock] $Mutate) {
    # Only copied exported data changes. No host, native engine, fixture or retained export is changed.
    $copy = ConvertFrom-Json -AsHashtable -InputObject (ConvertTo-Json -InputObject $Canonical -Depth 100)
    & $Mutate $copy
    $comparison = Compare-Canonical $Canonical $copy
    $countsUnchanged = $true
    $arrayLengthsUnchanged = $true
    for ($index = 0; $index -lt $Canonical.states.Count; $index++) {
        if ((ConvertTo-Json -InputObject $Canonical.states[$index].counts -Compress) -cne
            (ConvertTo-Json -InputObject $copy.states[$index].counts -Compress)) { $countsUnchanged = $false }
        if ($Canonical.states[$index].commands.Count -ne $copy.states[$index].commands.Count -or
            $Canonical.states[$index].paintOrder.Count -ne $copy.states[$index].paintOrder.Count) { $arrayLengthsUnchanged = $false }
    }
    $expectedCounts = $countsUnchanged -or $Name -eq 'previous-counter'
    return [ordered]@{ name = $Name; passed = ($comparison.differences.Count -gt 0 -and $expectedCounts -and $arrayLengthsUnchanged)
        priorCountersUnchanged = $countsUnchanged; commandAndPaintOrderCountsUnchanged = $arrayLengthsUnchanged
        differences = $comparison.differences }
}
Invoke-Stage 'export-compare' {
    $left = Get-Content -Raw -LiteralPath "$evidence/export-baseline.json" | ConvertFrom-Json -AsHashtable
    $right = Get-Content -Raw -LiteralPath "$evidence/export-candidate.json" | ConvertFrom-Json -AsHashtable
    $testResult = Get-Content -Raw -LiteralPath "$evidence/tests.json" | ConvertFrom-Json -AsHashtable
    foreach ($arm in @($left, $right)) {
        if ($arm.schema -ne 'managed-results-export-v1' -or $arm.status -ne 'passed' -or
            $arm.loadedNative.sha256 -ne $script:manifest.build.native.sha256 -or
            $arm.inputSha256 -ne $testResult.fixtureSha256 -or $arm.canonical.states.Count -ne 4 -or
            $arm.serializationChecks.Count -ne 4 -or
            @($arm.serializationChecks | Where-Object { $_.passed -ne $true -or $_.fillCommandsVerified -lt 1 -or
                $_.textCommandsVerified -lt 1 -or $_.geometryBoxesVerified -lt 1 }).Count -ne 0) {
            throw 'T4 exporter status, fixture, state count or native identity is invalid.'
        }
    }
    foreach ($pair in @(@($left, $exportBaselineOutput), @($right, $exportCandidateOutput))) {
        foreach ($assembly in $pair[0].assemblies) {
            if ($assembly.sha256 -ne (Get-Identity "$($pair[1])/$($assembly.file)").sha256) {
                throw 'T4 loaded managed assembly differs from the recorded build output.'
            }
        }
    }
    $negativeControls = @(
        Test-ComparatorRejection 'active-command-value' $left.canonical {
            param($copy)
            $command = @($copy.states[0].commands | Where-Object { $_.kind -eq 'FillRoundedRectangleCommand' })[0]
            $command.values.Color.R = [double]$command.values.Color.R + 0.125
        }
        Test-ComparatorRejection 'layout-topology' $left.canonical {
            param($copy)
            if ($copy.states[0].boxes[0].children.Count -lt 1) { throw 'Missing topology negative-control input.' }
            $copy.states[0].boxes[0].children[0] = 0
        }
        Test-ComparatorRejection 'command-order' $left.canonical {
            param($copy)
            $commands = $copy.states[0].commands
            $first = ConvertTo-Json -InputObject $commands[0] -Compress -Depth 100
            $different = -1
            for ($index = 1; $index -lt $commands.Count; $index++) {
                if ((ConvertTo-Json -InputObject $commands[$index] -Compress -Depth 100) -cne $first) {
                    $different = $index; break
                }
            }
            if ($different -lt 0) { throw 'Missing distinct commands for order negative-control.' }
            $temporary = $commands[0]; $commands[0] = $commands[$different]; $commands[$different] = $temporary
        }
        Test-ComparatorRejection 'paint-order' $left.canonical {
            param($copy)
            $order = $copy.states[0].paintOrder
            if ($order.Count -lt 2 -or $order[0] -eq $order[1]) { throw 'Missing distinct paint-order input.' }
            $temporary = $order[0]; $order[0] = $order[1]; $order[1] = $temporary
        }
        Test-ComparatorRejection 'previous-counter' $left.canonical {
            param($copy)
            $copy.states[0].counts.LayoutBuilds = [long]$copy.states[0].counts.LayoutBuilds + 1
        }
    )
    $comparison = Compare-Canonical $left.canonical $right.canonical
    $differences = $comparison.differences
    $controlsPassed = @($negativeControls | Where-Object { $_.passed -ne $true }).Count -eq 0
    Write-Json "$evidence/comparison.json" ([ordered]@{ schema = 'managed-result-comparison-v1'
        status = $(if ($differences.Count -eq 0 -and $controlsPassed) { 'passed' } else { 'failed' })
        baseline = (Get-Identity "$evidence/export-baseline.json"); candidate = (Get-Identity "$evidence/export-candidate.json")
        comparedLeaves = $comparison.comparedLeaves; differences = $differences; negativeControls = $negativeControls; provenance = $script:manifest.comparison
        scope = 'Exact canonical values/order and pre-v2 counters for four states; no timing equality or conformance claim.' })
    if (-not $controlsPassed) { throw 'T4 comparator failed to reject a deliberate canonical data change.' }
    if ($differences.Count -ne 0) { throw 'T4 canonical values differ; retained both exports and precise differences.' }
    $script:stage.details = Get-Identity "$evidence/comparison.json"
}
