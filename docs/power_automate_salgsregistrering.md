# Auto salgsregistrering online

Denne guide beskriver opsætningen uden lokal Excel, lokal Outlook og Azure/Entra-adgang for almindelige brugere.

Provi Tracker sender salgsregistreringen til Provi Tracker cloud. Cloud-funktionen sender derefter en mail med JSON-vedhæftning til den flow-mailboks, som Power Automate overvåger. Power Automate opdaterer Excel Online med Office Scriptet.

Almindelige brugere skal kun:

1. Åbne **Indstillinger** i Provi Tracker.
2. Kontrollere **Sælger initialer** og **Flow-mail**.
3. Slå **Send salgs-reg automatisk ved ny ordre** til.
4. Trykke **Gem salgsregistrering**.
5. Trykke **Send test** efter ændringer.

## 1. Klargør Excel-arket

1. Læg masterarket i OneDrive eller SharePoint.
2. Åbn filen i Excel Online.
3. Gå til **Automate**.
4. Opret et nyt Office Script.
5. Indsæt hele koden fra `scripts/excel_online_sales_registration.ts`.
6. Gem scriptet som `ProviTrackerSalesRegistration`.

Office Scriptet finder selv kolonner ud fra overskrifter som `Dato`, `Initialer`, `OSE-nr`, `CVR-nr`, `Firmanavn` og `Telefon`. Produkter matches mod produktnavne og aliases fra Provi Tracker. Scriptet kopierer formatet fra rækken ovenover og lægger den nye registrering på næste ledige række.

## 2. Opret Power Automate-flowet

1. Gå til Power Automate.
2. Opret et **Automated cloud flow**.
3. Vælg triggeren **Office 365 Outlook -> When a new email arrives (V3)**.
4. Vælg den flow-mailboks eller mappe, Provi Tracker cloud sender til.
5. Sæt emnefilter til `Salgs reg -`, hvis feltet er tilgængeligt.
6. Sørg for at flowet kun fortsætter, når mailen har en vedhæftning.
7. Tilføj handlingen **Get attachment (V2)** for JSON-vedhæftningen.
8. Tilføj **Compose**.
9. Brug dette udtryk i Compose:

```text
base64ToString(body('Get_attachment_(V2)')?['contentBytes'])
```

Navnet på `Get attachment (V2)` kan variere i Power Automate. Hvis handlingen får et andet internt navn, skal udtrykket bruge det navn Power Automate viser.

10. Tilføj **Excel Online (Business) -> Run script**.
11. Vælg masterarket.
12. Vælg scriptet `ProviTrackerSalesRegistration`.
13. Sæt parameteren `payloadJson` til outputtet fra Compose.
14. Gem flowet.

## 3. Indstil Provi Tracker

Gå til **Indstillinger -> Salgsregistrering**.

1. Udfyld **Sælger initialer**.
2. Udfyld **Flow-mail** med den mailboks flowet overvåger.
3. Slå **Send salgs-reg automatisk ved ny ordre** til.
4. Tryk **Gem salgsregistrering**.
5. Tryk **Send test**.

Når testen er sendt, kræver daglig brug ikke flere trin. Når en ordre oprettes i Provi Tracker, sendes salgsregistreringen online til mailflowet.

## 4. Administratoropsætning i Provi Tracker cloud

Cloud-funktionen `sales-registration-submit` er deployet i Supabase. Den lægger altid salgsregistreringer i tabellen `provi_sales_registration_queue`.

For at funktionen også skal sende mailen videre til Power Automate, skal følgende Supabase Function secrets sættes:

- `RESEND_API_KEY`
- `SALES_REGISTRATION_FROM`

`SALES_REGISTRATION_FROM` skal være en afsenderadresse, som mailudbyderen accepterer. Almindelige brugere skal ikke kende eller indtaste disse secrets.

## 5. Krav på brugerens computer

- Brugeren skal kunne logge ind i Provi Tracker cloud.
- Brugeren behøver ikke administratoradgang.
- Brugeren behøver ikke lokal Excel.
- Brugeren behøver ikke lokal Outlook.
- Brugeren behøver ikke Azure/Entra-adgang.
- Brugeren skal ikke indtaste Microsoft tenant, client ID eller OAuth scope.

## 6. Fejlfinding

- **Provi Tracker siger at mailer mangler:** Sæt `RESEND_API_KEY` og `SALES_REGISTRATION_FROM` i Supabase Function secrets.
- **Flowet starter ikke:** Tjek at triggeren lytter på den rigtige mailboks eller mappe, og at emnefilteret matcher `Salgs reg -`.
- **Office Scriptet fejler med manglende kolonner:** Tjek at masterarket har overskrifter for dato, initialer, ordre/OSE, CVR, firmanavn og telefon.
- **Et produkt rammer ikke den rigtige kolonne:** Tilføj produktnavnet eller et alias i Provi Tracker, så det matcher en kolonneoverskrift i masterarket.

Testregistreringer har `isTest: true`, så Office Scriptet svarer OK uden at oprette en rigtig salgsrække.
