# Auto salgsregistrering via mailflow

Denne guide beskriver opsætningen uden Azure/Entra-adgang for brugerne. Provi Tracker sender en almindelig Outlook-mail til en flow-mailboks. Mailen indeholder en JSON-vedhæftning med hele ordren, og Power Automate bruger vedhæftningen til at oprette rækken i Excel Online.

Brugerne skal kun have klassisk Outlook desktop installeret og sat op på deres Windows-profil. De skal ikke oprette app registrations, bruge Azure Portal eller indtaste Microsoft client ID i Provi Tracker.

Når opsætningen er lavet en gang, skal almindelige brugere kun:

1. Åbne **Indstillinger** i Provi Tracker.
2. Kontrollere **Sælger initialer** og **Flow-mail**.
3. Slå **Send salgs-reg automatisk ved ny ordre** til.
4. Trykke **Gem salgsregistrering**.
5. Trykke **Send testmail** efter ændringer.

## 1. Klargør Excel-arket

1. Læg masterarket i OneDrive eller SharePoint.
2. Åbn filen i Excel Online.
3. Gå til **Automate**.
4. Opret et nyt Office Script.
5. Indsæt hele koden fra `scripts/excel_online_sales_registration.ts`.
6. Gem scriptet som `ProviTrackerSalesRegistration`.

Office Scriptet finder selv kolonner ud fra overskrifter som `Dato`, `Initialer`, `OSE-nr`, `CVR-nr`, `Firmanavn` og `Telefon`. Produkter matches mod produktnavne og aliases fra Provi Tracker.

## 2. Opret Power Automate-flowet

1. Gå til Power Automate.
2. Opret et **Automated cloud flow**.
3. Vælg triggeren **Office 365 Outlook -> When a new email arrives (V3)**.
4. Vælg den flow-mailboks eller mappe, Provi Tracker skal sende til.
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
5. Tryk **Send testmail**.

Hvis testmailen virker, kræver den daglige brug ikke flere trin. Når en ordre oprettes i Provi Tracker, sender appen automatisk salgsregistreringen til flowet.

## 4. Krav på brugerens computer

- Klassisk Outlook desktop skal være installeret.
- Outlook skal være åbnet mindst en gang og have en fungerende mailprofil.
- Brugeren behøver ikke administratoradgang.
- Brugeren behøver ikke Azure/Entra-adgang.
- Brugeren skal ikke indtaste Microsoft tenant, client ID eller OAuth scope.

## 5. Fejlfinding

- **Testmailen kommer ikke frem:** Tjek Flow-mail i Provi Tracker og at Outlook kan sende almindelige mails fra computeren.
- **Provi Tracker siger at Outlook ikke blev fundet:** Installer eller åbn klassisk Outlook desktop på computeren. Den nye webbaserede Outlook-app understøtter ikke altid den lokale Outlook-automation.
- **Flowet starter ikke:** Tjek at triggeren lytter på den rigtige mailboks eller mappe, og at emnefilteret matcher `Salgs reg -`.
- **Office Scriptet fejler med manglende kolonner:** Tjek at masterarket har overskrifter for dato, initialer, ordre/OSE, CVR, firmanavn og telefon.
- **Et produkt rammer ikke den rigtige kolonne:** Tilføj produktnavnet eller et alias i Provi Tracker, så det matcher en kolonneoverskrift i masterarket.

Testmails har `isTest: true`, så Office Scriptet svarer OK uden at oprette en rigtig salgsrække.
