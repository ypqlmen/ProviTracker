# Microsoft OAuth til salgsregistrering

Provi Tracker sender salgsregistrering via Microsoft Graph `sendMail`. Det kraever ikke Power Automate HTTP-triggeren.

Appen skal bruge en Entra app registration, saa brugeren kan logge ind med Microsoft/MFA og give appen lov til at sende mail paa brugerens vegne.

## 1. Opret Entra app registration

Hvis Azure CLI er installeret, kan app registration-delen laves automatisk med:

```powershell
.\scripts\setup_microsoft_power_automate_oauth_app.ps1
```

Manuel opsaetning:

1. Gaa til `https://portal.azure.com`.
2. Aabn **Microsoft Entra ID -> App registrations -> New registration**.
3. Navn: `Provi Tracker Mailflow`.
4. Supported account types: **Accounts in this organizational directory only**.
5. Redirect URI:
   - Platform: **Mobile and desktop applications** eller **Public client/native**.
   - URI: `http://localhost`.
6. Gem appen.
7. Kopier:
   - **Application (client) ID** til Provi Tracker.
   - **Directory (tenant) ID** til Provi Tracker.

## 2. Tilfoej Microsoft Graph delegated permissions

1. Aabn app registration.
2. Gaa til **API permissions -> Add a permission**.
3. Vaelg **Microsoft Graph**.
4. Vaelg **Delegated permissions**.
5. Tilfoej:
   - `Mail.Send`
6. Tryk **Add permissions**.
7. Tryk **Grant admin consent**, hvis jeres tenant kraever admin consent.

Appens default OAuth scope er:

```text
https://graph.microsoft.com/Mail.Send
```

Appen tilfoejer selv `offline_access`, `openid` og `profile` ved login, saa login kan gemmes og fornys uden at brugeren skal logge ind hver gang.

## 3. Indstil Provi Tracker

I **Indstillinger -> Salgsregistrering**:

1. Udfyld **Flow-mail** med mailboksen som Power Automate overvager.
2. Slaa **Send salgs-reg automatisk ved ny ordre** til.
3. Microsoft tenant: Directory tenant ID eller `organizations`.
4. Microsoft client ID: Application client ID fra app registration.
5. OAuth scope: `https://graph.microsoft.com/Mail.Send`.
6. Tryk **Gem salgsregistrering**.
7. Tryk **Log ind**.
8. Gennemfoer Microsoft-login/MFA i browseren.
9. Tryk **Send testmail**.

## 4. Test uden appen

```powershell
.\scripts\test_power_automate_oauth.ps1 `
  -TenantId "<tenant-id>" `
  -ClientId "<application-client-id>" `
  -Recipient "flowmail@firma.dk"
```

Scriptet bruger device-code login og sender en testmail via Microsoft Graph.

## Hvis OAuth-testen fejler

- `AADSTS65001` eller consent-fejl: app registration mangler bruger- eller admin-consent.
- `Insufficient privileges`: app registration mangler `Mail.Send`.
- `invalid_scope`: OAuth scope i appen er stadig sat til den gamle Power Automate scope. Brug `https://graph.microsoft.com/Mail.Send`.
- Mailen sendes, men flowet starter ikke: tjek at flowet lytter paa den rigtige mailboks/mappe, og at emnefilteret matcher `Salgs reg -`.
