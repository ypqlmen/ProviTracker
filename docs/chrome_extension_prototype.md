# Salgsregistrering i Chrome – testversion 0.1.4

Provi Tracker overfører nye salgsregistreringer gennem brugerens eksisterende Chrome-login. Ordrekøen bruger Chrome i stedet for en separat Microsoft-browser. Funktionen er endnu kun i testversionen; den offentlige opdateringskanal ændres ikke af Windows-prøvebygningen.

## Brugerens opsætning

1. Installer den nye Windows-testversion. Åbn **Indstillinger → Salgsregistrering** i Provi Tracker.
2. Gem sælgerinitialer og det direkte masterark-link med `sourcedoc`. Åbn dette masterark én gang i Chrome, og log ind.
3. Vælg **Installer Chrome-udvidelse**. Følg programmets guide: åbn `chrome://extensions`, behold **Udviklertilstand** aktiveret, og vælg **Indlæs udpakket** med den mappesti, programmet kopierer. Det kræver ingen administratorinstallation. Hvis udvidelsen allerede er installeret, bruges samme mappe og samme udvidelses-ID. Kontrollér version **0.1.4** og adgang til Excel og scriptpanelet; Chrome kan bede om ny webstedsadgang ved denne opdatering.
4. Vælg **Forbind masterark**. Her ligger guiden og knappen **Kopier Excel-script**. Opret et nyt Office-script via **Automatiser → Nyt script → Opret i Kodeeditor**, og omdøb det til **ProviTrackerSalesRegistrationV3**. Markér hele standardkoden, indsæt den kopierede kode, og gem. Bevar eventuelle tidligere scripts.
5. Lad V3-scriptet være åbent i kodeeditoren. Vælg **Kontrollér opsætning** i programmets guide. Denne kontrol skal bekræftes af Excel og tilføjer ingen salg.
6. Slå **Registrer automatisk i masterarket** til, og gem indstillingerne. Gem den næste nye ordre med alle kundedata og produkter. Kontrollér **Registreret i masterark** på Ordrer og de to placeringer i Excel.

Excel-scriptet skal oprettes én gang pr. arbejdskonto. Brugeren får hjælp inde i programmet og skal ikke hente kildefiler fra GitHub. Scriptet kan genfindes under Automatiser. Det åbne masterark må være en baggrundsfane; brugeren skal ikke aktivere fanen for hvert salg. Browseren og masterarket skal være åbne og logget ind. Microsofts krav om login/MFA gælder fortsat. Mailafsendelse er en senere opgave.

## Arkets struktur og skrivning

V3 kræver fanen Ark1, de kendte overskrifter i række 2, seneste salg i række 3 og en eksisterende historik med månedsmarkeringer længere nede. Ukendte eller manglende kolonner stopper registreringen.

Et nyt salg udfylder først række 3 og arkiveres derefter nederst. Ved første salg i en nyere måned indsættes en særskilt række med månedens første dato, formateret som `dd.mm.yyyy`; rækken kopierer farve og øvrige formater fra arkets eksisterende månedsmarkering. Arkivrækken kopierer salgsformaterne fra række 3. Eksisterende historik og blankområdet mellem række 3 og historikken bevares. Der indsættes ingen tomme måneder uden salg. Salg fra en tidligere måned kræver manuel registrering i den korrekte del af historikken.

Produkterne bruger præcise katalog-nøgler og de respektive produktkolonner. Tillæg samles i Add-on og specificeres i Bemærkninger. Kundetekst, OSE, CVR og telefon behandles som tekst; produktantal er tal.

## Kvitteringer og genforsøg

Hvert salgs-snapshot har et stabilt registreringsnummer (UUID), og hvert forsøg har et nyt kontrolnummer. OSE er et forretningsnummer og kan gentages i forskellige ordrer. En fuld, frisk V3-kvittering med begge korrekte numre og den korrekte OSE er nødvendig, før programmet gemmer status som registreret. Et tjek af Chrome-forbindelsen alene er utilstrækkeligt.

Et meget skjult journalark `_ProviTrackerReg` reserverer historikrække og indhold før skrivningen. Genforsøg kontrollerer og genbruger denne reservation. En fuldt skrevet række efter et afbrudt forsøg bekræftes uden ny kopi og uden at overskrive seneste salg øverst. Delvis eller manuelt flyttet historik, ændrede salgsdata og uafsluttede reservationer kræver handling; appen gætter ikke på en ny række. Journalen er til genforsøg, ikke en transaktionel lås mellem flere samtidige computere. Brug én Provi Tracker-instans pr. masterark.

Ældre V2-registreringer overføres ikke automatisk igen, fordi de ikke har V3-identitet. De skal kontrolleres manuelt. Rettelser og sletninger til allerede overførte salg ændrer ikke automatisk Excel.

## Chrome-adgang og opdateringer

Udvidelsen har adgang til virksomhedens præcise SharePoint-vært, EU Excel-rammen og det observerede Office Scripts-panel på `fa000000043.mro1cdnstorage.public.onecdn.static.microsoft`. Panelet må kun bruges, når det er direkte barn af den tilladte Excel-ramme. Den konkrete arbejdsbog identificeres af `sourcedoc` i fanens adresse og kontrolleres igen under forløbet. Kun én tilsvarende, ikke parkeret fane accepteres.

Udvidelsen betjener Office Scripts' almindelige synlige knapper og parameterfelt. Den læser ikke cookies, adgangskoder, sidetokens eller private Excel-API'er. Kundedata sendes lokalt fra appen gennem native messaging og videre til scriptet i brugerens eget masterark. Appen gemmer den oprindelige ordre før Excel-overførslen. Et afbrudt login eller manglende panel giver en handlingsbesked og et genforsøg fra Ordrer.

Forberedelsen kopierer filerne til `%LOCALAPPDATA%/ProviTrackerChromeBridge/extension` og registrerer hjælpeprogrammet under HKCU. Provi Tracker opdaterer denne mappe fra sin medfølgende pakke. Den aktive udvidelse genindlæses automatisk efter en fuld opdatering, når en igangværende kontrol/registrering er færdig. En tidligere installeret 0.1.1 skal genindlæses manuelt én gang. Nye webstedsrettigheder kan kræve Chrome-bekræftelse. Hver ændring i udvidelsespakken skal hæve versionsnummeret; Windows-prøvebygningen håndhæver det.

## Verifikation og praktiske grænser

Node-prøver kontrollerer produktkatalog, øverste række, historik, måned/årsskifte, formater og afbrudte/genoptagne registreringer. Python- og C++-prøver kontrollerer ordrenumre, stabile registreringsnumre, kvitteringer, kø og native transport. En isoleret Chromium-prøve betjener de faktiske udvidelsesfunktioner mod syntetiske Office-kontroller uden Microsoft-login eller kundedata. Windows-forløbet bruger det installerede hjælpeprogram, HKCU, opdatering/genindlæsning og kvitteringer fra det syntetiske panel.

En skrivefri kørsel af V3 i brugerens rigtige Excel validerer kompilering, kolonner og historik. Den endelige afprøvning af et autentificeret salg fra brugerens Windows-program i en baggrundsfane sker hos brugeren, før fuld udgivelse. Microsoft kan ændre kontrollernes struktur; løsningen skal i så fald opdateres og afprøves igen.
