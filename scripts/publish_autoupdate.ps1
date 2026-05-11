param(
    [string]$Version = "1.3.11",
    [string]$Installer = "$PSScriptRoot\..\dist\ProviBeregnerSetup-1.3.11.exe",
    [string]$Repo = "ypqlmen/ProviTracker",
    [string]$Tag = "autoupdate",
    [string]$Branch = "main",
    [string]$Appcast = "$PSScriptRoot\..\appcast.xml"
)

$ErrorActionPreference = "Stop"

if (!(Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI (gh) blev ikke fundet. Installer GitHub CLI eller upload asset manuelt i GitHub Releases."
}

$installerPath = Resolve-Path $Installer
$assetName = Split-Path -Leaf $installerPath
$appcastPath = Resolve-Path $Appcast
$appcastText = Get-Content $appcastPath -Raw

[xml]$null = $appcastText
if ($appcastText -notmatch [regex]::Escape($assetName)) {
    throw "appcast.xml peger ikke paa $assetName."
}
if ($appcastText -notmatch "sparkle:version=`"$([regex]::Escape($Version))`"") {
    throw "appcast.xml har ikke sparkle:version=$Version."
}

gh release view $Tag --repo $Repo *> $null
if ($LASTEXITCODE -ne 0) {
    gh release create $Tag --repo $Repo --title "v$Version" --notes "Provi Tracker v$Version"
} else {
    gh release edit $Tag --repo $Repo --title "v$Version" --notes "Provi Tracker v$Version"
}

gh release upload $Tag $installerPath --repo $Repo --clobber
if ($LASTEXITCODE -ne 0) { throw "Upload til GitHub Release fejlede." }

$currentAppcast = $null
$currentAppcastJson = & gh api "repos/$Repo/contents/appcast.xml?ref=$Branch" 2>$null
if ($LASTEXITCODE -eq 0 -and $currentAppcastJson) {
    $currentAppcast = $currentAppcastJson | ConvertFrom-Json
}

$content = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($appcastText))
$apiArgs = @(
    "api",
    "-X", "PUT",
    "repos/$Repo/contents/appcast.xml",
    "-f", "message=Update appcast for v$Version",
    "-f", "content=$content",
    "-f", "branch=$Branch"
)
if ($currentAppcast -and $currentAppcast.sha) {
    $apiArgs += @("-f", "sha=$($currentAppcast.sha)")
}

& gh @apiArgs | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Opdatering af appcast.xml paa GitHub fejlede." }

Write-Host "Uploadet $assetName til https://github.com/$Repo/releases/tag/$Tag"
Write-Host "Opdateret appcast.xml paa branch '$Branch'."
