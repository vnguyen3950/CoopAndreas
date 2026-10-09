[CmdletBinding()]
param([string]$WorkspaceRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))

$ErrorActionPreference = 'Stop'
$labRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$gameDirectory = Join-Path $labRoot 'game-lab'
$releaseDirectory = Join-Path $labRoot 'artifacts\windows\x86\release'
$stagingDirectory = Join-Path $labRoot 'staging'
$windowedSourceDirectory = Join-Path $labRoot 'tools\windowed-mode-v2.2'
$scriptArtifactDirectory = Join-Path $labRoot 'artifacts\scripts'
$approvedBuildPath = Join-Path $labRoot 'artifacts\approved-build.json'
if (Test-Path -LiteralPath $approvedBuildPath -PathType Leaf) {
    $approvedBuild = Get-Content -LiteralPath $approvedBuildPath -Raw | ConvertFrom-Json
    if ($approvedBuild.Name -notmatch '^mission-batch-[0-9]+$') {
        throw 'Invalid prepared batch name.'
    }
    # Development builds cannot replace the last reviewed, matched update.
    $approvedArtifactDirectory = Join-Path $labRoot ('artifacts\' + $approvedBuild.Name)
    $releaseDirectory = Join-Path $approvedArtifactDirectory 'windows\x86\release'
    $scriptArtifactDirectory = Join-Path $approvedArtifactDirectory 'scripts'
}
$scriptBuildManifestPath = Join-Path $scriptArtifactDirectory 'script-build.json'
$gameExecutable = Join-Path $gameDirectory 'gta_sa.exe'

if (-not (Test-Path -LiteralPath (Join-Path $gameDirectory 'eax_orig.dll') -PathType Leaf)) {
    throw 'The isolated lab and its original eax backup must exist before updating.'
}
if ((Get-FileHash -LiteralPath $gameExecutable -Algorithm SHA256).Hash -ne 'A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26') {
    throw 'The lab executable does not match the supported test copy.'
}

$programPaths = @(
    $gameExecutable,
    (Join-Path $gameDirectory 'LaunchCoopAndreas.exe'),
    (Join-Path $gameDirectory 'server.exe')
)
$runningPrograms = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $programPaths -contains $_.ExecutablePath })
if ($runningPrograms.Count) {
    throw "Close the lab game, launcher, and server before updating. Running PIDs: $($runningPrograms.ProcessId -join ', ')."
}

$filePairs = @(
    @{ Source = 'CoopAndreasSA.dll'; Destination = 'CoopAndreasSA.dll' },
    @{ Source = 'LaunchCoopAndreas.exe'; Destination = 'LaunchCoopAndreas.exe' },
    @{ Source = 'LaunchCoopAndreas.exe.manifest'; Destination = 'LaunchCoopAndreas.exe.manifest' },
    @{ Source = 'server.exe'; Destination = 'server.exe' },
    @{ Source = 'proxy.dll'; Destination = 'eax.dll' }
)
foreach ($filePair in $filePairs) {
    if (-not (Test-Path -LiteralPath (Join-Path $releaseDirectory $filePair.Source) -PathType Leaf)) {
        throw "Missing build output: $($filePair.Source)"
    }
}

$windowedFiles = @('III.VC.SA.WindowedMode.asi', 'III.VC.SA.WindowedMode.ini', 'III.VC.SA.WindowedMode.LICENSE.txt')
$installWindowedMode = Test-Path -LiteralPath (Join-Path $windowedSourceDirectory $windowedFiles[0]) -PathType Leaf
if ($installWindowedMode) {
    foreach ($windowedFile in $windowedFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $windowedSourceDirectory $windowedFile) -PathType Leaf)) {
            throw "Missing windowed-mode file: $windowedFile"
        }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $windowedSourceDirectory $windowedFiles[0]) -Algorithm SHA256).Hash -ne '2D41A4BBA901C05FA31E0CF246C01CFC7EC8C040F1755BD25640B72C0A0282F3') {
        throw 'The windowed-mode module does not match the verified v2.2 release.'
    }
}

$installScriptPack = Test-Path -LiteralPath $scriptBuildManifestPath -PathType Leaf
if ($installScriptPack) {
    $scriptBuildManifest = Get-Content -LiteralPath $scriptBuildManifestPath -Raw | ConvertFrom-Json
    foreach ($scriptFile in @('main.scm','script.img')) {
        $expectedScript = @($scriptBuildManifest.Artifacts | Where-Object { $_.File -eq $scriptFile })
        if ($expectedScript.Count -ne 1 -or
            (Get-FileHash -LiteralPath (Join-Path $scriptArtifactDirectory $scriptFile) -Algorithm SHA256).Hash -ne $expectedScript[0].SHA256) {
            throw "Validated script artifact is missing or changed: $scriptFile"
        }
    }
}

$backupDirectory = Join-Path $labRoot ('backups\before-build-update-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backupDirectory | Out-Null
foreach ($filePair in $filePairs) {
    $destinationFile = Join-Path $gameDirectory $filePair.Destination
    if (Test-Path -LiteralPath $destinationFile) {
        Copy-Item -LiteralPath $destinationFile -Destination (Join-Path $backupDirectory $filePair.Destination)
    }
    $sourceFile = Join-Path $releaseDirectory $filePair.Source
    Copy-Item -LiteralPath $sourceFile -Destination $destinationFile
    Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $stagingDirectory $filePair.Destination)
    if ((Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash) {
        throw "Updated file failed verification: $($filePair.Destination)"
    }
}

if ($installWindowedMode) {
    foreach ($windowedFile in $windowedFiles) {
        $sourceFile = Join-Path $windowedSourceDirectory $windowedFile
        $destinationFile = Join-Path $gameDirectory $windowedFile
        # Preserve the user's saved size, position, and style on subsequent updates.
        if ($windowedFile -like '*.ini' -and (Test-Path -LiteralPath $destinationFile)) {
            continue
        }
        if (Test-Path -LiteralPath $destinationFile) {
            Copy-Item -LiteralPath $destinationFile -Destination (Join-Path $backupDirectory $windowedFile)
        }
        Copy-Item -LiteralPath $sourceFile -Destination $destinationFile
        Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $stagingDirectory $windowedFile)
        if ((Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash) {
            throw "Updated file failed verification: $windowedFile"
        }
    }
}

if ($installScriptPack) {
    $backupScriptDirectory = Join-Path $backupDirectory 'CoopAndreas'
    New-Item -ItemType Directory -Path $backupScriptDirectory | Out-Null
    foreach ($scriptFile in @('main.scm','script.img')) {
        $destinationFile = Join-Path $gameDirectory ('CoopAndreas\' + $scriptFile)
        Copy-Item -LiteralPath $destinationFile -Destination (Join-Path $backupScriptDirectory $scriptFile)
        Copy-Item -LiteralPath (Join-Path $scriptArtifactDirectory $scriptFile) -Destination $destinationFile
        $expectedScript = @($scriptBuildManifest.Artifacts | Where-Object { $_.File -eq $scriptFile })
        if ((Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash -ne $expectedScript[0].SHA256) {
            throw "Updated script failed verification: $scriptFile"
        }
    }
}

$manifest = foreach ($filePair in $filePairs) {
    $file = Get-Item -LiteralPath (Join-Path $gameDirectory $filePair.Destination)
    [PSCustomObject]@{ File = $filePair.Destination; Bytes = $file.Length; SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
}
if ($installWindowedMode) {
    $manifest += foreach ($windowedFile in $windowedFiles) {
        $file = Get-Item -LiteralPath (Join-Path $gameDirectory $windowedFile)
        [PSCustomObject]@{ File = $windowedFile; Bytes = $file.Length; SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
    }
}
if ($installScriptPack) {
    $manifest += foreach ($scriptFile in @('main.scm','script.img')) {
        $file = Get-Item -LiteralPath (Join-Path $gameDirectory ('CoopAndreas\' + $scriptFile))
        [PSCustomObject]@{ File=('CoopAndreas\' + $scriptFile); Bytes=$file.Length; SHA256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
    }
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $gameDirectory 'build-manifest.json') -Encoding UTF8
Get-ChildItem -LiteralPath $stagingDirectory -Recurse -File | Where-Object { $_.Name -ne 'manifest.json' } | ForEach-Object {
    [PSCustomObject]@{ Path = $_.FullName; Bytes = $_.Length; SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stagingDirectory 'manifest.json') -Encoding UTF8
Write-Output "Lab updated. Previous binaries: $backupDirectory"
