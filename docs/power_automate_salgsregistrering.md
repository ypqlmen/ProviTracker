# Auto salgsregistrering via mailflow

Denne guide erstatter det gamle webhook-setup. Provi Tracker bruger ikke Power Automate-triggeren **When an HTTP request is received**, fordi den ofte kr?ver premium-adgang.

I stedet sender Provi Tracker en Microsoft Graph-mail til en flow-mailboks. Mailen indeholder en JSON-vedh?ftning med hele ordren. Power Automate l?ser vedh?ftningen og k?rer et Office Script, som opretter r?kken i Excel Online.

N?r ops?tningen er lavet ?n gang, skal almindelige brugere kun:

1. ?bne **Indstillinger** i Provi Tracker.
2. Kontrollere **S?lger initialer** og **Flow-mail**.
3. Sl? **Send salgs-reg automatisk ved ny ordre** til.
4. Trykke **Log ind** ved Microsoft-login, hvis appen beder om det.
5. Trykke **Send testmail** efter ?ndringer.

## 1. Klarg?r Excel-arket

1. L?g masterarket i OneDrive eller SharePoint.
2. ?bn filen i Excel Online.
3. G? til **Automate**.
4. Opret et nyt Office Script.
5. Inds?t hele koden fra `scripts/excel_online_sales_registration.ts`.
6. Gem scriptet som `ProviTrackerSalesRegistration`.

Office Scriptet finder selv kolonner ud fra overskrifter som `Dato`, `Initialer`, `OSE-nr`, `CVR-nr`, `Firmanavn` og `Telefon`. Produkter matches mod produktnavne og aliases fra Provi Tracker.

## 2. Opret Microsoft-login til Provi Tracker

Dette skal normalt kun g?res ?n gang af den person, der har adgang til Microsoft Entra.

1. G? til `https://portal.azure.com`.
2. ?bn **Microsoft Entra ID**.
3. G? til **App registrations**.
4. Tryk **New registration**.
5. Navn: `Provi Tracker Mailflow`.
6. Supported account types: **Accounts in this organizational directory only**.
7. Redirect URI:
   - Platform: **Mobile and desktop applications** eller **Public client/native**.
   - URI: `http://localhost`.
8. Gem appen.
9. Kopier **Application (client) ID**.
10. Kopier **Directory (tenant) ID**.

Tilf?j derefter mail-rettigheden:

1. ?bn app registration.
2. G? til **API permissions**.
3. Tryk **Add a permission**.
4. V?lg **Microsoft Graph**.
5. V?lg **Delegated permissions**.
6. Tilf?j `Mail.Send`.
7. Tryk **Add permissions**.
8. Tryk **Grant admin consent**, hvis jeres tenant kr?ver det.

OAuth scope i Provi Tracker skal v?re:

```text
https://graph.microsoft.com/Mail.Send
```

Provi Tracker tilf?jer selv `offline_access`, `openid` og `profile`, s? brugeren ikke skal logge ind igen hver gang.

## 3. Opret Power Automate-flowet

1. G? til Power Automate.
2. Opret et **Automated cloud flow**.
3. V?lg triggeren **Office 365 Outlook -> When a new email arrives (V3)**.
4. V?lg den flow-mailboks eller mappe, Provi Tracker skal sende til.
5. S?t emnefilter til `Salgs reg -`, hvis feltet er tilg?ngeligt.
6. S?rg for at flowet kun forts?tter, n?r mailen har en vedh?ftning.
7. Tilf?j handlingen **Get attachment (V2)** for JSON-vedh?ftningen.
8. Tilf?j **Compose**.
9. Brug dette udtryk i Compose:

```text
base64ToString(body('Get_attachment_(V2)')?['contentBytes'])
```

Navnet p? `Get attachment (V2)` kan variere i Power Automate. Hvis handlingen f?r et andet internt navn, skal udtrykket bruge det navn Power Automate viser.

10. Tilf?j **Excel Online (Business) -> Run script**.
11. V?lg masterarket.
12. V?lg scriptet `ProviTrackerSalesRegistration`.
13. S?t parameteren `payloadJson` til outputtet fra Compose.
14. Gem flowet.

## 4. Indstil Provi Tracker

G? til **Indstillinger -> Salgsregistrering**.

1. Udfyld **S?lger initialer**.
2. Udfyld **Flow-mail** med den mailboks flowet overv?ger.
3. Sl? **Send salgs-reg automatisk ved ny ordre** til.
4. Sl? **Brug Microsoft-login til mailflow** til.
5. Udfyld **Microsoft tenant** med Directory tenant ID eller `organizations`.
6. Udfyld **Microsoft client ID** med Application client ID.
7. Lad **OAuth scope** st? som `https://graph.microsoft.com/Mail.Send`.
8. Tryk **Gem salgsregistrering**.
9. Tryk **Log ind**.
10. Gennemf?r Microsoft-login og MFA i browseren.
11. Tryk **Send testmail**.

Hvis testmailen virker, kr?ver den daglige brug ikke flere trin. N?r en ordre oprettes i Provi Tracker, sender appen automatisk salgsregistreringen til flowet.

## 5. Fejlfinding

- **Testmailen kommer ikke frem:** Tjek Flow-mail i Provi Tracker og at Microsoft-login er aktivt.
- **Flowet starter ikke:** Tjek at triggeren lytter p? den rigtige mailboks eller mappe, og at emnefilteret matcher `Salgs reg -`.
- **Office Scriptet fejler med manglende kolonner:** Tjek at masterarket har overskrifter for dato, initialer, ordre/OSE, CVR, firmanavn og telefon.
- **Et produkt rammer ikke den rigtige kolonne:** Tilf?j produktnavnet eller et alias i Provi Tracker, s? det matcher en kolonneoverskrift i masterarket.
- **Microsoft-login fejler med consent:** Entra app registration mangler typisk `Mail.Send` eller admin consent.

Testmails har `isTest: true`, s? Office Scriptet svarer OK uden at oprette en rigtig salgsr?kke.
