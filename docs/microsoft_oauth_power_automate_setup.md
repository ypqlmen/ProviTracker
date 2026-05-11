# Microsoft OAuth til Power Automate

Dette er den manglende tenant-side for Provi Trackers salgsregistrering.

Appen er allerede klar til Microsoft-login/MFA. Det der mangler udenfor appen er en Entra app registration og et Power Automate-flow, der accepterer OAuth-authenticated HTTP requests.

## Hvad der ikke kan automatiseres lokalt

Dette kan ikke klares fra repoet alene, fordi Microsoft kraever at en bruger med de rette tenant-rettigheder logger ind og opretter/godkender app registration og flow.

Der er ikke `az`, `m365` eller `pac` installeret paa denne maskine, og der er ingen aktiv Microsoft admin-session. Derfor kan jeg ikke oprette ressourcerne direkte i jeres tenant uden at du eller en admin logger ind.

## 1. Opret Entra app registration

Hvis Azure CLI er installeret, kan app registration-delen laves automatisk med:

```powershell
.\scripts\setup_microsoft_power_automate_oauth_app.ps1
```

Scriptet logger ind med Microsoft, opretter/genbruger app registration, tilfoejer `http://localhost` som public redirect og proever at tilfoeje Power Automate Service delegated permission.

Manuel opsaetning:

1. Gaa til `https://portal.azure.com`.
2. Aabn **Microsoft Entra ID -> App registrations -> New registration**.
3. Navn: `Provi Tracker Power Automate`.
4. Supported account types: **Accounts in this organizational directory only**.
5. Redirect URI:
   - Platform: **Mobile and desktop applications** eller **Public client/native**.
   - URI: `http://localhost`.
6. Gem appen.
7. Kopier:
   - **Application (client) ID**: skal ind i Provi Tracker som `Microsoft client ID`.
   - **Directory (tenant) ID**: skal ind i Provi Tracker som `Microsoft tenant`.

## 2. Tillaeg Power Automate delegated permissions

1. Aabn app registration.
2. Gaa til **API permissions -> Add a permission**.
3. Vaelg **APIs my organization uses**.
4. Soeg efter **Power Automate Service**.
5. Vaelg en delegated permission til Flow-service. Start med **Flows.Read.All** hvis den findes i tenantens liste.
6. Tryk **Add permissions**.
7. Tryk **Grant admin consent**, hvis jeres tenant kraever admin consent.

Vigtigt: Power Automate Service vises foerst i nogle tenants, naar mindst en bruger har vaeret logget ind paa `https://make.powerautomate.com`.

## 3. Saet Power Automate-triggeren til OAuth

I flowet med triggeren **When an HTTP request is received**:

1. Aabn triggerens settings/configuration.
2. Saet authentication til:
   - **Any user in my tenant**, eller
   - **Specific users in my tenant** hvis kun bestemte brugere maa kalde flowet.
3. Gem flowet.
4. Kopier den nye HTTP POST URL.

Den token Provi Tracker henter bruger audience:

```text
https://service.flow.microsoft.com/
```

Derfor er appens default OAuth scope:

```text
https://service.flow.microsoft.com//.default
```

Den dobbelte slash foer `.default` er bevidst, fordi resource-URI'en ender paa `/`.

## 4. Indstil Provi Tracker

I **Indstillinger -> Salgsregistrering**:

1. Saet webhook URL til HTTP POST URL'en fra flowet.
2. Saet modtager-mail.
3. Slaa **Send salgs-reg automatisk ved ny ordre** til.
4. Slaa **Brug Microsoft-login/MFA til Power Automate** til.
5. Microsoft tenant: Directory tenant ID eller `organizations`.
6. Microsoft client ID: Application client ID fra app registration.
7. OAuth scope: `https://service.flow.microsoft.com//.default`.
8. Tryk **Gem salgsregistrering**.
9. Tryk **Log ind med Microsoft**.
10. Gennemfoer MFA i browseren.
11. Tryk **Test webflow**.

## 5. Test uden appen

Naar tenant/app/flow er sat op, kan dette script teste OAuth og flow direkte:

```powershell
.\scripts\test_power_automate_oauth.ps1 `
  -TenantId "<tenant-id>" `
  -ClientId "<application-client-id>" `
  -WebhookUrl "<power-automate-http-post-url>" `
  -Recipient "modtager@firma.dk"
```

Scriptet bruger device-code login, sender et test-payload med `isTest=true` og forventer et `200 OK` svar fra flowet.

## Hvis OAuth-testen fejler

- `401` eller `The OAuth authorization scheme is required`: Flow-triggeren forventer OAuth, men kaldet mangler et gyldigt bearer-token.
- `One or more claims either missing...`: Tokenets audience, tenant eller allowed users matcher ikke flow-triggeren.
- `AADSTS65001` eller consent-fejl: App registration mangler admin consent.
- `AADSTS650053` eller scope-fejl: App registration mangler Power Automate Service delegated permission, eller scope er forkert.

Naar testscriptet virker, bruger Provi Tracker samme type token og samme webhook URL.
