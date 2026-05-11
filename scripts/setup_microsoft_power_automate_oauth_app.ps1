param(
    [string]$DisplayName = "Provi Tracker Power Automate",
    [string]$RedirectUri = "http://localhost",
    [string]$FlowServiceAppId = "7df0a125-d3be-4c96-aa54-591f83ff541c"
)

$ErrorActionPreference = "Stop"

$azPaths = @(
    "C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin",
    "C:\Program Files (x86)\Microsoft SDKs\Azure\CLI2\wbin"
)
foreach ($path in $azPaths) {
    if (Test-Path $path) {
        $env:Path = "$path;$env:Path"
    }
}

if (!(Get-Command az -ErrorAction SilentlyContinue)) {
    throw "Azure CLI blev ikke fundet. Installer Microsoft.AzureCLI foerst."
}

Write-Host "Tjekker Microsoft-login..."
$accountJson = az account show 2>$null
if ($LASTEXITCODE -ne 0 -or -not $accountJson) {
    Write-Host ""
    Write-Host "Du skal logge ind med Microsoft nu. Brug kontoen der maa oprette app registrations."
    Write-Host "Hvis du faar en kode, saa aabn linket, tast koden og godkend MFA."
    Write-Host ""
    az login --use-device-code | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Microsoft-login fejlede."
    }
    $accountJson = az account show
}

$account = $accountJson | ConvertFrom-Json
$tenantId = $account.tenantId

Write-Host "Bruger tenant: $tenantId"
Write-Host "Finder eller opretter app registration: $DisplayName"

$existingApps = az ad app list --display-name $DisplayName | ConvertFrom-Json
$app = $existingApps | Select-Object -First 1

if (-not $app) {
    $app = az ad app create `
        --display-name $DisplayName `
        --sign-in-audience AzureADMyOrg `
        --is-fallback-public-client true `
        --public-client-redirect-uris $RedirectUri `
        | ConvertFrom-Json
} else {
    az ad app update `
        --id $app.appId `
        --is-fallback-public-client true `
        --public-client-redirect-uris $RedirectUri `
        | Out-Null
}

$clientId = $app.appId
Write-Host "App client ID: $clientId"

Write-Host "Finder Power Automate Service permissions..."
$flowSp = az ad sp show --id $FlowServiceAppId 2>$null | ConvertFrom-Json
if (-not $flowSp) {
    throw "Kunne ikke finde Power Automate Service i tenant. Log ind paa https://make.powerautomate.com mindst en gang og proev igen."
}

$preferredScopes = @(
    "user_impersonation",
    "Flows.Read.All",
    "Flows.Manage.All",
    "Approvals.Read.All",
    "Activity.Read.All"
)

$scope = $null
foreach ($name in $preferredScopes) {
    $scope = $flowSp.oauth2PermissionScopes | Where-Object { $_.value -eq $name -and $_.isEnabled } | Select-Object -First 1
    if ($scope) { break }
}

if (-not $scope) {
    $available = ($flowSp.oauth2PermissionScopes | Where-Object { $_.isEnabled } | Select-Object -ExpandProperty value) -join ", "
    throw "Kunne ikke finde en brugbar delegated permission paa Power Automate Service. Tilgaengelige scopes: $available"
}

Write-Host "Tilfoejer delegated permission: $($scope.value)"
az ad app permission add `
    --id $clientId `
    --api $FlowServiceAppId `
    --api-permissions "$($scope.id)=Scope" `
    | Out-Null

Write-Host "Proever at give admin consent..."
try {
    az ad app permission admin-consent --id $clientId | Out-Null
    $consentStatus = "Admin consent er givet."
} catch {
    $consentStatus = "Admin consent blev ikke givet automatisk. En admin skal trykke Grant admin consent i Azure Portal."
}

$result = [ordered]@{
    tenantId = $tenantId
    clientId = $clientId
    appName = $DisplayName
    redirectUri = $RedirectUri
    oauthScope = "https://service.flow.microsoft.com//.default"
    delegatedPermission = $scope.value
    consentStatus = $consentStatus
}

Write-Host ""
Write-Host "Faerdig. Indsaet disse vaerdier i Provi Tracker:"
$result | Format-List
