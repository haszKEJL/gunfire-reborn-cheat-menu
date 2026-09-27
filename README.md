# ScarletAim — Gunfire Reborn Steam / C++17 / Windows x64

Menu Dear ImGui, aimbot, ESP, GlowESP i opcje walki. Metody gry sa odnajdywane w dzialajacym IL2CPP po nazwach i sygnaturach, bez zapisanych offsetow dla buildu Steam **25361614**.

## Uruchomienie

1. Uruchom Gunfire Reborn w **oknie lub borderless**. Nakladka ESP nie jest przeznaczona do wylacznego trybu pelnoekranowego.
2. Uruchom `Uruchom.cmd` albo `bin/ScarletAim.exe`. Jedyna sciezka uruchomienia runtime to manual mapping; `Uruchom-ManualMap.cmd` jest aliasem.
3. **Insert** otwiera/zamyka menu. Otwieraj je po wstrzymaniu rozgrywki; aimbot jest wstrzymany, dopoki menu pozostaje otwarte.
4. Domyslnie przytrzymaj **lewy Shift**, aby celowac. Klawisz i tryb aktywacji zmienisz w menu.
5. **End** konczy dzialanie, odpina hooki i przywraca oryginalne materialy modeli.

Trzymaj `ScarletAim.exe`, `ScarletAim.dll`, `glow.bundle` i katalog `licenses` razem w `bin`. Nie trzeba kopiowac plikow do instalacji gry. Ustawienia sa zapisywane przy zamknieciu w `bin/settings.bin`; ustawienia v4 i v5 sa wczytywane i zapisywane w nowym formacie v6. Przyciski zapisu/wczytania sa tez w menu.

## Funkcje

**Aimbot i Legit:** dowolny klawisz, Hold/Toggle/Always, Aimlock, Silent, filtr widocznosci, FOV 1-179 stopni, wygladzanie, dystans, wybor weakspotu i priorytet celu. Zakladka Legit steruje martwa strefa, predkoscia pozioma i pionowa, przyspieszeniem, zwalnianiem przy celu, zmiennoscia reakcji i predkosci, przywiazaniem do celu oraz celowaniem tylko podczas strzelania. Ustawienia sa wspolne dla wszystkich broni.

**ESP:** ramki pelne, narozniki lub poziome; grubosc, zaokraglenie, obwodka, wypelnienie, wielkosc tekstu, nazwy nad/pod modelem, odleglosc, linie, punkty glowy i srodka, podswietlenie wybranego celu oraz strzalki poza ekranem. Dostepne sa tez HP, tarcza, pancerz, filtr widocznosci, osobne kolory i okrag FOV. Dokladna sylwetka liczy ramke z aktualnej siatki animowanego modelu i obciaza CPU bardziej niz zwykle granice.

**Triggerbot:** strzal, gdy celownik jest na ramce przeciwnika albo na jego weakspocie. Ma wlasny klawisz aktywacji, filtr widocznosci, opoznienie, odstep miedzy strzalami i limit zasiegu.

**Walka:** No Spread prostuje kierunek promieni RayCast i dwoch obslugiwanych rodzin pociskow; wspiera tez metode `GetSpreadAccuracy`, gdy jest wywolywana. No Recoil wylacza dwa wywolania odrzutu. Weakness Hit zmienia czesc trafienia potwora na weakspot. Suwaki 1-3x steruja szybkoscia ruchu, przeladowania i strzelania przez metody wlasnej postaci.

**World:** Item ESP ma ramki dopasowane do modeli dungeon, portali, kupcow i skrzyn. Kategorie maja osobne przelaczniki i kolory; dostepne sa nazwy, odleglosc, linie, wypelnienie i zasieg. Pozycje ekranowe i ramki sa przeliczane co klatke, a lista obiektow co 200 ms. Drzwi na listach ukrytych i transferowych sa klasyfikowane jako portale jeszcze przed odkryciem, jesli gra utworzyla juz ich obiekt. Dziala niezaleznie od ESP przeciwnikow. Auto Pickup przenosi lezace przedmioty do postaci co 0.5 s, gdy gra jest na pierwszym planie. Zakladki Aimbot i ESP maja podglady ustawien.

**Jezyki:** PL (bez polskich znakow), EN i RU. Wybor w naglowku menu albo w Ustawieniach; rosyjskie znaki sa ladowane do czcionki. Nazwy przeciwnikow pochodza z lokalizacji gry.

**GlowESP:** niezalezne od ESP podswietlenie geometrii modeli, opcja przez sciany, wlasny kolor, krycie, intensywnosc i zasieg. Uzywa lokalnie przygotowanego wariantu shadera z instalacji gry. Zmiany materialow sa tymczasowe; oryginaly sa przywracane po wylaczeniu efektu, utracie celu, zmianie sceny/kamery i przy End. Nie zmienia plikow gry.

**Manual mapping:** loader sprawdza PE x64, mapuje sekcje, uzupelnia importy i relokacje, inicjalizuje TLS/CRT oraz rejestruje dane do wyjatkow. Po potwierdzeniu pracy hooka aktualizacji gry zeruje naglowek PE i zabezpiecza jego strone. Recznie mapowanej DLL nie rejestruje na listach loadera, dlatego nie ma tam jej wpisu do usuwania. MinHook zmienia ochrone wybranych stron kodu gry na czas instalacji/odpinania hookow. Przy End hooki i materialy sa przywracane; sama alokacja obrazu pozostaje do zamkniecia gry, aby nie zwolnic kodu wykonywanego przez watki.

Zgodnie z zamowieniem nie ma opcji Auto scope, Auto shot, Ignore flash, Ignore smoke, Friendly fire, Max aim inaccuracy, Max shot inaccuracy, Min damage, Killshot ani Between shots.

## Ograniczenia

- Silent koryguje wspolna pozycje celu, dwa promienie kamery, wybrane metody celu umiejetnosci i trzy rodziny torow pociskow: **RayCastCartoon**, **ThrowByPowerCartoon** i **EntityParabolaCartoon**. Dla uzytej w poprzednim tescie broni zaobserwowano korekty podczas strzalow. Nie kazda bron ani umiejetnosc byla dostepna do testu; czesc atakow moze uzywac innych metod.
- Zwykly aimbot steruje wzglednym ruchem myszy. Jego szybkosc zalezy rowniez od czulosci ustawionej w grze; Smooth reguluje tempo prowadzenia. Brak wyprzedzania celu dla wolnych pociskow.
- Nie ma automatycznych profili osobnych kategorii broni ani kalkulatora obrazen. „Najlepszy slaby punkt” korzysta z metody gry, a nie z szacowania obrazen.
- Glow zmienia wyglad modeli na czas dzialania. Intensywnosc i bloom gry wplywaja na wynik. ESP pobiera wyswietlana nazwe potwora z aktualnej tabeli gry na podstawie jego SID, zamiast z pola nicku wlasciciela.
- Weakness Hit, No Recoil i Triggerbot wymagaja dalszych testow skutku w grze. Samo zainstalowanie hooka lub zliczenie trafien nie potwierdza efektu; host sieciowy moze nadpisac zmiany stanu obrazen i przedmiotow.
- Ramki obiektow korzystaja z granic rendererow Unity; gdy obiekt nie ma renderera, stosowany jest przyblizony obrys wokol jego pozycji.
- Portal, ktorego obiekt nie istnieje jeszcze w stanie klienta, nie ma dostepnej pozycji do narysowania. Klasyfikacja ukrytych drzwi i nowe mnozniki predkosci wymagaja sprawdzenia w aktywnej rozgrywce.
- Dynamiczne rozwiazywanie API pozwala przetrwac zmiane offsetow miedzy aktualizacjami. Zmiana nazw, sygnatur, zachowania metod lub zasobow shadera moze wymagac nowej wersji; nie da sie zagwarantowac dzialania po kazdej przyszlej aktualizacji.

## Kompilacja

`./build.ps1` kompiluje `ScarletAim.exe` i DLL lokalnym `C:/msys64/ucrt64/bin/g++.exe`. `./build.ps1 -RuntimeOnly` przebudowuje tylko DLL. Parametr `-LauncherName` pozwala ustawic inna nazwe EXE, gdy poprzednia wersja jest uruchomiona. Zrodla ImGui 1.91.9b i MinHook 1.3.4 oraz ich licencje sa w `vendor`.

Do zbudowania paczki wydania sluzy `./package.ps1 -SkipBuild` po kompilacji. Paczka zawiera `bin/glow.bundle`; do odtworzenia go z wlasnej instalacji gry sluzy `python scripts/prepare_glow.py <sciezka-do-assetbundle>`. Skrypt potrzebuje UnityPy. To narzedzie developerskie: dzialajacy program pozostaje w C++ i nie wymaga Pythona.

## Diagnostyka i weryfikacja

- `bin/ScarletAim.exe --probe` — odczyt celow i rendererow bez sterowania.
- `bin/ScarletAim.exe --render-test` — test Glow z ESP wylaczonym.
- `bin/ScarletAim.exe --silent-test` — 30-sekundowy pomiar Silent i No Spread bez automatycznych strzalow.
- `bin/ScarletAim.exe --combat-test` — 20-sekundowy pomiar Silent, No Spread i Weakness Hit.
- `bin/ScarletAim.exe --world-test` — czterosekundowy odczyt Item ESP i liczby przeniesionych przedmiotow.
- `bin/ScarletAim.exe --map-test` — samodzielny test loadera bez gry.
- `bin/ScarletAim.exe --preview`, `--preview-en`, `--preview-ru`, `--preview-legit`, `--preview-esp`, `--preview-world` — podglady menu bez laczenia z gra.
- `bin/ScarletAim.exe --inspect` — diagnostyczny spis wybranych klas API do `bin/aim.log`.

Weryfikacja poprzedniej wersji w aktywnej grze: nazwy przeciwnikow, Glow, Silent i Auto Pickup byly odczytywane poprawnie. Wersja 1.0.0 kompiluje sie; nowe sciezki portali, predkosci i umiejetnosci wymagaja jeszcze sprawdzenia w aktywnej rozgrywce.

Zrodla informacji o API i licencje zaleznosci: [THIRD_PARTY.md](THIRD_PARTY.md).
