# Salgsregistrering via mailflow

Programmet bruger ikke laengere Power Automate-triggeren **When an HTTP request is received**. Den trigger er ofte premium/lukket for almindelige brugere.

I stedet sender Provi Tracker en Microsoft Graph-mail til en valgt flow-mailboks. Mailen indeholder:

- HTML-tabellen til menneskelig laesning.
- En JSON-vedhaeftning med hele salgsregistreringen.

Power Automate kan derfor starte paa den almindelige Outlook-trigger **When a new email arrives**, hente JSON-vedhaeftningen og koere Office Scriptet mod Excel Online.

## 1. Klargoer masterarket

1. Laeg masterarket i OneDrive eller SharePoint.
2. Aabn filen i Excel Online.
3. Gaa til **Automate** og opret et nyt Office Script.
4. Indsaet koden fra `scripts/excel_online_sales_registration.ts`.
5. Gem scriptet som `ProviTrackerSalesRegistration`.

Scriptet finder kolonnerne ud fra overskrifter som `Dato`, `Initialer`, `OSE-nr`, `Cvr nr.`, `Firmanavn` og `Telefon`. Produkter matches mod produktnavne og aliases fra appen.

## 2. Opret Power Automate-flow

1. Opret et automated cloud flow.
2. Vaelg triggeren **Outlook -> When a new email arrives (V3)**.
3. Saet mailboksen/mappe til den flow-mail, Provi Tracker sender til.
4. Filtrer gerne paa emne, fx `Salgs reg -`.
5. Tilfoej **Get attachment (V2)** eller tilsvarende handling for JSON-vedhaeftningen.
6. Tilfoej **Compose** og lav attachment-content om til tekst. Brug typisk et udtryk i stil med:

```text
base64ToString(body('Get_attachment_(V2)')?['contentBytes'])
```

Navnet paa feltet kan variere lidt i Power Automate. Det vigtige er, at Office Scriptet modtager JSON-teksten fra vedhaeftningen.

7. Tilfoej **Excel Online (Business) -> Run script**.
8. Vaelg masterarket og scriptet `ProviTrackerSalesRegistration`.
9. Saet script-parameteren `payloadJson` til outputtet fra Compose.

Testmails har `isTest: true`, saa Office Scriptet returnerer OK uden at oprette en rigtig salgsraekke.

## 3. Indstil Provi Tracker

I **Indstillinger -> Salgsregistrering**:

1. Udfyld **Flow-mail** med den mailboks flowet overvager.
2. Udfyld saelgerinitialer.
3. Slaa **Send salgs-reg automatisk ved ny ordre** til.
4. Udfyld Microsoft tenant og client ID.
5. Lad OAuth scope staa som `https://graph.microsoft.com/Mail.Send`, medmindre tenant setup kraever andet.
6. Tryk **Gem salgsregistrering**.
7. Tryk **Log ind** og gennemfoer Microsoft-login/MFA.
8. Tryk **Send testmail**.

## 4. Intramanager-salgsregistrering

Den sikre vej er stadig at lave en separat discovery-runde paa Intramanager, foer appen nogensinde skriver direkte dertil:

1. Log ind med Playwright-workeren.
2. Find dagens relevante rapportlinje i `reports/history/`.
3. Aabn `data-popover-load="https://5r.intramanager.com/reports/popover-products/{reportId}/"`.
4. Registrer de netvaerkskald popoveren laver, naar et salg gemmes manuelt.
5. Bekraeft endpoint, CSRF/FMK-token, feltnavne og produkt-ID'er i et testmiljoe eller paa en aftalt testregistrering.
6. Foerst derefter tilfoejes en worker-action som fx `--action sale-register`.

Der er ikke hardcodet et live POST-kald til Intramanager, fordi det kan oprette rigtige salg.
