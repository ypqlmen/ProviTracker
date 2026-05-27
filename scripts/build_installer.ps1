$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $root "build\Desktop_Qt_6_9_3_MinGW_64_bit-Release"
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$stageDir = Join-Path $tempRoot "ProviTrackerBuildStage"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$qtBin = "C:\Qt\6.9.3\mingw_64\bin"
$mingwBin = "C:\Qt\Tools\mingw1310_64\bin"
$windeployqt = Join-Path $qtBin "windeployqt.exe"
$iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
$iss = Join-Path $root "installer\ProviTracker.iss"
$winSparkleDll = Join-Path $root "third_party\winsparkle\bin\WinSparkle.dll"
$workerSourceDir = Join-Path $root "intramanager_worker"
$workerBuildDir = Join-Path $releaseDir "intramanager_worker"
$workerPython = Join-Path $workerBuildDir ".venv\Scripts\python.exe"

if (!(Test-Path $cmake)) { throw "CMake blev ikke fundet: $cmake" }
if (!(Test-Path $windeployqt)) { throw "windeployqt blev ikke fundet: $windeployqt" }
if (!(Test-Path $iscc)) { throw "Inno Setup compiler blev ikke fundet: $iscc" }
if (!(Test-Path $winSparkleDll)) { throw "WinSparkle.dll blev ikke fundet: $winSparkleDll" }

$env:Path = "$mingwBin;$qtBin;$env:Path"

& $cmake --build $releaseDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Release build fejlede." }

if (!(Test-Path $workerPython)) { throw "Python venv blev ikke fundet: $workerPython" }
Copy-Item (Join-Path $workerSourceDir "intramanager_sync.py") $workerBuildDir -Force
Copy-Item (Join-Path $workerSourceDir "intramanager_sync.spec") $workerBuildDir -Force
$workerNestedBrowsers = Join-Path $workerBuildDir ".venv\Lib\site-packages\playwright\driver\package\.local-browsers"
if (Test-Path $workerNestedBrowsers) {
    $resolvedNestedBrowsers = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $workerNestedBrowsers).Path)
    $resolvedWorkerBuildDir = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $workerBuildDir).Path)
    if (-not $resolvedNestedBrowsers.StartsWith($resolvedWorkerBuildDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Afviser at slette Playwright browser-cache uden for worker build-mappen: $resolvedNestedBrowsers"
    }
    $longNestedBrowsers = '\\?\' + $resolvedNestedBrowsers
    [System.IO.Directory]::Delete($longNestedBrowsers, $true)
}
Push-Location $workerBuildDir
try {
    & $workerPython -m PyInstaller "intramanager_sync.spec" --noconfirm
    if ($LASTEXITCODE -ne 0) { throw "PyInstaller build af intramanager_worker fejlede." }
} finally {
    Pop-Location
}
Copy-Item (Join-Path $workerBuildDir "dist\intramanager_sync\intramanager_sync.exe") (Join-Path $workerBuildDir "intramanager_sync.exe") -Force
robocopy (Join-Path $workerBuildDir "dist\intramanager_sync\_internal") (Join-Path $workerBuildDir "_internal") /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -gt 7) { throw "Opdatering af intramanager_worker _internal fejlede." }

Copy-Item -LiteralPath $winSparkleDll -Destination $releaseDir -Force

& $windeployqt --release --compiler-runtime (Join-Path $releaseDir "ProvisionTrackerV2.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt fejlede." }

if (Test-Path $stageDir) {
    $resolvedStage = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $stageDir).Path)
    if (-not $resolvedStage.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Afviser at slette staging uden for tempmappen: $resolvedStage"
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
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
$nestedBrowsers = Join-Path $workerStage "_internal\playwright\driver\package\.local-browsers"
if (Test-Path $nestedBrowsers) {
    Remove-Item -LiteralPath $nestedBrowsers -Recurse -Force
}
$browserSrc = Join-Path $releaseDir "intramanager_worker\b"
if (!(Test-Path $browserSrc)) {
    throw "Playwright browserpakken blev ikke fundet i den korte worker-mappe: $browserSrc"
}
robocopy $browserSrc (Join-Path $workerStage "b") /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -gt 7) { throw "Kopiering af Playwright browserpakke fejlede." }

& $iscc "/DBuildDir=$stageDir" $iss
if ($LASTEXITCODE -ne 0) { throw "Inno Setup build fejlede." }

$installer = Join-Path $root "dist\ProviBeregnerSetup-1.5.0.exe"
if (!(Test-Path $installer)) { throw "Installer blev ikke oprettet: $installer" }

$updateZip = Join-Path $root "dist\ProviTrackerUpdate-1.5.0.zip"
if (Test-Path $updateZip) {
    Remove-Item -LiteralPath $updateZip -Force
}
Compress-Archive -LiteralPath $installer -DestinationPath $updateZip -Force
if (!(Test-Path $updateZip)) { throw "Update-zip blev ikke oprettet: $updateZip" }

Write-Host "Installer klar: $installer"
Write-Host "Update-zip klar: $updateZip"
