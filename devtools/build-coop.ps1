[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Mode = 'release',
    [ValidateSet('client', 'server', 'proxy', 'launcher')]
    [string[]]$Targets = @('client', 'server', 'proxy', 'launcher'),
    [string]$MapFile = '',
    [string]$WorkspaceRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

$ErrorActionPreference = 'Stop'
$labDirectory = [IO.Path]::GetFullPath($WorkspaceRoot)
$repositoryDirectory = Split-Path -Parent $PSScriptRoot
$xmakeExecutable = Join-Path $labDirectory 'tools\xmake\xmake\xmake.exe'
$buildDirectory = Join-Path $labDirectory 'artifacts'
$savedBuildEnvironment = @{}

if (-not (Test-Path -LiteralPath $xmakeExecutable -PathType Leaf)) {
    throw "Portable xmake not found at $xmakeExecutable"
}

foreach ($variableName in @('GTA_SA_DIR', 'XMAKE_GLOBALDIR', 'XMAKE_CONFIGDIR')) {
    $savedBuildEnvironment[$variableName] = [Environment]::GetEnvironmentVariable($variableName, 'Process')
}

Push-Location -LiteralPath $repositoryDirectory
try {
    # The upstream client target writes directly into GTA_SA_DIR when it is set.
    [Environment]::SetEnvironmentVariable('GTA_SA_DIR', $null, 'Process')
    $env:XMAKE_GLOBALDIR = Join-Path $labDirectory 'tools\xmake-state'
    $env:XMAKE_CONFIGDIR = Join-Path $labDirectory 'tools\xmake-config'

    $configureArguments = @('f', '-p', 'windows', '-a', 'x86', '-m', $Mode, '--vs=2022', '-o', $buildDirectory)
    if ($MapFile) {
        $configureArguments += '--ldflags=/MAP:' + [IO.Path]::GetFullPath($MapFile)
        $configureArguments += '--shflags=/MAP:' + [IO.Path]::GetFullPath($MapFile)
    }
    else {
        $configureArguments += '--ldflags='
        $configureArguments += '--shflags='
    }
    & $xmakeExecutable @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'Xmake configuration failed.' }

    foreach ($targetName in $Targets) {
        & $xmakeExecutable build $targetName
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $targetName." }
    }
    Write-Output "Built $($Targets -join ', ') into $buildDirectory"
}
finally {
    Pop-Location
    foreach ($variableName in $savedBuildEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($variableName, $savedBuildEnvironment[$variableName], 'Process')
    }
}
