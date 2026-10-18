# Salgsregistrering i Chrome – testversion 0.1.17

Provi Tracker overfører nye salgsregistreringer gennem brugerens eksisterende Chrome-login. Ordrekøen bruger Chrome i stedet for en separat Microsoft-browser. Funktionen er endnu kun i testversionen; den offentlige opdateringskanal ændres ikke af Windows-prøvebygningen.

## Brugerens opsætning

1. Installer den nye Windows-testversion. Åbn **Indstillinger → Salgsregistrering** i Provi Tracker.
2. Gem sælgerinitialer og det direkte masterark-link med `sourcedoc`. Åbn dette masterark én gang i Chrome, og log ind.
3. Vælg **Installer Chrome-udvidelse**. Følg programmets guide: åbn `chrome://extensions`, behold **Udviklertilstand** aktiveret, og vælg **Indlæs udpakket** med den mappesti, programmet kopierer. Det kræver ingen administratorinstallation. Hvis udvidelsen allerede er installeret, bruges samme mappe og samme udvidelses-ID. Kontrollér version **0.1.17** og adgang til Excel og scriptpanelet; Chrome kan bede om ny webstedsadgang ved denne opdatering.
4. Gem det direkte link til masterarket i appen. Vælg **Forbind masterark → Opsæt automatisk i Chrome**.
5. Følg trinene i dialogen. Udvidelsen genbruger eller opretter sit eget versionsbestemte `ProviTrackerAutoV4_…`-script, indsætter den medfølgende kode, læser hele koden tilbage, gemmer, genåbner og kontrollerer den igen. Eksisterende og ændrede scripts overskrives ikke. En skrivefri kørsel skal kvittere fra Excel, før appen melder klar.
6. Slå **Registrer automatisk i masterarket** til, og gem indstillingerne.

**Gem forløbsrapport** i appen eller **Gem opsætningsrapport** i udvidelsen gemmer det seneste forløbs trin og tider – både ved succes og fejl. En særskilt trinoversigt bevarer alle forløbets faser, selv om den detaljerede log afgrænses ved lang ventetid. Rapporten indeholder ikke salgsdata, dokumentlink, kode eller login og sendes ikke automatisk. Microsoft-login/MFA skal fortsat godkendes af brugeren. Ved blokeret Office Scripts eller ændrede Excel-kontroller stopper opsætningen med besked; en manuel reservevej findes i samme dialog.

Udvidelsen bruger almindelige synlige felter og editorens tastatur-/kopi-/indsæt-hændelser. Den læser ikke systemets udklipsholder og kræver ingen nye Chrome-rettigheder. Et genforsøg genbruger det uændrede gemte script. Et afbrudt forløb med ufuldstændig kode kræver manuel kontrol; løsningen overskriver ikke koden automatisk.

Chrome og Microsoft-login skal være aktive. Udvidelsen beskytter de verificerede masterark og sin Intramanager-fane mod Memory Saver og automatisk frysning med en dokumentbundet Web Lock. En allerede frossen masterark-fane skal åbnes én gang; en sovende Intramanager-fane genindlæses automatisk. Opsætning og salg kan foregå i en baggrundsfane; daglig MFA omgås ikke.

## Arkets struktur og skrivning

V4 bruger den valgte arkfane med valgfrit navn. Det kræver de kendte overskrifter i række 2, seneste salg i række 3 og en eksisterende historik med månedsmarkeringer længere nede. Ukendte eller manglende kolonner stopper registreringen.

Et nyt salg udfylder først række 3 og arkiveres derefter nederst. Ved første salg i en nyere måned indsættes en særskilt række med månedens første dato, formateret som `dd.mm.yyyy`; rækken kopierer farve og øvrige formater fra arkets eksisterende månedsmarkering. Arkivrækken kopierer salgsformaterne fra række 3. Eksisterende historik og blankområdet mellem række 3 og historikken bevares. Der indsættes ingen tomme måneder uden salg. Salg fra en tidligere måned kræver manuel registrering i den korrekte del af historikken.

Produkterne bruger præcise katalog-nøgler og de respektive produktkolonner. Tillæg samles i Add-on og specificeres i Bemærkninger. Kundetekst, OSE, CVR og telefon behandles som tekst; produktantal er tal.

## Kvitteringer og genforsøg

Hvert salgs-snapshot har et stabilt registreringsnummer (UUID), og hvert forsøg har et nyt kontrolnummer. OSE er et forretningsnummer og kan gentages i forskellige ordrer. En fuld, frisk V4-kvittering med begge korrekte numre og den korrekte OSE er nødvendig, før programmet gemmer status som registreret. Et tjek af Chrome-forbindelsen alene er utilstrækkeligt.

Et meget skjult journalark `_ProviTrackerReg` reserverer historikrække og indhold før skrivningen. Genforsøg kontrollerer og genbruger denne reservation. En fuldt skrevet række efter et afbrudt forsøg bekræftes uden ny kopi og uden at overskrive seneste salg øverst. Delvis eller manuelt flyttet historik, ændrede salgsdata og uafsluttede reservationer kræver handling; appen gætter ikke på en ny række. Journalen er til genforsøg, ikke en transaktionel lås mellem flere samtidige computere. Brug én Provi Tracker-instans pr. masterark.

Ældre V2-registreringer overføres ikke automatisk igen, fordi de ikke har V3-identitet. De skal kontrolleres manuelt. Rettelser og sletninger til allerede overførte salg ændrer ikke automatisk Excel.

## Chrome-adgang og opdateringer

Udvidelsen kræver Chrome 132 eller nyere for at kunne identificere frosne faner. Den har adgang til virksomhedens præcise SharePoint-vært, EU Excel-rammen og det observerede Office Scripts-panel på `fa000000043.mro1cdnstorage.public.onecdn.static.microsoft`. Panelet må kun bruges, når det er direkte barn af den tilladte Excel-ramme. Den konkrete arbejdsbog identificeres af `sourcedoc` i fanens adresse og kontrolleres igen under forløbet. Kun én tilsvarende fane accepteres. Udvidelsen beskytter det verificerede masterark mod automatisk aflastning med `autoDiscardable:false`. Den oprindelige værdi opbevares i Chromes sessionslager og gendannes ved navigation væk fra arket. Udvidelsen aktiverer ikke fanen. En allerede aflastet eller frosset fane giver `chrome-sleeping`; appen bevarer den samme registrering i køen og prøver igen hvert minut. Et nyt kontrolnummer bruges ved hvert genforsøg. Andre fejl kræver fortsat handling. `autoDiscardable` kombineres med en Web Lock i dokumentet mod automatisk frysning. Beskyttelsen genanvendes efter navigation og udvidelsesopdatering. Den holder ikke computeren tændt, ændrer ikke globale Chrome-indstillinger og omgår ikke login. Office-opsætning venter op til 10 minutter på Microsoft-login og fortsætter derefter i samme forsøg. Intramanager hentes via datoformularen og dens Søg-knap; URL-parametre alene anvendes ikke, da den rigtige side ignorerer dem. Kun en frisk, genindlæst tabel accepteres, og samtidige forespørgsler for samme lønperiode samles i appen.

Udvidelsen betjener Office Scripts' almindelige synlige knapper og parameterfelt. Den læser ikke cookies, adgangskoder, sidetokens eller private Excel-API'er. Kundedata sendes lokalt fra appen gennem native messaging og videre til scriptet i brugerens eget masterark. Appen gemmer den oprindelige ordre før Excel-overførslen. Et afbrudt login eller manglende panel giver en handlingsbesked og et genforsøg fra Ordrer.

Forberedelsen kopierer filerne til `%LOCALAPPDATA%/ProviTrackerChromeBridge/extension` og registrerer hjælpeprogrammet under HKCU. Provi Tracker opdaterer denne mappe fra sin medfølgende pakke. Den aktive udvidelse genindlæses automatisk efter en fuld opdatering, når en igangværende kontrol/registrering er færdig. En tidligere installeret 0.1.1 skal genindlæses manuelt én gang. Nye webstedsrettigheder kan kræve Chrome-bekræftelse. Hver ændring i udvidelsespakken skal hæve versionsnummeret; Windows-prøvebygningen håndhæver det.

## Verifikation og praktiske grænser

Node-prøver kontrollerer produktkatalog, øverste række, historik, måned/årsskifte, formater og afbrudte/genoptagne registreringer. Python- og C++-prøver kontrollerer ordrenumre, stabile registreringsnumre, kvitteringer, kø og native transport. En isoleret Chromium-prøve betjener de faktiske udvidelsesfunktioner mod syntetiske Office-kontroller uden Microsoft-login eller kundedata. Windows-forløbet bruger det installerede hjælpeprogram, HKCU, opdatering/genindlæsning og kvitteringer fra det syntetiske panel.

En skrivefri kørsel af V4 i brugerens rigtige Excel skal validere kompilering, kolonner og historik. Den endelige afprøvning af et autentificeret salg fra brugerens Windows-program i en baggrundsfane sker hos brugeren, før fuld udgivelse. Microsoft kan ændre kontrollernes struktur; løsningen skal i så fald opdateres og afprøves igen.

## Parametervinduet

Udvidelsen genkender `payloadJson` via feltets label, navn eller tilgængelighedstekst. Den ældre hjælpetekst er kun en fallback. Et allerede åbent V4-parametervindue genbruges før kodeeditorens Kør-knap. Salgsdata erstatter den indledende `{"isTest":true}`-værdi; kun en fuld identisk værdi må sendes. Hvis Excel nulstiller et styret felt, udfyldes det igen højst tre gange. Et vindue, som et tidligere forsøg allerede har sendt, kræver kontrol før genbrug. Brug automatisk opsætning fra appen, eller opret V4 via den manuelle reservevej; bevar det gamle V3-script. Genforsøg bindes til den oprindelige arkfane via dens ID. Eksisterende V3-journalrækker behandles som registreringer i Ark1.

## Automatisk opsætning – afprøvning
Den isolerede Chrome-prøve bruger den rigtige Monaco-editor og udvidelsen med krydsdomæne-rammer. Den verificerer ny oprettelse, fuld kodetilbage­læsning, gemning/genåbning, kontrolkvittering uden salg, genbrug, bevarelse af andre scripts og stop ved ændret eller ugemt kode. Den erstatter ikke afprøvning af automatisk opsætning i en brugers autentificerede Microsoft-session.

## Automatiske scriptopdateringer
Efter første automatiske opsætning sammenligner udvidelsen scriptets versionsfingeraftryk med den kode, der følger med den installerede udgave. Ved en ændring oprettes en ny navngivet version inden næste salgsregistrering. Koden kontrolleres efter gemning/genåbning, og en skrivefri kontrol skal kvittere, før det oprindelige salg må fortsætte. Registreringsnummer, forespørgselsnummer og den oprindelige arkfane bevares. Den tidligere scriptversion slettes eller overskrives ikke. En fejl under opdateringen stopper salget og gemmer en strukturel rapport. Den native forbindelse venter på den nye udvidelsesversion, før en opdateringskrævende anmodning sendes. Ældre manuelt opsatte V4-scripts skal overgå med **Opsæt automatisk i Chrome** én gang.
