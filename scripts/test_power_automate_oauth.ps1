param(
    [Parameter(Mandatory = $true)]
    [string]$ClientId,

    [Parameter(Mandatory = $true)]
    [string]$Recipient,

    [string]$TenantId = "organizations",
    [string]$Scope = "https://graph.microsoft.com/Mail.Send"
)

$ErrorActionPreference = "Stop"

function Invoke-OAuthFormPost {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Uri,

        [Parameter(Mandatory = $true)]
        [hashtable]$Body
    )

    Invoke-RestMethod `
        -Method Post `
        -Uri $Uri `
        -ContentType "application/x-www-form-urlencoded" `
        -Body $Body
}

$authBase = "https://login.microsoftonline.com/$TenantId/oauth2/v2.0"
$deviceCode = Invoke-OAuthFormPost `
    -Uri "$authBase/devicecode" `
    -Body @{
        client_id = $ClientId
        scope = "$Scope offline_access openid profile"
    }

Write-Host ""
Write-Host $deviceCode.message
Write-Host ""

$token = $null
$deadline = (Get-Date).AddSeconds([int]$deviceCode.expires_in)

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds ([int]$deviceCode.interval)

    try {
        $token = Invoke-OAuthFormPost `
            -Uri "$authBase/token" `
            -Body @{
                client_id = $ClientId
                grant_type = "urn:ietf:params:oauth:grant-type:device_code"
                device_code = $deviceCode.device_code
            }
        break
    } catch {
        $response = $_.Exception.Response
        if (-not $response) {
            throw
        }

        $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
        $errorText = $reader.ReadToEnd()

        if ($errorText -match "authorization_pending") {
            continue
        }
        if ($errorText -match "slow_down") {
            Start-Sleep -Seconds 5
            continue
        }

        throw "Microsoft-login fejlede: $errorText"
    }
}

if (-not $token -or -not $token.access_token) {
    throw "Microsoft-login blev ikke gennemfoert inden tidsfristen."
}

$payloadObject = @{
    source = "Provi Tracker"
    type = "sales_registration_test"
    isTest = $true
    recipient = $Recipient
    createdAt = (Get-Date).ToString("o")
    date = (Get-Date).ToString("dd.MM.yy")
    sellerInitials = "TEST"
    orderNumber = "TEST-" + (Get-Date).ToString("yyyyMMddHHmmss")
    cvrNumber = "00000000"
    companyName = "Mailflow test"
    phoneNumber = "00000000"
    note = "Mailflow test fra Provi Tracker"
    mailSubject = "Salgs reg - Mailflow test"
    mailHtml = "<table><tr><td>Mailflow test fra Provi Tracker</td></tr></table>"
    items = @(
        @{
            key = "til_1000gb_data"
            productName = "Tillaeg 1000GB data"
            category = "Tillaeg"
            quantity = 1
            points = 0.5
            aliases = @("1000GB data", "1000 GB data")
        }
    )
}

$payloadJson = $payloadObject | ConvertTo-Json -Depth 10
$attachmentBytes = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payloadJson))

$mail = @{
    message = @{
        subject = $payloadObject.mailSubject
        body = @{
            contentType = "HTML"
            content = "<p>Salgsregistrering fra Provi Tracker.</p>$($payloadObject.mailHtml)<p>JSON-data er vedhaeftet til Power Automate-mailflowet.</p>"
        }
        toRecipients = @(
            @{
                emailAddress = @{
                    address = $Recipient
                }
            }
        )
        attachments = @(
            @{
                "@odata.type" = "#microsoft.graph.fileAttachment"
                name = "provi-sales-reg-$($payloadObject.orderNumber).json"
                contentType = "application/json"
                contentBytes = $attachmentBytes
            }
        )
    }
    saveToSentItems = $true
} | ConvertTo-Json -Depth 20

Invoke-RestMethod `
    -Method Post `
    -Uri "https://graph.microsoft.com/v1.0/me/sendMail" `
    -Headers @{ Authorization = "Bearer $($token.access_token)" } `
    -ContentType "application/json; charset=utf-8" `
    -Body $mail | Out-Null

Write-Host "Microsoft Graph mailflow test sendt til $Recipient."
