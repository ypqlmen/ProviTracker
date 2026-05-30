# Provi Tracker

Qt Widgets desktop-app i C++ med Supabase cloud-login, per-user installation og GitHub auto-update.

## Funktioner

- Brugernavn/kodeord-login via Supabase
- Automatisk migration af gamle lokale data ved f?rste cloud-login
- ?n aktiv s?lger pr. bruger, hvor s?lgernavnet f?lger brugernavnet
- Ordrer med flere produkter i samme ordre
- Redigering og sletning af ordrer
- Dashboard med KPI'er, m?l og provisionsstatus
- Rapporter i appen og PDF-eksport
- Backup/import
- Automatisk m?nedsluk for tidligere m?neder med snapshots + PDF
- Provisionsmotor med 5-trinsl?sning for SIMO og 10-trinsl?sning for VOICE
- Intramanager-timer caches pr. rapportperiode
- Mailbaseret salgsregistrering til Excel Online/Outlook via Microsoft Graph og Power Automate

## Salgsregistrering via mailflow

Programmet bruger ikke lokal Excel eller Outlook. N?r salgsregistrering er sl?et til i Indstillinger, sender appen en Microsoft Graph-mail til den flow-mailboks, der er gemt i indstillingerne.

Programmet ?bner Microsoft-login i browseren, s? MFA h?ndteres af Microsoft, og gemmer kun refresh-token i Windows Credential Manager. Programmet gemmer ikke Outlook-adgangskoder.

Payloaden indeholder bl.a.:

- `date`, `sellerInitials`, `orderNumber`, `cvrNumber`, `companyName`, `phoneNumber`
- `recipient`, `mailSubject` og `mailHtml`
- `items` med produktnavn, antal, point og aliases til matching mod masterarket

Mailen indeholder en HTML-tabel og en JSON-vedh?ftning. Et Power Automate-flow kan starte med **When a new email arrives**, l?se JSON-vedh?ftningen og bruge de faste felter til at oprette r?kken i Excel Online.

Se `docs/power_automate_salgsregistrering.md` og `scripts/excel_online_sales_registration.ts` for den konkrete flow-ops?tning.

## Cloud-login og data

Version 1.4.0 og nyere bruger Supabase-projektet `provi tracker` til login og datalagring. Appen bruger brugernavn og kodeord, ikke email-login.

Cloud-data pr. bruger:

- `settings`
- `orders`
- `products`
- ?n `salesperson`, normaliseret til brugerens brugernavn
- `secrets` med Intramanager-adgangskode og Microsoft refresh-token krypteret client-side

Ved f?rste login med en tom cloud-profil uploader appen automatisk de gamle lokale JSON-data. Derefter er Supabase source of truth for indstillinger, ordrer, produkter, s?lgeren og krypterede adgangsoplysninger. Secrets krypteres med AES-256-GCM f?r upload med en n?gle afledt af brugerens Provi-login. Den lokale computer gemmer kun en DPAPI-krypteret cloud-session og en DPAPI-krypteret kopi af krypteringsn?glen, s? automatisk login kan ?bne de krypterede secrets igen.

Databaseschema og RPC-funktioner ligger i `docs/supabase_cloud_schema.sql`.

## Provisionslogik

- Dagsprovision = point * 50
- M?nedsprovision = alle m?nedens point afregnes til h?jeste n?ede sats
- SIMO provision = hvis SIMO < 5 => 0, ellers `floor(SIMO / 5) * 5 * 200`
- VOICE provision = hvis VOICE < 20 => 0, ellers `floor(VOICE / 10) * 10 * 200`

Eksempler:

- 4 SIMO = 0
- 5 SIMO = 1000
- 9 SIMO = 1000
- 10 SIMO = 2000
- 19 VOICE = 0
- 20 VOICE = 4000
- 24 VOICE = 4000
- 29 VOICE = 4000
- 30 VOICE = 6000

## Kodestruktur

- `main_v32.cpp` indeholder app-start, cloud-login, hovedvindue, UI-opbygning og brugerflows.
- `storage_paths.h` indeholder AppData-stier og migration fra gamle installationer.
- `domain.h` indeholder modeller, settings og JSON-serialisering.
- `credentials.h` indeholder krypteret Intramanager-login og Microsoft Credential Manager-hj?lpere.
- `repository.h` indeholder lokal migrationsl?sning, cloud-payloads og produktkatalog-migration.
- `commission.h` indeholder provisions-, l?nperiode- og datoberegninger.
- `report_service.h` indeholder HTML/PDF-rapportgenerering og m?nedsluk.

## Byg med CMake + Qt 6

```powershell
cmake -S . -B build/Desktop_Qt_6_9_3_MinGW_64_bit-Release -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64 -G "MinGW Makefiles"
cmake --build build/Desktop_Qt_6_9_3_MinGW_64_bit-Release --config Release
```

## Krav

- Qt 6 Widgets
- Qt 6 Network
- C++17
- Inno Setup 6 til installer-builds

## Datafiler

Version 1.4.0 bruger Supabase som prim?r lagring, s? almindelige brugere ikke beh?ver administratoradgang og kan hente deres ops?tning ved login. Gamle versioners lokale JSON-filer bruges stadig som migrationskilde:

- `salespeople.json`
- `products.json`
- `orders.json`
- `settings.json`
- `intramanager_login.json` med brugerbundet DPAPI-krypteret Intramanager-adgangskode
- `cloud_session.json` med brugerbundet DPAPI-krypteret Supabase-session
- `snapshots/`
- `reports/`

## Installer og auto-update

Installer-scriptet ligger i `installer/ProviTracker.iss` og installerer til:

`%LOCALAPPDATA%\Programs\Provi Tracker`

Byg installer:

```powershell
.\scripts\build_installer.ps1
```

Appen l?ser appcast fra:

`https://raw.githubusercontent.com/ypqlmen/ProviTracker/main/appcast.xml`

GitHub Release-tagget til auto-update er `autoupdate`. Auto-update i appen henter zip-assetet, pakker det ud i brugerens tempmappe, starter installeren og rydder op bagefter.

Aktuelle assets for version 1.5.2:

- `ProviTrackerUpdate-1.5.2.zip`
- `ProviBeregnerSetup-1.5.2.exe`

Bem?rk: den oprindelige 1.1-build indeholdt WinSparkle DLL'en, men ikke en appcast-URL i selve programmet eller installeren. Brugere p? 1.1 skal derfor installere en nyere version manuelt ?n gang; derefter kan auto-update hente fremtidige versioner.

Version 1.3.24 og nyere bruger appens egen zip-baserede updater i stedet for WinSparkle.

Installeren fra 1.3.9 og nyere migrerer 1.1-data og afinstallerer den gamle 1.1-app, hvis den ligger i en separat mappe.

Udgiv update n?r GitHub CLI er installeret og logget ind:

```powershell
.\scripts\publish_autoupdate.ps1
```

Scriptet uploader update-zippen og installeren til release-tagget og opdaterer `appcast.xml` p? `main`.
