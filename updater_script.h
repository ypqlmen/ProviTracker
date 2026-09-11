#pragma once

// Variable assignments are written before this body, as UTF-8 with a BOM for
// Windows PowerShell 5.1. The application stays open until readyPath exists.
static constexpr const char* updateHelperBody = R"PS(
$ErrorActionPreference = 'Stop'
$applicationClosed = $false
$installSucceeded = $false
try {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $logPath) | Out-Null
    Set-Content -LiteralPath $logPath -Encoding UTF8 -Value ('Starting update ' + (Get-Date).ToString('s') + ' to ' + $targetDir)
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'Installer not found' }
    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    $writeProbe = Join-Path $targetDir ('.update-' + [guid]::NewGuid().ToString())
    Set-Content -LiteralPath $writeProbe -Value 'test'
    Remove-Item -LiteralPath $writeProbe
    Set-Content -LiteralPath $readyPath -Value 'ready'
    $parent = Get-Process -Id $parentPid -ErrorAction SilentlyContinue
    if ($parent) { $parent | Wait-Process -Timeout 30 -ErrorAction Stop }
    $applicationClosed = $true
    $arguments += ('/LOG="' + $logPath + '.installer.log"')
    $proc = Start-Process -FilePath $installer -ArgumentList $arguments -PassThru -Wait -ErrorAction Stop
    $exitCode = $proc.ExitCode
    Add-Content -LiteralPath $logPath -Encoding UTF8 -Value ('Installer exit code: ' + $exitCode)
    if (@(0, 42) -notcontains $exitCode) { throw ('Installer failed with exit code ' + $exitCode) }
    if (-not (Test-Path -LiteralPath $targetApp -PathType Leaf)) { throw 'Installed application not found' }
    Start-Process -FilePath $targetApp -WorkingDirectory (Split-Path -Parent $targetApp) -ErrorAction Stop | Out-Null
    $installSucceeded = $true
    Set-Content -LiteralPath $markerPath -Encoding UTF8 -Value ('success ' + (Get-Date).ToString('s') + ' ' + $targetApp)
    Add-Content -LiteralPath $logPath -Encoding UTF8 -Value ('Restarted ' + $targetApp)
} catch {
    $failure = $_.Exception.Message
    Add-Content -LiteralPath $logPath -Encoding UTF8 -Value ('FAILED: ' + $failure) -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $markerPath -Encoding UTF8 -Value ('failed ' + (Get-Date).ToString('s') + ' ' + $failure) -ErrorAction SilentlyContinue
    if ($applicationClosed -and (Test-Path -LiteralPath $currentApp)) {
        Start-Process -FilePath $currentApp -WorkingDirectory (Split-Path -Parent $currentApp) -ErrorAction SilentlyContinue | Out-Null
    }
} finally {
    # Retain failed packages and both logs for diagnosis. Never report a failed
    # installation as successful or delete its evidence.
    if ($installSucceeded) {
        Set-Location -LiteralPath $env:TEMP
        Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
}
)PS";
