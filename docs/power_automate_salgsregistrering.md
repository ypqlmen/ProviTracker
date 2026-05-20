# Salgsregistrering via mailflow

Programmet bruger ikke l?ngere Power Automate-triggeren **When an HTTP request is received**. Den trigger er ofte premium/lukket for almindelige brugere.

I stedet sender Provi Tracker en Microsoft Graph-mail til en valgt flow-mailboks. Mailen indeholder:

- HTML-tabellen til menneskelig l?sning.
- En JSON-vedh?ftning med hele salgsregistreringen.

Power Automate kan derfor starte p? den almindelige Outlook-trigger **When a new email arrives**, hente JSON-vedh?ftningen og k?re Office Scriptet mod Excel Online.

## 1. Klarg?r masterarket

1. L?g masterarket i OneDrive eller SharePoint.
2. ?bn filen i Excel Online.
3. G? til **Automate** og opret et nyt Office Script.
4. Inds?t koden fra `scripts/excel_online_sales_registration.ts`.
5. Gem scriptet som `ProviTrackerSalesRegistration`.

Scriptet finder kolonnerne ud fra overskrifter som `Dato`, `Initialer`, `OSE-nr`, `Cvr nr.`, `Firmanavn` og `Telefon`. Produkter matches mod produktnavne og aliases fra appen.

## 2. Opret Power Automate-flow

1. Opret et automated cloud flow.
2. V?lg triggeren **Outlook -> When a new email arrives (V3)**.
3. S?t mailboksen/mappe til den flow-mail, Provi Tracker sender til.
4. Filtrer gerne p? emne, fx `Salgs reg -`.
5. Tilf?j **Get attachment (V2)** eller tilsvarende handling for JSON-vedh?ftningen.
6. Tilf?j **Compose** og lav attachment-content om til tekst. Brug typisk et udtryk i stil med:

```text
base64ToString(body('Get_attachment_(V2)')?['contentBytes'])
```

Navnet p? feltet kan variere lidt i Power Automate. Det vigtige er, at Office Scriptet modtager JSON-teksten fra vedh?ftningen.

7. Tilf?j **Excel Online (Business) -> Run script**.
8. V?lg masterarket og scriptet `ProviTrackerSalesRegistration`.
9. S?t script-parameteren `payloadJson` til outputtet fra Compose.

Testmails har `isTest: true`, s? Office Scriptet returnerer OK uden at oprette en rigtig salgsr?kke.

## 3. Indstil Provi Tracker

I **Indstillinger -> Salgsregistrering**:

1. Udfyld **Flow-mail** med den mailboks flowet overvager.
2. Udfyld s?lgerinitialer.
3. Sl? **Send salgs-reg automatisk ved ny ordre** til.
4. Udfyld Microsoft tenant og client ID.
5. Lad OAuth scope st? som `https://graph.microsoft.com/Mail.Send`, medmindre tenant setup kr?ver andet.
6. Tryk **Gem salgsregistrering**.
7. Tryk **Log ind** og gennemf?r Microsoft-login/MFA.
8. Tryk **Send testmail**.

## 4. Intramanager-salgsregistrering

Den sikre vej er stadig at lave en separat discovery-runde p? Intramanager, f?r appen nogensinde skriver direkte dertil:

1. Log ind med Playwright-workeren.
2. Find dagens relevante rapportlinje i `reports/history/`.
3. ?bn `data-popover-load="https://5r.intramanager.com/reports/popover-products/{reportId}/"`.
4. Registrer de netv?rkskald popoveren laver, n?r et salg gemmes manuelt.
5. Bekr?ft endpoint, CSRF/FMK-token, feltnavne og produkt-ID'er i et testmilj? eller p? en aftalt testregistrering.
6. F?rst derefter tilf?jes en worker-action som fx `--action sale-register`.

Der er ikke hardcodet et live POST-kald til Intramanager, fordi det kan oprette rigtige salg.
