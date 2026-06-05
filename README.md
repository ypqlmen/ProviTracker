# Provi Tracker

Qt Widgets desktop-app i C++ med Supabase cloud-login, per-user installation og GitHub auto-update.

## Funktioner

- Brugernavn/kodeord-login via Supabase
- Automatisk migration af gamle lokale data ved første cloud-login
- En aktiv sælger pr. bruger, hvor sælgernavnet følger brugernavnet
- Ordrer med flere produkter i samme ordre
- Redigering og sletning af ordrer
- Dashboard med KPI'er, mål og provisionsstatus
- Rapporter i appen og PDF-eksport
- Backup/import
- Automatisk månedsluk for tidligere måneder med snapshots + PDF
- Provisionsmotor med 5-trinslåsning for SIMO og 10-trinslåsning for VOICE
- Intramanager-timer caches pr. rapportperiode
- Mailbaseret salgsregistrering til Excel Online via Outlook desktop og Power Automate

## Salgsregistrering via mailflow

Programmet bruger ikke lokal Excel og kræver ikke Azure/Entra-adgang for brugerne. Når salgsregistrering er slået til i Indstillinger, opretter appen en Outlook-mail fra brugerens klassiske Outlook desktop og sender den til den flow-mailboks, der er gemt i indstillingerne.

Programmet gemmer ikke Outlook-adgangskoder og opretter ikke Microsoft Graph tokens. Outlook skal bare være installeret og konfigureret på brugerens Windows-profil.

Payloaden indeholder bl.a.:

- `date`, `sellerInitials`, `orderNumber`, `cvrNumber`, `companyName`, `phoneNumber`
- `recipient`, `mailSubject` og `mailHtml`
- `items` med produktnavn, antal, point og aliases til matching mod masterarket

Mailen indeholder en HTML-tabel og en JSON-vedhæftning. Et Power Automate-flow kan starte med **When a new email arrives**, læse JSON-vedhæftningen og bruge de faste felter til at oprette rækken i Excel Online.

Se `docs/power_automate_salgsregistrering.md` og `scripts/excel_online_sales_registration.ts` for den konkrete flow-opsætning.

## Cloud-login og data

Version 1.4.0 og nyere bruger Supabase-projektet `provi tracker` til login og datalagring. Appen bruger brugernavn og kodeord, ikke email-login.

Cloud-data pr. bruger:

- `settings`
- `orders`
- `products`
- en `salesperson`, normaliseret til brugerens brugernavn
- `secrets` med Intramanager-adgangskode krypteret client-side

Ved første login med en tom cloud-profil uploader appen automatisk de gamle lokale JSON-data. Derefter er Supabase source of truth for indstillinger, ordrer, produkter, sælgeren og krypterede adgangsoplysninger. Secrets krypteres med AES-256-GCM før upload med en nøgle afledt af brugerens Provi-login. Den lokale computer gemmer kun en DPAPI-krypteret cloud-session og en DPAPI-krypteret kopi af krypteringsnøglen, så automatisk login kan åbne de krypterede secrets igen.

Databaseschema og RPC-funktioner ligger i `docs/supabase_cloud_schema.sql`.

## Provisionslogik

- Dagsprovision = point * 50
- Månedsprovision = alle månedens point afregnes til højeste nåede sats
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
- `credentials.h` indeholder krypteret Intramanager-login og cloud secret-hjælpere.
- `repository.h` indeholder lokal migrationslæsning, cloud-payloads og produktkatalog-migration.
- `commission.h` indeholder provisions-, lønperiode- og datoberegninger.
- `report_service.h` indeholder HTML/PDF-rapportgenerering og månedsluk.

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

Version 1.4.0 bruger Supabase som primær lagring, så almindelige brugere ikke behøver administratoradgang og kan hente deres opsætning ved login. Gamle versioners lokale JSON-filer bruges stadig som migrationskilde:

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

Appen læser appcast fra:

`https://raw.githubusercontent.com/ypqlmen/ProviTracker/main/appcast.xml`

GitHub Release-tagget til auto-update er `autoupdate`. Auto-update i appen henter zip-assetet, pakker det ud i brugerens tempmappe, starter installeren og rydder op bagefter.

Aktuelle assets for version 1.5.6:

- `ProviTrackerUpdate-1.5.6.zip`
- `ProviBeregnerSetup-1.5.6.exe`

Bemærk: den oprindelige 1.1-build indeholdt WinSparkle DLL'en, men ikke en appcast-URL i selve programmet eller installeren. Brugere på 1.1 skal derfor installere en nyere version manuelt en gang; derefter kan auto-update hente fremtidige versioner.

Version 1.3.24 og nyere bruger appens egen zip-baserede updater i stedet for WinSparkle.

Installeren fra 1.3.9 og nyere migrerer 1.1-data og afinstallerer den gamle 1.1-app, hvis den ligger i en separat mappe.

Udgiv update når GitHub CLI er installeret og logget ind:

```powershell
.\scripts\publish_autoupdate.ps1
```

Scriptet uploader update-zippen og installeren til release-tagget og opdaterer `appcast.xml` på `main`.
