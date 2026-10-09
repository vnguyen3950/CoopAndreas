[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReportPath, [string]$WorkspaceRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))

$ErrorActionPreference = 'Stop'
$reportFile = Get-Item -LiteralPath $ReportPath
$report = Get-Content -LiteralPath $reportFile.FullName -Raw | ConvertFrom-Json
if (-not $report.Success -or -not $report.SnapshotUnchanged) {
    throw 'Only a successful, validated script build can be staged.'
}
if ($report.PreliminaryOnly -or $report.SemanticReview.Status -ne 'READY' -or
    ($null -ne $report.EligibleForStaging -and -not $report.EligibleForStaging)) {
    throw 'This script build has not completed source review and is not eligible for staging.'
}
$runDirectory = $reportFile.Directory.FullName
$buildDirectory = $reportFile.Directory.Parent.Parent.FullName
$manifestPath = Join-Path $buildDirectory 'snapshot-manifest.json'
$snapshotFiles = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$repositoryDirectory = Split-Path -Parent $PSScriptRoot
foreach ($sourceFile in $snapshotFiles) {
    $currentFile = Join-Path $repositoryDirectory $sourceFile.Path
    if (-not (Test-Path -LiteralPath $currentFile -PathType Leaf) -or
        (Get-FileHash -LiteralPath $currentFile -Algorithm SHA256).Hash -ne $sourceFile.SHA256) {
        throw "Working source differs from the compiled snapshot: $($sourceFile.Path)"
    }
}

$scriptArtifacts = Join-Path $WorkspaceRoot 'artifacts\scripts'
$scriptStaging = Join-Path $WorkspaceRoot 'staging\CoopAndreas'
New-Item -ItemType Directory -Path $scriptArtifacts,$scriptStaging -Force | Out-Null
$validatedArtifacts = foreach ($scriptName in @('main.scm','script.img')) {
    $compiledFile = Join-Path $runDirectory ('output\' + $scriptName)
    $expected = @($report.Artifacts | Where-Object { $_.Path -eq $scriptName })
    if ($expected.Count -ne 1 -or
        (Get-FileHash -LiteralPath $compiledFile -Algorithm SHA256).Hash -ne $expected[0].SHA256) {
        throw "Compiled artifact is missing or changed: $scriptName"
    }
    [PSCustomObject]@{ File=$scriptName; Bytes=$expected[0].Bytes; SHA256=$expected[0].SHA256 }
}
foreach ($artifact in $validatedArtifacts) {
    $compiledFile = Join-Path $runDirectory ('output\' + $artifact.File)
    foreach ($destinationDirectory in @($scriptArtifacts,$scriptStaging)) {
        $destinationFile = Join-Path $destinationDirectory $artifact.File
        Copy-Item -LiteralPath $compiledFile -Destination $destinationFile
        if ((Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash -ne $artifact.SHA256) {
            throw "Staged artifact failed verification: $($artifact.File)"
        }
    }
}
[ordered]@{
    StagedUTC=[DateTime]::UtcNow.ToString('o')
    BuildReport=$reportFile.FullName
    SourceManifest=$manifestPath
    SourceHead=$report.HeadAtInvocation
    Mode=$report.Mode
    Artifacts=@($validatedArtifacts)
    RuntimeValidated=$false
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $scriptArtifacts 'script-build.json') -Encoding UTF8
Write-Output "Validated script pack staged in $scriptArtifacts. Game lab was not modified."
