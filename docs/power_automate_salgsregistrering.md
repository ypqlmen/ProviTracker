# Salgsregistrering fra Ordrer til Excel Online

Denne udviklingsversion erstatter det tidligere mail/Power Automate-flow i brugerfladen. Ingen mails sendes, når en ordre oprettes.

## Opsætning pr. bruger

1. Gem linket til brugerens eget SharePoint-masterark i **Indstillinger → Salgsregistrering → Masterark**.
2. Vælg **Forbind masterark**. Log ind med arbejdskontoen i browservinduet. Programmet bruger en særskilt browserprofil pr. Provi Tracker-konto.
3. Hjælperen opretter `ProviTrackerSalesRegistration`, hvis det ikke allerede vises i scriptgalleriet. Et eksisterende script overskrives ikke. Kontrollen validerer kolonnerne uden at tilføje et testsalg.
4. Slå **Registrer nye salg automatisk i masterarket** til og gem.

Der kræves ikke lokal Excel, Outlook eller Windows-administratorrettigheder. Microsoft-kontoen skal kunne åbne/redigere masterarket og køre Office Scripts. Adgang på én konto dokumenterer ikke adgang på alle kollegers konti.

## Daglig brug

En ny ordre får en salgsreg med de valgte produkter og kundeoplysninger. Den gemmes med ordren i den eksisterende datalagring, før automatisk overførsel begynder. Linket til masterarket gemmes på den enkelte registrering, så senere ændringer af indstillinger ikke flytter gamle registreringer til et andet ark.

**Salgsreg**-kolonnen på Ordrer viser:

- **Afventer masterark**: registreringen afventer overførsel eller en afbrudt overførsel skal kontrolleres.
- **Registreret i masterark**: Excel har returneret en bekræftelse på denne ordre og dette forsøg.
- **Kræver handling**: fx udløbet login, manglende kolonne eller ukendt produkt. Fejlen vises som hjælpetekst på statusfeltet. Vælg ordren og **Registrer / prøv igen**.
- **Ændret – kontrollér masterark**: et tidligere forsøgt/registreret salg er rettet. Programmet overskriver ikke automatisk den gamle registrering i Excel.

Ved udløbet Microsoft-login vælges **Forbind masterark** igen. Samme OSE-nummer og samme indhold returnerer `already_registered`; forskelligt indhold giver en konflikt. Programmet sender én registrering ad gangen på samme computer. Kør ikke samtidige overførsler fra flere computere til samme workbook: Office Scriptet giver ikke en database-transaktion på tværs af samtidige Excel-sessioner.

## Masterarkets struktur

Fanen skal hedde **Ark1**. Scriptet finder overskriftsrækken ud fra Dato og Initialer, kontrollerer alle krævede kolonner og bruger en fast produktkobling til den vedlagte skabelon. Ukendte produkter afvises før skrivning. Add-ons samles i Add-on-kolonnen, med specifikation i Bemærkninger.

Nye salg tilføjes efter eksisterende værdier. Historikken og den øverste forhåndsvisningsrække bevares. OSE, CVR og telefon behandles som tekst. Sletning i Provi Tracker sletter ikke rækker i Excel.

## Verifikation og resterende afprøvning

- Office Script v2 blev kørt mod brugerens eksisterende masterark med `isTest: true`; alle produktkolonner blev godkendt, uden en ny salgsrække.
- Lokale tests dækker produktkobling, sammentælling, ukendte produkter, dubletter, konflikter, append og afvisning af gamle/forkerte kvitteringer.
- Den pakkede Windows-hjælper og automatisk skrivning fra den færdige app skal afprøves på Windows, før funktionen beskrives som færdigafprøvet eller udgives til alle.
