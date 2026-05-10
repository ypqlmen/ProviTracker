# Provi Tracker

Qt Widgets desktop-app i C++ med lokal JSON-lagring, per-user installation og WinSparkle/GitHub auto-update.

## Funktioner

- Flere saelgere
- Aktiv saelger gemmes lokalt
- Ordrer med flere produkter i samme ordre
- Redigering og sletning af ordrer
- Dashboard med KPI'er, maal og bonusstatus
- Rapporter i appen og PDF-eksport
- Backup/import
- Automatisk maanedsluk for tidligere maaneder med snapshots + PDF
- Bonusmotor med 5-trinslaasning for SIMO og 10-trinslaasning for VOICE
- Intramanager-timer caches pr. rapportperiode

## Bonuslogik

- Dagsbonus = point * 50
- Maanedsbonus = alle maanedens point afregnes til hoejeste naaede sats
- SIMO bonus = hvis SIMO < 5 => 0, ellers `floor(SIMO / 5) * 5 * 200`
- VOICE bonus = hvis VOICE < 20 => 0, ellers `floor(VOICE / 10) * 10 * 200`

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

## Byg med CMake + Qt 6

```powershell
cmake -S . -B build/Desktop_Qt_6_9_3_MinGW_64_bit-Release -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64 -G "MinGW Makefiles"
cmake --build build/Desktop_Qt_6_9_3_MinGW_64_bit-Release --config Release
```

## Krav

- Qt 6 Widgets
- C++17
- Inno Setup 6 til installer-builds

## Datafiler

Appen gemmer data i brugerens lokale AppData, saa almindelige brugere ikke behoever administratoradgang:

- `salespeople.json`
- `products.json`
- `orders.json`
- `settings.json`
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

GitHub Release-tagget til auto-update er `autoupdate`, og asset-navnet for version 1.3.6 er:

`ProviBeregnerSetup-1.3.6.exe`

Bemærk: den oprindelige 1.1-build indeholdt WinSparkle DLL'en, men ikke en appcast-URL i selve programmet eller installeren. Brugere på 1.1 skal derfor installere en nyere version manuelt én gang; derefter kan auto-update hente fremtidige versioner.

Udgiv update naar GitHub CLI er installeret og logget ind:

```powershell
.\scripts\publish_autoupdate.ps1
```

Scriptet uploader installeren til release-tagget og opdaterer `appcast.xml` paa `main`.
