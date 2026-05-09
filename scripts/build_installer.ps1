$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $root "build\Desktop_Qt_6_9_3_MinGW_64_bit-Release"
$stageDir = "C:\ptstage\ProviTracker"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$qtBin = "C:\Qt\6.9.3\mingw_64\bin"
$mingwBin = "C:\Qt\Tools\mingw1310_64\bin"
$windeployqt = Join-Path $qtBin "windeployqt.exe"
$iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
$iss = Join-Path $root "installer\ProviTracker.iss"

if (!(Test-Path $cmake)) { throw "CMake blev ikke fundet: $cmake" }
if (!(Test-Path $windeployqt)) { throw "windeployqt blev ikke fundet: $windeployqt" }
if (!(Test-Path $iscc)) { throw "Inno Setup compiler blev ikke fundet: $iscc" }

$env:Path = "$mingwBin;$qtBin;$env:Path"

& $cmake --build $releaseDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Release build fejlede." }

& $windeployqt --release --compiler-runtime (Join-Path $releaseDir "ProvisionTrackerV2.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt fejlede." }

if (Test-Path $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stageDir | Out-Null

Copy-Item (Join-Path $releaseDir "ProvisionTrackerV2.exe") $stageDir -Force
Copy-Item (Join-Path $releaseDir "*.dll") $stageDir -Force
foreach ($name in @("platforms", "styles", "iconengines", "imageformats", "networkinformation", "generic", "tls", "translations")) {
    $src = Join-Path $releaseDir $name
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $stageDir $name) -Recurse -Force
    }
}

$workerStage = Join-Path $stageDir "intramanager_worker"
New-Item -ItemType Directory -Path $workerStage | Out-Null
Copy-Item (Join-Path $releaseDir "intramanager_worker\intramanager_sync.exe") $workerStage -Force
robocopy (Join-Path $releaseDir "intramanager_worker\_internal") (Join-Path $workerStage "_internal") /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -gt 7) { throw "Kopiering af intramanager_worker fejlede." }

& $iscc "/DBuildDir=$stageDir" $iss
if ($LASTEXITCODE -ne 0) { throw "Inno Setup build fejlede." }

$installer = Join-Path $root "dist\ProviBeregnerSetup-1.3.5.exe"
if (!(Test-Path $installer)) { throw "Installer blev ikke oprettet: $installer" }

Write-Host "Installer klar: $installer"
