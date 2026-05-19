# Provi Tracker

Qt Widgets desktop-app i C++ med lokal JSON-lagring, per-user installation og WinSparkle/GitHub auto-update.

## Funktioner

- Flere saelgere
- Aktiv saelger gemmes lokalt
- Ordrer med flere produkter i samme ordre
- Redigering og sletning af ordrer
- Dashboard med KPI'er, maal og provisionsstatus
- Rapporter i appen og PDF-eksport
- Backup/import
- Automatisk maanedsluk for tidligere maaneder med snapshots + PDF
- Provisionsmotor med 5-trinslaasning for SIMO og 10-trinslaasning for VOICE
- Intramanager-timer caches pr. rapportperiode
- Mailbaseret salgsregistrering til Excel Online/Outlook via Microsoft Graph og Power Automate

## Salgsregistrering via mailflow

Programmet bruger ikke lokal Excel eller Outlook. Naar salgsregistrering er slaaet til i Indstillinger, sender appen en Microsoft Graph-mail til den flow-mailboks, der er gemt i indstillingerne.

Programmet aabner Microsoft-login i browseren, saa MFA haandteres af Microsoft, og gemmer kun refresh-token i Windows Credential Manager. Programmet gemmer ikke Outlook-adgangskoder.

Payloaden indeholder bl.a.:

- `date`, `sellerInitials`, `orderNumber`, `cvrNumber`, `companyName`, `phoneNumber`
- `recipient`, `mailSubject` og `mailHtml`
- `items` med produktnavn, antal, point og aliases til matching mod masterarket

Mailen indeholder en HTML-tabel og en JSON-vedhaeftning. Et Power Automate-flow kan starte med **When a new email arrives**, laese JSON-vedhaeftningen og bruge de faste felter til at oprette raekken i Excel Online.

Se `docs/power_automate_salgsregistrering.md`, `docs/microsoft_oauth_power_automate_setup.md` og `scripts/excel_online_sales_registration.ts` for den konkrete flow-opsaetning.

## Provisionslogik

- Dagsprovision = point * 50
- Maanedsprovision = alle maanedens point afregnes til hoejeste naaede sats
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

- `main_v32.cpp` indeholder app-start, hovedvindue, UI-opbygning og brugerflows.
- `storage_paths.h` indeholder AppData-stier og migration fra gamle installationer.
- `domain.h` indeholder modeller, settings og JSON-serialisering.
- `credentials.h` indeholder krypteret Intramanager-login og Microsoft Credential Manager-hjaelpere.
- `repository.h` indeholder lokal JSON-lagring og produktkatalog-migration.
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

Appen gemmer data i brugerens lokale AppData, saa almindelige brugere ikke behoever administratoradgang:

- `salespeople.json`
- `products.json`
- `orders.json`
- `settings.json`
- `intramanager_login.json` med brugerbundet DPAPI-krypteret Intramanager-adgangskode
- `snapshots/`
- `reports/`

## Installer og auto-update

Installer-scriptet ligger i `installer/ProviTracker.iss` og installerer til:

`%LOCALAPPDATA%\Programs\Provi Tracker`

Byg installer:

```powershell
.\scripts\build_installer.ps1
```

WinSparkle laeser appcast fra:

`https://raw.githubusercontent.com/ypqlmen/ProviTracker/main/appcast.xml`

GitHub Release-tagget til auto-update er `autoupdate`, og asset-navnet for version 1.3.21 er:

`ProviBeregnerSetup-1.3.21.exe`

Bem?rk: den oprindelige 1.1-build indeholdt WinSparkle DLL'en, men ikke en appcast-URL i selve programmet eller installeren. Brugere p? 1.1 skal derfor installere en nyere version manuelt ?n gang; derefter kan auto-update hente fremtidige versioner.

Installeren fra 1.3.9 og nyere migrerer 1.1-data og afinstallerer den gamle 1.1-app, hvis den ligger i en separat mappe.

Udgiv update naar GitHub CLI er installeret og logget ind:

```powershell
.\scripts\publish_autoupdate.ps1
```

Scriptet uploader installeren til release-tagget og opdaterer `appcast.xml` paa `main`.
