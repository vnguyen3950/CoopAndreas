# Portable, headless SCM builds. All build writes remain in script-build; nothing is deployed.
# Example (freeze current work, then build):
# powershell -NoProfile -ExecutionPolicy Bypass -File .\devtools\build-scripts.ps1 -Source WorkingTree -BuildName mission-build-current-001
# Example (HEAD): use -Source Head -BuildName mission-build-head-001
# Rebuild an existing frozen snapshot: -Source ExistingSnapshot -BuildName mission-build-baseline
[CmdletBinding()]
param(
    [ValidateSet('Head','WorkingTree','ExistingSnapshot')][string]$Source = 'Head',
    [ValidatePattern('^mission-(?:build|blockers)-[A-Za-z0-9_-]+$')][string]$BuildName = ('mission-build-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [ValidateRange(1,600)][int]$TimeoutSeconds = 120,
    [string]$WorkspaceRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$lab = [IO.Path]::GetFullPath((Join-Path $WorkspaceRoot 'script-build'))
$build = [IO.Path]::GetFullPath((Join-Path $lab $BuildName))
if ([IO.Path]::GetDirectoryName($build) -ne $lab) { throw 'Build must be a direct child of script-build.' }
$snapshot = Join-Path $build 'snapshot'
$tool = Join-Path $lab 'tools\sanny-builder'
$exe = Join-Path $tool 'sanny.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Portable Sanny Builder is missing.' }
function Write-Json($Value, [string]$Path) {
    $Value | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $Path -Encoding UTF8
}
function Get-Manifest([string]$Root) {
    @(Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName | ForEach-Object {
        [pscustomobject]@{ Path=$_.FullName.Substring($Root.Length+1).Replace('\','/'); Bytes=$_.Length; SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash }
    })
}
function Get-GitFiles {
    $files = @(& git -C $repo ls-files --cached --others --exclude-standard -- scm sdk)
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }
    @($files | Sort-Object -Unique)
}
function Get-EngineCapacities {
    $engine = Join-Path $WorkspaceRoot 'gta-reversed'
    $header = Join-Path $engine 'source\game_sa\Scripts\TheScripts.h'
    $running = Join-Path $engine 'source\game_sa\Scripts\RunningScript.h'
    $chunks = Join-Path $engine 'source\game_sa\Scripts\SCMChunks.hpp'
    $reader = Join-Path $engine 'source\game_sa\Scripts\TheScripts.cpp'
    $missionLoader = Join-Path $engine 'source\game_sa\Scripts\Commands\MissionCommands.cpp'
    $h = Get-Content -LiteralPath $header -Raw
    $r = Get-Content -LiteralPath $running -Raw
    $c = Get-Content -LiteralPath $chunks -Raw
    $fields = 'uint32\s+m_MainScriptSize;\s+uint32\s+m_LargestMissionScriptSize;\s+uint16\s+m_NumberOfMissionScripts;\s+uint16\s+m_NumberOfExclusiveMissionScripts;\s+uint32\s+m_LargestNumberOfMissionScriptLocalVars;\s+uint32\s+m_MissionScriptOffsets\[\];'
    if ($c -notmatch $fields -or $c -notmatch 'VALIDATE_SIZE\(tSCMChunkHeader, 0x8\)' -or $c -notmatch 'VALIDATE_SIZE\(tSCMScriptFileInfoChunk, 0x18') { throw 'Engine SCM header layout changed; review before parsing artifacts.' }
    $limits = [ordered]@{}
    foreach ($definition in @(
        @('MainBlockBytes',$h,'MAIN_SCRIPT_SIZE'),
        @('MissionBytes',$h,'MISSION_SCRIPT_SIZE'),
        @('MissionLocalSlots',$h,'MAX_NUM_LOCAL_VARIABLES_FOR_CURRENT_MISSION'),
        @('MissionCount',$h,'MAX_NUM_MISSION_SCRIPTS'),
        @('ThreadLocalSlots',$r,'MAX_LOCAL_VARS'),
        @('ThreadTimerSlots',$r,'MAX_NUM_TIMERS')
    )) {
        $match = [regex]::Match($definition[1], '\b'+$definition[2]+"\s*=\s*([0-9']+)")
        if (!$match.Success) { throw "Engine capacity constant missing: $($definition[2])" }
        $limits[$definition[0]] = [int]$match.Groups[1].Value.Replace("'",'')
    }
    $limits.ThreadLocalSlotsIncludingTimers = $limits.ThreadLocalSlots + $limits.ThreadTimerSlots
    $limits.EngineHead = (& git -C $engine rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Could not identify adjacent engine revision.' }
    $limits.Sources = @($header,$running,$chunks,$reader,$missionLoader | ForEach-Object {
        [ordered]@{ File=$_; SHA256=(Get-FileHash -LiteralPath $_).Hash }
    })
    return $limits
}
function Get-SCMValidation([byte[]]$Bytes, [string]$SourceText, $Limits) {
    # Offsets come from packed tSCMChunkHeader/tSCMScriptFileInfoChunk in SCMChunks.hpp.
    # TheScripts.cpp ReadMultiScriptFileOffsetsFromScript copies these exact fields.
    $errors = New-Object 'Collections.Generic.List[string]'
    if ($Bytes.Length -lt 8 -or $Bytes[7] -ne [byte][char]'s') { throw 'Invalid SA global-variable chunk.' }
    $modelSegment = [BitConverter]::ToUInt32($Bytes,3)
    if ($modelSegment+8 -gt $Bytes.Length -or $Bytes[$modelSegment+7] -ne 0) { throw 'Invalid used-objects chunk.' }
    $missionSegment = [BitConverter]::ToUInt32($Bytes,$modelSegment+3)
    if ($missionSegment+24 -gt $Bytes.Length -or $Bytes[$missionSegment+7] -ne 1) { throw 'Invalid script-file-info chunk.' }
    $externalSegment = [BitConverter]::ToUInt32($Bytes,$missionSegment+3)
    if ($externalSegment+16 -gt $Bytes.Length -or $Bytes[$externalSegment+7] -ne 2) { throw 'Invalid streamed-script-info chunk.' }
    $mainSize = [BitConverter]::ToUInt32($Bytes,$missionSegment+8)
    $declaredLargest = [BitConverter]::ToUInt32($Bytes,$missionSegment+12)
    $missionCount = [BitConverter]::ToUInt16($Bytes,$missionSegment+16)
    $missionLocals = [BitConverter]::ToUInt32($Bytes,$missionSegment+20)
    $externalCount = [BitConverter]::ToUInt32($Bytes,$externalSegment+12)
    $expectedMissions = [int]([regex]::Match($SourceText,'(?m)^DEFINE MISSIONS (\d+)').Groups[1].Value)
    $expectedExternals = [int]([regex]::Match($SourceText,'(?m)^DEFINE EXTERNAL_SCRIPTS (\d+)').Groups[1].Value)+1
    if ($mainSize -gt $Limits.MainBlockBytes) { $errors.Add("Main block $mainSize bytes exceeds engine buffer $($Limits.MainBlockBytes).") }
    if ($declaredLargest -gt $Limits.MissionBytes) { $errors.Add("Declared largest mission $declaredLargest bytes exceeds engine buffer $($Limits.MissionBytes).") }
    if ($missionLocals -gt $Limits.MissionLocalSlots) { $errors.Add("Mission locals $missionLocals slots exceed engine array $($Limits.MissionLocalSlots).") }
    if ($missionCount -gt $Limits.MissionCount) { $errors.Add("Mission count $missionCount exceeds engine offset array $($Limits.MissionCount).") }
    if ($missionCount -ne $expectedMissions) { $errors.Add('Compiled mission count differs from frozen source.') }
    if ($missionSegment+24+4*$missionCount -gt $externalSegment) { throw 'Mission offset table exceeds its chunk.' }
    $labels = @{}
    foreach ($match in [regex]::Matches($SourceText,'(?m)^DEFINE MISSION\s+(\d+)\s+AT\s+@(\w+)([^\r\n]*)')) {
        $id = [int]$match.Groups[1].Value
        if ($labels.ContainsKey($id)) { throw "Duplicate mission ID $id in source." }
        $labels[$id] = [ordered]@{ Label=$match.Groups[2].Value; Title=($match.Groups[3].Value -replace '^\s*//\s*','').Trim() }
    }
    if ($labels.Count -ne $missionCount) { $errors.Add('Source mission definitions differ from compiled offset table.') }
    $offsets = @()
    for ($i=0; $i -lt $missionCount; $i++) { $offsets += [BitConverter]::ToUInt32($Bytes,$missionSegment+24+4*$i) }
    $sorted = @($offsets | Sort-Object -Unique)
    if ($sorted.Count -ne $missionCount -or !$missionCount) { throw 'Mission offsets must be nonempty and unique.' }
    if ($sorted[0] -ne $mainSize) { $errors.Add('First mission offset differs from m_MainScriptSize.') }
    $ends = @{}
    for ($i=0; $i -lt $sorted.Count; $i++) {
        $start = $sorted[$i]
        $end = if ($i+1 -lt $sorted.Count) { $sorted[$i+1] } else { $Bytes.Length }
        if ($start -lt $externalSegment+16 -or $end -le $start -or $end -gt $Bytes.Length) { throw 'Mission offset lies outside compiled mission data.' }
        $ends[$start] = $end
    }
    # AddExtraInfo=0 is mandatory above: final mission ends at EOF, without debug metadata.
    $missions = @()
    for ($i=0; $i -lt $missionCount; $i++) {
        if (!$labels.ContainsKey($i)) { throw "Source lacks mission ID $i." }
        $start = $offsets[$i]; $end = $ends[$start]; $size = $end-$start
        $missions += [pscustomobject][ordered]@{ MissionId=$i; Label=$labels[$i].Label; Title=$labels[$i].Title; StartOffset=$start; EndOffset=$end; Bytes=$size; LimitBytes=$Limits.MissionBytes; HeadroomBytes=($Limits.MissionBytes-$size) }
        if ($size -gt $Limits.MissionBytes) { $errors.Add("Mission #$i @$($labels[$i].Label) is $size bytes, exceeding engine mission buffer $($Limits.MissionBytes).") }
    }
    $largest = $missions | Sort-Object Bytes -Descending | Select-Object -First 1
    if ($largest.Bytes -ne $declaredLargest) { $errors.Add('Computed largest mission differs from m_LargestMissionScriptSize.') }
    return [ordered]@{ Missions=$missionCount; ExpectedMissions=$expectedMissions; ExternalScriptsIncludingAAA=$externalCount; ExpectedIMGEntries=$expectedExternals; MainBlockBytes=$mainSize; MainBlockLimitBytes=$Limits.MainBlockBytes; MainBlockHeadroomBytes=($Limits.MainBlockBytes-$mainSize); LargestMissionBytes=$declaredLargest; LargestMission=$largest; MissionLocalSlots=$missionLocals; MissionLocalLimitSlots=$Limits.MissionLocalSlots; MissionLocalHeadroomSlots=($Limits.MissionLocalSlots-$missionLocals); EngineCapacities=$Limits; PerMission=$missions; ValidationErrors=@($errors.ToArray()); EngineCapacitiesValid=($errors.Count -eq 0); LocalValidation='Compiled mission-local count checked against engine array; Sanny Compiler::CheckLocalVariables=1 checks ordinary thread locals (32 + 2 timers) and mission operand ranges.' }
}
$mutex = New-Object Threading.Mutex($false, 'Local\CoopAndreasPortableSannyBuild')
$locked = $false
try {
    $locked = $mutex.WaitOne(0)
    if (!$locked) { throw 'Another SCM build is using this local compiler; retry when it finishes.' }
    $engineCapacities = Get-EngineCapacities
    $head = (& git -C $repo rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'git rev-parse failed.' }
    if ($Source -ne 'ExistingSnapshot') {
        if (Test-Path -LiteralPath $build) { throw 'Build directory already exists. Choose a fresh BuildName.' }
        New-Item -ItemType Directory -Path $snapshot -Force | Out-Null
        if ($Source -eq 'Head') {
            $archive = Join-Path $build 'head-scm-sdk.zip'
            & git -C $repo archive --format=zip "--output=$archive" $head scm sdk
            if ($LASTEXITCODE -ne 0) { throw 'git archive failed.' }
            Expand-Archive -LiteralPath $archive -DestinationPath $snapshot
        } else {
            # Verify both the source list and every copied byte after the copy.
            # Abort on concurrent edits rather than compile a mixed snapshot.
            $files = @(Get-GitFiles)
            $before = @{}
            foreach ($relative in $files) {
                $src = Join-Path $repo $relative
                if (!(Test-Path -LiteralPath $src -PathType Leaf)) { continue }
                $before[$relative] = (Get-FileHash -LiteralPath $src).Hash
                $dest = Join-Path $snapshot $relative
                New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($dest)) -Force | Out-Null
                Copy-Item -LiteralPath $src -Destination $dest
            }
            if (($files -join "`n") -ne ((Get-GitFiles) -join "`n")) { throw 'Source file list changed during snapshot. Retry with a new BuildName.' }
            foreach ($relative in $before.Keys) {
                $src = Join-Path $repo $relative
                $dest = Join-Path $snapshot $relative
                if (!(Test-Path -LiteralPath $src) -or (Get-FileHash -LiteralPath $src).Hash -ne $before[$relative] -or (Get-FileHash -LiteralPath $dest).Hash -ne $before[$relative]) {
                    throw "Source changed while copying: $relative. Retry with a new BuildName."
                }
            }
        }
        Write-Json ([ordered]@{ Source=$Source; Head=$head; FrozenAtUTC=[DateTime]::UtcNow.ToString('o') }) (Join-Path $build 'snapshot-origin.json')
    }
    $main = Join-Path $snapshot 'scm\main.txt'
    $sdk = Join-Path $snapshot 'sdk\Sanny Builder 4\data\sa_sbl_coopandreas'
    if (!(Test-Path -LiteralPath $main) -or !(Test-Path -LiteralPath (Join-Path $sdk 'mode.xml'))) { throw 'Snapshot must contain scm and repository SDK.' }
    $manifest = @(Get-Manifest $snapshot)
    Write-Json $manifest (Join-Path $build 'snapshot-manifest.json')
    $sdkTarget = Join-Path $tool 'data\sa_sbl_coopandreas'
    # Only local tool data is replaced; tracked SDK and pinned outputs are untouched.
    if (Test-Path -LiteralPath $sdkTarget) {
        $resolved = [IO.Path]::GetFullPath($sdkTarget)
        if ($resolved -ne [IO.Path]::GetFullPath((Join-Path $tool 'data\sa_sbl_coopandreas'))) { throw 'Unexpected SDK destination.' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    Copy-Item -LiteralPath $sdk -Destination (Join-Path $tool 'data') -Recurse
    $runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $run = Join-Path $build "runs\$runId"
    $output = Join-Path $run 'output'
    $logs = Join-Path $run 'logs'
    New-Item -ItemType Directory -Path $output,$logs -Force | Out-Null
    $compiledMain = Join-Path $output 'main.scm'
    $argsLine = '--no-splash --mode sa_sbl_coopandreas -o Editor::ShowProgress 0 -o Editor::ShowReport 0 -o Compiler::ShowIMGWarning 0 -o Compiler::CheckConditions 1 -o Compiler::CheckLocalVariables 1 -o Compiler::AddExtraInfo 0 --compile "' + $main + '" "' + $compiledMain + '"'
    $command = '"' + $exe + '" ' + $argsLine
    $command | Set-Content -LiteralPath (Join-Path $logs 'command.txt') -Encoding UTF8
    $sdkJson = Get-Content -LiteralPath (Join-Path $sdk 'sa_coop.json') -Raw | ConvertFrom-Json
    $coop = @($sdkJson.extensions | Where-Object name -eq 'CoopAndreas')[0].commands
    Write-Json $coop (Join-Path $logs 'coop-opcodes.json')
    $oldLogs = @{}
    foreach ($name in @('compile.log','core.log','debugger.log')) {
        $path = Join-Path $tool $name
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $logs "before-$name"); $oldLogs[$name] = (Get-FileHash -LiteralPath $path).Hash }
    }
    # A previous failure must not contaminate this run, even if errors repeat.
    $previousErrorLog = Join-Path $tool 'compile.log'
    if (Test-Path -LiteralPath $previousErrorLog) { Remove-Item -LiteralPath $previousErrorLog }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$exe; $info.Arguments=$argsLine; $info.WorkingDirectory=$logs
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $process = New-Object Diagnostics.Process
    $process.StartInfo=$info
    $startedUTC = [DateTime]::UtcNow
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process.Start() | Out-Null
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $completed = $process.WaitForExit($TimeoutSeconds*1000)
    if (!$completed) { $process.Kill(); $process.WaitForExit() }
    $timer.Stop()
    $exitCode = $process.ExitCode
    $stdoutTask.Result | Set-Content -LiteralPath (Join-Path $logs 'stdout.log') -Encoding UTF8
    $stderrTask.Result | Set-Content -LiteralPath (Join-Path $logs 'stderr.log') -Encoding UTF8
    $compilerErrorLog = $false
    foreach ($name in @('compile.log','core.log','debugger.log')) {
        $path = Join-Path $tool $name
        if (Test-Path -LiteralPath $path) {
            Copy-Item -LiteralPath $path -Destination (Join-Path $logs $name)
            if ($name -eq 'compile.log') { $compilerErrorLog=$true }
        }
    }
    if (Test-Path -LiteralPath (Join-Path $logs 'compile.log')) { $compilerErrorLog=$true }
    $artifacts = @(Get-Manifest $output)
    $after = @(Get-Manifest $snapshot)
    $snapshotUnchanged = (($manifest | ConvertTo-Json -Depth 4 -Compress) -eq ($after | ConvertTo-Json -Depth 4 -Compress))
    $hasPack = (Test-Path -LiteralPath $compiledMain) -and (Test-Path -LiteralPath (Join-Path $output 'script.img'))
    $validation = $null
    if ($hasPack) {
      try {
        $scmBytes = [IO.File]::ReadAllBytes($compiledMain)
        $text = Get-Content -LiteralPath $main -Raw
        $validation = Get-SCMValidation $scmBytes $text $engineCapacities
        $missionCount = $validation.Missions
        $externalCount = $validation.ExternalScriptsIncludingAAA
        $expectedMissions = $validation.ExpectedMissions
        $expectedExternals = $validation.ExpectedIMGEntries
        $validation.PerMission | Export-Csv -LiteralPath (Join-Path $run 'mission-sizes.csv') -NoTypeInformation -Encoding UTF8
        $assignedBaseline = Join-Path $lab 'mission-build-baseline\assigned-mission-sizes.csv'
        if (Test-Path -LiteralPath $assignedBaseline) {
            $assigned = @{}
            Import-Csv -LiteralPath $assignedBaseline | ForEach-Object { $assigned[$_.Label] = $_.AssignedWriter }
            $validation.PerMission | Where-Object { $assigned.ContainsKey($_.Label) } | Select-Object *,@{n='AssignedWriter';e={$assigned[$_.Label]}} | Export-Csv -LiteralPath (Join-Path $run 'assigned-mission-sizes.csv') -NoTypeInformation -Encoding UTF8
        }
        $img = [IO.File]::ReadAllBytes((Join-Path $output 'script.img'))
        if ($img.Length -lt 8) { throw 'IMG header is truncated.' }
        $magic = [Text.Encoding]::ASCII.GetString($img,0,4)
        $count = [BitConverter]::ToUInt32($img,4)
        $entries = @()
        $boundsOK = (8 + 32*$count -le $img.Length)
        for ($i=0; $boundsOK -and $i -lt $count; $i++) {
            $offset = 8+32*$i
            $sector = [BitConverter]::ToUInt32($img,$offset)
            $sectors = [BitConverter]::ToUInt16($img,$offset+4)
            if ($sectors -eq 0) { $sectors=[BitConverter]::ToUInt16($img,$offset+6) }
            $name=[Text.Encoding]::ASCII.GetString($img,$offset+8,24).TrimEnd([char]0)
            $boundsOK = ($sector*2048 -ge 8+32*$count -and ($sector+$sectors)*2048 -le $img.Length)
            $entries += [ordered]@{Name=$name; Sector=$sector; Sectors=$sectors}
        }
        $validation.IMGEntries=$count; $validation.IMGMagic=$magic; $validation.IMGBoundsValid=$boundsOK; $validation.Entries=$entries
        $hasPack = $validation.EngineCapacitiesValid -and $boundsOK -and $magic -eq 'VER2' -and $missionCount -eq $expectedMissions -and $count -eq $expectedExternals -and $externalCount -eq $expectedExternals
      } catch {
        $hasPack=$false
        $validation=[ordered]@{ EngineCapacities=$engineCapacities; EngineCapacitiesValid=$false; ValidationErrors=@($_.Exception.Message) }
      }
    }
    $success = $completed -and $exitCode -eq 0 -and !$compilerErrorLog -and $snapshotUnchanged -and $hasPack
    $report=[ordered]@{ Success=$success; Source=$Source; HeadAtInvocation=$head; StartedUTC=$startedUTC.ToString('o'); DurationSeconds=$timer.Elapsed.TotalSeconds; ToolVersion=(Get-Item -LiteralPath $exe).VersionInfo.FileVersion; ToolSHA256=(Get-FileHash -LiteralPath $exe).Hash; Mode='sa_sbl_coopandreas'; Command=$command; WorkingDirectory=$logs; ExitCode=$exitCode; TimedOut=(!$completed); CompilerErrorLogPresent=$compilerErrorLog; SnapshotUnchanged=$snapshotUnchanged; CoopOpcodeCount=$coop.Count; Artifacts=$artifacts; Validation=$validation; ReleaseURL='https://github.com/sannybuilder/dev/releases/tag/v4.2.0'; CLIDocs='https://docs.sannybuilder.com/editor/cli'; ModeDocs='https://docs.sannybuilder.com/edit-modes' }
    Write-Json $report (Join-Path $run 'build-report.json')
    Write-Output "Build report: $(Join-Path $run 'build-report.json')"
    Write-Output "Staged outputs: $output"
    if (!$success) { throw "Build failed or validation failed; inspect $logs and build-report.json." }
    Write-Output "SUCCESS: $missionCount missions, $count IMG entries, $($coop.Count) custom opcode definitions. No deployment performed."
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
