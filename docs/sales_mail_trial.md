# Salgs reg på mail — 2.0.1 trial / udvidelse 0.1.22

## Opsætning

1. Installer trial-versionen og åbn Indstillinger → Masterark.
2. Behold udvidelsen installeret i den samme mappe. Provi Tracker opdaterer den eksisterende udvidelse; kontrollér 0.1.22 i Chrome.
3. Vælg Forbind Outlook i Chrome. Godkend udvidelsens valgfrie Outlook-adgang, og log ind med sælgerens egen arbejdsmail, hvis Microsoft beder om det. De tilladte Outlook-adresser er outlook.office.com, outlook.cloud.microsoft og outlook.office365.com.
4. Indtast én modtagermailadresse i Salgs reg på mail. Slå Send automatisk salgs reg efter registrering til, og vælg Gem mailindstillinger. Automatisk masterark-registrering skal være aktiveret.
5. Opret en ny ordre. Mailen sendes først efter Excels verificerede registrering. Følg den separate mailstatus på ordrekortet.

Chrome skal køre med netforbindelse og gyldigt Microsoft-login. Outlook åbnes i udvidelsens egen baggrundsfane. Den beskyttes mod automatisk aflastning/frysning, men løsningen omgår ikke MFA, udløbet login, lukket Chrome eller slukket computer. Outlooks kontotilknytning og mailkøen gemmes på tværs af sessioner; login skal stadig være gyldigt.

## Indhold og status

Emnet er altid `salgs reg`. HTML-tabellen indeholder masterarkets to overskriftsrækker og salgets verificerede historikrække med farver, skrifter, kanter, bredder og cellejustering. Den kopierer ikke en senere ordres øverste salgsrække under genforsøg. Vedhæftninger og kundedata fra eksempelmailen medfølger ikke.

Kun nye ordrer oprettet med mailfunktionen aktiveret får en mailopgave. Mailudsnittet bevares i køen, indtil afsendelsen er bekræftet; derefter fjernes den ekstra HTML-/tekstkopi fra ordredata. Modtager, afsender og status bevares på ordren; en senere ændring i indstillingerne omdirigerer ikke en eksisterende mail. Ældre ordrer sendes ikke automatisk.

Masterark-registrering og mail har hver sin status. Et mailproblem ændrer ikke en verificeret Excel-registrering. Knappen Mail på Ordrer viser status eller prøver et sikkert genforsøg. Hvis afsendelsen blev afbrudt efter Send, kræver programmet kontrol af Sendt post før et nyt forsøg. En allerede bekræftet mail sendes ikke automatisk igen.

Mail sendt betyder, at en ny kopi er fundet i den tilknyttede sælgers Sendt post med det rigtige emne, hele den forventede tabel og den eksplicit kontrollerede modtagermail. Kopien må ikke være blandt de beskeder, der fandtes i Sendt post før afsendelsen. Det er ikke en garanti for modtagerens levering; eksempelvis kan en senere fejlmail stadig forekomme.

## Tekniske afgrænsninger

Al mailbetjening foregår gennem Outlooks normale browserkontroller. Der anvendes ingen cookies, adgangskoder, Graph-tokens eller private Outlook-API'er. Kontrolleret afsender, én modtager, fast emne og hele tabellens celler verificeres før Send. En navnebaseret modtagerbrik kontrolleres via kontaktkortets entydige mailadresse. Andre åbne kladder overskrives ikke.

En lokal afsendelsesjournal gemmer indholdets hash før Send og kvittering bagefter. Appen gemmer tilsvarende afsendelsesstatus før udsendelsen. Et ukendt resultat giver ingen automatisk genafsendelse. Brug fortsat én Provi Tracker-instans pr. sælger/masterark.

Denne branch er en trial. Den ændrer ikke den offentlige 2.0.0-opdateringskanal. Syntetiske browserprøver erstatter ikke afprøvning i brugerens autentificerede Windows/Chrome-installation.
