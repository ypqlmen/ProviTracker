# Salgsregistrering via Power Automate

Programmet sender salgs-reg som JSON til en webhook. Flowet skal derefter opdatere Excel Online og sende Outlook-mailen.

## 1. Klargoer masterarket

1. Laeg masterarket i OneDrive eller SharePoint.
2. Aabn filen i Excel Online.
3. Gaa til **Automate** og opret et nyt Office Script.
4. Indsaet koden fra `scripts/excel_online_sales_registration.ts`.
5. Gem scriptet som `ProviTrackerSalesRegistration`.

Scriptet finder kolonnerne ud fra overskrifter som `Dato`, `Initialer`, `OSE-nr`, `Cvr nr.`, `Firmanavn` og `Telefon`. Produkter matches mod produktnavne og aliases fra appen.

## 2. Opret Power Automate-flow

1. Opret et cloud flow med triggeren **When an HTTP request is received**.
2. Brug denne simple JSON-schema:

```json
{
  "type": "object",
  "properties": {
    "type": { "type": "string" },
    "isTest": { "type": "boolean" },
    "recipient": { "type": "string" },
    "mailSubject": { "type": "string" },
    "mailHtml": { "type": "string" }
  },
  "additionalProperties": true
}
```

3. Tilfoej handlingen **Excel Online (Business) -> Run script**.
4. Vaelg masterarket og scriptet `ProviTrackerSalesRegistration`.
5. S?t script-parameteren `payloadJson` til hele trigger body som string. Brug typisk udtrykket:

```text
string(triggerBody())
```

6. Tilfoej handlingen **Outlook -> Send an email (V2)**.
7. S?t **To** til `recipient`, **Subject** til `mailSubject`, og **Body** til `mailHtml`.
8. Slaa HTML-body til, hvis flow-designet viser den mulighed.
9. Tilfoej handlingen **Response** med status `200` og body:

```json
{
  "message": "Salgs-reg sendt."
}
```

Hvis flowet skal beskyttes med Microsoft OAuth/MFA, saa foelg ogsaa `docs/microsoft_oauth_power_automate_setup.md`.

## 3. Test fra appen

1. Kopier HTTP POST URL'en fra flow-triggeren.
2. Saet URL'en ind i Provi Tracker under **Indstillinger -> Salgsregistrering**.
3. Udfyld modtager-mail og standard-initialer.
4. Tryk **Test webflow**.

Testkaldet sender `isTest: true`. Office Script returnerer derfor OK uden at oprette en rigtig salgsr?kke.

## 4. Intramanager-salgsregistrering

Den sikre vej er at lave en separat discovery-runde paa Intramanager:

1. Log ind med Playwright-workeren.
2. Find dagens relevante rapportlinje i `reports/history/`.
3. Aabn `data-popover-load="https://5r.intramanager.com/reports/popover-products/{reportId}/"`.
4. Registrer de netvaerkskald popoveren laver, naar et salg gemmes manuelt.
5. Bekraeft endpoint, CSRF/FMK-token, feltnavne og produkt-ID'er i et testmiljoe eller paa en aftalt testregistrering.
6. Foerst derefter tilfoejes en worker-action som fx `--action sale-register`.

Jeg har ikke hardcodet et live POST-kald, fordi det kan oprette rigtige salg i Intramanager. Debug-HTML viser kun historik og popover-loaderen, ikke et dokumenteret eller sikkert write-endpoint.
