# Wgrywanie na OpenDTU Fusion v2

**Wersja: fusion-v2** (kolejne wersje: <https://github.com/okty111-ux/OpenDTU-Fusion-SolarEdge/releases>)

Firmware: OpenDTU (gałąź `supporting_5000_MIT_8T`, obsługa Hoymiles MIT-5000-8T) + odczyt SolarEdge przez Modbus TCP,
historia produkcji, zapis CSV na kartę microSD, szacunek mocy każdego panelu i wyświetlacz OLED SSD1322.
Wersja eksperymentalna — skompilowana i zweryfikowana (sumy kontrolne obrazu), nie testowana na fizycznym sprzęcie.

## Który plik?

| Plik | Kiedy |
|---|---|
| `wifi/opendtufusionv2_factory.bin` | **Pierwsze wgranie** przez USB, Fusion v2 z Wi-Fi |
| `poe/opendtufusionv2_poe_factory.bin` | Pierwsze wgranie, Fusion v2 z nakładką PoE (W5500) |
| `*_ota.bin` | Aktualizacja przez przeglądarkę, gdy na płytce już jest OpenDTU |

Plik `factory` wgrywa się zawsze pod adres **0x0** (zawiera bootloader, partycje i aplikację, flash 8 MB).

## Sposób A — przeglądarka (najprościej, Chrome/Edge)

1. Podłącz Fusion kablem USB-C (z transmisją danych).
2. Otwórz <https://espressif.github.io/esptool-js/>, baudrate 921600, kliknij **Connect** i wybierz port (*USB JTAG/serial debug unit*).
3. Kliknij **Erase Flash** (zalecane przy pierwszym wgraniu).
4. Flash Address `0x0`, plik `wifi/opendtufusionv2_factory.bin` → **Program**.
5. Po zakończeniu odłącz i podłącz zasilanie.

## Sposób B — skrypt

- Windows: `flash.bat wifi COM5`
- Linux/macOS: `./flash.sh wifi /dev/ttyACM0` (macOS: `/dev/cu.usbmodem*`)

Wymagany Python 3; skrypt sam doinstaluje `esptool`.

## Sposób C — aktualizacja OTA

Jeśli Fusion ma już OpenDTU: **Settings → Firmware Upgrade** → wybierz `*_ota.bin`.
Konfiguracja (Wi-Fi, inwertery, MQTT) i historia produkcji zostają zachowane.
**Aktualizuj zawsze tą metodą** — sposoby A i B kasują pamięć, razem z historią.

## Gdy płytka nie jest wykrywana

Przytrzymaj **BOOT**, naciśnij i puść **RESET**, puść **BOOT** — płytka wejdzie w tryb wgrywania. Spróbuj innego kabla/portu USB.

## Pierwsze uruchomienie

1. Połącz się z Wi-Fi `OpenDTU-xxxxxx`, hasło `openDTU42`, otwórz <http://192.168.4.1> (login `admin` / `openDTU42`).
2. **Settings → Network**: wpisz swoją sieć Wi-Fi. Zmień hasło admina w **Settings → Security**.
3. **Settings → DTU**: częstotliwość CMT 865–868 MHz (EU), moc nadawania wg potrzeb. Piny Fusion v2 są wkompilowane — **nie wybieraj** w Device Manager profilu „with SH1106/SSD1306 Display” (zajmuje GPIO1/2 potrzebne karcie SD).
4. **Settings → Inverter**: dodaj MIT-5000-8T po numerze seryjnym z naklejki. Wyłącz oryginalną DTU Hoymiles, jeśli odpytuje ten sam inwerter.
5. Resztę ustawisz na stronie **`http://<IP_FUSION>/fusion/settings`** (login admina): radio CMT, inwertery,
   panele na MPPT, SolarEdge (Modbus TCP przez sieć albo RS485), wyświetlacz i karta SD.
6. **Settings → MQTT**: broker — dane trafią pod `solar/<serial>/...`, `solar/solaredge/...`, `solar/fusion/ac/power`.

Sumy SHA-256 plików: `SHA256SUMS.txt`.

## Historia produkcji (nowość)

- Strona z tabelami: `http://<IP_FUSION>/history` (dziś, ostatnie dni, miesiące, lata, liczniki całkowite).
- Dane JSON: `http://<IP_FUSION>/api/history`; kasowanie historii: `curl -u admin:HASLO -X POST http://<IP_FUSION>/api/history/reset`.
- Energia dzienna liczona jest z liczników zapisanych w samych falownikach (Hoymiles YieldTotal, SolarEdge, licznik SE),
  więc po zaniku zasilania płytki nic nie ginie — produkcja z czasu wyłączenia zostanie doliczona do dnia ponownego uruchomienia.
- Zapis do pamięci flash (`/history.json`) co 10 min i o północy, przez plik tymczasowy (odporny na zanik zasilania).
- Bufor cykliczny: 62 dni, 36 miesięcy, 25 lat — najstarsze wpisy są usuwane automatycznie. Plik ma maks. ~10 KB.
- MQTT: `solar/fusion/day/hoymiles`, `day/solaredge`, `day/total`, `day/exported`, `day/imported` (kWh).
- Wymaga synchronizacji czasu (NTP) — bez niej historia nie jest zapisywana.

## Podłączenie — wszystkie piny

| Urządzenie | Sygnał | GPIO Fusion v2 | Złącze |
|---|---|---|---|
| OLED SSD1322 | DIN (SDA) | 9 | wyświetlacz SPI – MOSI |
| | SCLK | 10 | wyświetlacz SPI – SCK |
| | CS | 11 | wyświetlacz SPI – CSN |
| | DC | 12 | wyświetlacz SPI – DC |
| | RES | 13 | wyświetlacz SPI – RST |
| | VCC / GND | 3V3 / GND | |
| microSD (SDMMC 1-bit) | CLK | 14 | wyświetlacz SPI – MISO |
| | CMD | 2 | I2C – SDA |
| | D0 | 1 | I2C – SCL |
| | CS/D3 | 10 kΩ do 3V3 | |
| | VCC / GND | 3V3 / GND | |
| Przycisk stron | | 0 | wbudowany przycisk BOOT |
| SolarEdge RS485 | A / B (+ GND) | 16 R, 45 D, 46 DE, 15 RE_N (na płytce) | złącze RS485 A/B na Fusion |

- Moduł karty musi być „pasywny” 3,3 V (samo gniazdo + rezystory). Moduły z układem buforującym 5 V → 3,3 V
  (np. z kostką 74LVC125) działają tylko w trybie SPI, nie w SDMMC. Zalecane rezystory 10 kΩ do 3V3 na CMD i D0.
- Karta i wyświetlacz nie zajmują sprzętowego SPI, więc NRF24, CMT2300A i W5500 (PoE) działają bez zmian.

## Strona ustawień `/fusion/settings`

Radio CMT (częstotliwość MHz, moc dBm), lista inwerterów (dodawanie po numerze seryjnym, dla MIT zaczyna się od `1520`),
panele na MPPT, SolarEdge z testem połączenia, wyświetlacz i karta SD. Wymaga logowania admina OpenDTU.

## Panele na MPPT (szacunek)

MIT-5000-8T ma 4 MPPT, każdy z jednym wejściem; zwykle 2 panele połączone szeregowo. Inwerter mierzy tylko cały string
(napięcie, prąd, moc, uzysk), więc wartości dla pojedynczego panelu są **szacunkiem**:

- szereg: prąd wspólny, moc i napięcie dzielone proporcjonalnie do Vmp (jeśli podasz Vmp obu paneli) albo do Wp,
- równolegle: napięcie wspólne, prąd i moc dzielone proporcjonalnie do Wp.

Dla paneli o różnej mocy wpisz Wp i Vmp z tabliczek — wtedy podział jest dokładniejszy. Zacienienie jednego panelu
nie będzie widoczne osobno; do pomiaru rzeczywistego potrzebne są optymalizatory lub osobne czujniki.
Uwaga: w szeregu prąd jest ograniczony przez słabszy panel, a suma Voc przy mrozie musi być poniżej 140 V.

## Karta microSD i pliki CSV

- Strona `http://<IP_FUSION>/sd`: lista plików, pobieranie CSV jednym kliknięciem, usuwanie (admin).
- `/fusion/RRRR/RRRR-MM-DD.csv` — wiersz co 1–60 min (domyślnie 5): moce, uzyski, sieć, napięcia,
  a także dla każdego MPPT `U_V`, `I_A`, `P_W`, `dzis_Wh` oraz szacunek `panelN_P_W_szac`.
- `/fusion/dni.csv` — jeden wiersz na dzień (podsumowanie).
- Format dla polskiego Excela: separator `;`, przecinek dziesiętny, UTF-8 z BOM.
- Kolumny zapisuje się w nagłówku pliku przy pierwszym zapisie danego dnia — zmiana paneli lub inwerterów
  w ciągu dnia obowiązuje w CSV od następnego dnia.
- Karta FAT32 (lub exFAT). Można ją wyjmować — zapis wznowi się w ciągu 30 s po włożeniu.

## Wyświetlacz OLED SSD1322 (256×64)

- Moduł ustaw w tryb **4-wire SPI** (zworki/rezystory BS0/BS1 według opisu modułu; część modułów fabrycznie ma tryb równoległy 80xx).
- Włącz w `/fusion/settings` → Wyświetlacz („Wyświetlacz podłączony”). W OpenDTU → Ustawienia → Sprzęt zostaw wyświetlacz „brak”.
- Przycisk BOOT: krótko — następna strona (pierwsze naciśnięcie budzi wygaszony ekran), przytrzymanie 1 s — automatyczne
  przełączanie wł./wył. Własny przycisk można podłączyć równolegle do BOOT (GPIO0 → GND). Nie trzymaj go przy włączaniu
  zasilania — płytka wejdzie w tryb wgrywania.
- Strony: Produkcja, Wykres mocy (dziś), Sieć (licznik SolarEdge), Hoymiles MPPT, Panele (szacunek), SolarEdge, Historia, Status.
  Każdą można wyłączyć; strony bez danych są pomijane.
- Wygaszanie nocą: po N minutach bez produkcji i bez naciśnięcia przycisku; rano ekran włącza się sam.
- Podgląd ekranów: `podglad/ssd1322_dzien.png`, `podglad/ssd1322_noc.png` (rysowane tym samym kodem co w firmware).

## Wykresy

- Strona `/history`: wykres mocy w ciągu dnia (Dziś / Wczoraj). Hoymiles i SolarEdge są pokazane warstwowo,
  z linią „teraz” i podpowiedzią po najechaniu lub dotknięciu. Pod nim słupki energii: 30 dni / miesiące / lata.
- Wyświetlacz SSD1322: strona „Wykres mocy - dziś”. Hoymiles to pełne pole, SolarEdge kratka nad nim.
  Obok szczyt, godzina szczytu i energia dziś. Zakres godzin dopasowuje się do produkcji.
- Zapis: średnia moc co 5 minut, dziś i wczoraj, w pliku `/power.bin` (pamięć flash). Zapis co 10 min i o północy,
  więc po zaniku zasilania zostaje wykres najwyżej bez ostatnich 10 minut. W czasie, gdy płytka była wyłączona,
  na wykresie jest przerwa.
- Dane JSON: `http://<IP_FUSION>/api/history/power?day=0` (dziś) lub `?day=1` (wczoraj).
- Wykresy działają bez internetu (rysowane w przeglądarce, bez zewnętrznych bibliotek).

## SolarEdge przez RS485 (Modbus RTU)

1. `/fusion/settings` → SolarEdge → Sposób komunikacji: **RS485 (Modbus RTU)**. Prędkość 115200, 8N1, Modbus ID 1
   (domyślne SolarEdge) albo takie, jak ustawisz w falowniku.
2. SetApp: Commissioning → Site Communication → **RS485-1** → Protocol: **SunSpec (Non-SE Logger)**, Device ID, Baud rate.
   Jeśli RS485-1 jest zajęty (licznik energii SolarEdge, bateria), użyj **RS485-2**.
3. Przewód: skrętka, A falownika → A Fusion, B → B, GND → GND. Przy długiej linii terminator 120 Ω
   (na Fusion jest nieobsadzone miejsce R16 na rezystor).
4. „Sprawdź połączenie” pokaże model i numer seryjny. Brak odpowiedzi: zamień A/B, sprawdź Device ID i prędkość.
- Falownik obsługuje tylko jednego mastera Modbus: RS485 albo TCP, nie oba naraz.
- RS485 jest na płytce Fusion (GPIO 15/16/45/46), więc nie koliduje z kartą SD, wyświetlaczem ani W5500 (PoE).
- Licznik SolarEdge (eksport/import) odczytywany jest tak samo jak przez TCP, jeśli jest podłączony do falownika.

## Pulpit w stylu aplikacji i menu boczne

- `/history` to teraz pulpit „Mój dom”: grafika domu z panelami (dzień ze słońcem, noc z księżycem i gwiazdami),
  dzisiejsza produkcja, pierścień „Bieżąca moc” (pomarańczowy Hoymiles, niebieski SolarEdge), kafelki źródeł i sieci,
  karta „Wytworzona energia” (miesiąc / rok / okres eksploatacji) i wykres z zakładkami Dzień / Tydzień / Miesiąc / Rok / Łącznie
  ze strzałkami poprzedni / następny / dziś.
- Menu z lewej: Pulpit, Wykresy, Inwertery i panele, Historia, Karta SD, Ustawienia, OpenDTU. Na telefonie chowa się pod ☰.
  Takie samo menu jest na stronach `/fusion/settings` i `/sd`.
- Zamiast temperatury pogody pokazany jest wschód / zachód słońca — Fusion działa bez internetu.
- Widok „Dzień” obejmuje dziś i wczoraj (krzywa 5-min z 2 dni), „Miesiąc” słupki dzienne z ostatnich 62 dni.
- Podgląd: `podglad/historia_dzien_desktop.png`, `historia_noc_mobile.png`, `historia_dzien_mobiledark.png`, `menu_mobile.png`.

## Język polski

- Firmware ma wbudowaną pełną polską paczkę językową OpenDTU (wszystkie 707 tekstów interfejsu).
  Przy pierwszym uruchomieniu zapisuje się sama jako `/pl.lang.json` — nie trzeba niczego wgrywać ręcznie.
- W OpenDTU wybierz w prawym górnym rogu **Polski**; wybór jest zapamiętany w przeglądarce.
- Wyświetlacz OpenDTU (SSD1306/SH1106) przy nowej konfiguracji domyślnie używa języka polskiego
  (Ustawienia → Sprzęt → Język wyświetlacza → Polski).
- Strony Fusion (pulpit, ustawienia, karta SD) i wyświetlacz SSD1322 są po polsku.
- Plik `zrodla/pl.lang.json` można też wgrać ręcznie: Ustawienia → Zarządzanie konfiguracją → Przywróć → Paczka językowa.

## Aktualizacja z GitHuba

- Menu → **Aktualizacja z GitHuba** (`/fusion/update`). Widać zainstalowaną wersję (np. `v2`) i najnowszą wersję z GitHuba
  z opisem zmian. Przycisk **Sprawdź aktualizacje**, potem **Zainstaluj vN** — Fusion pobiera plik, pokazuje postęp,
  sprawdza sumę SHA-256 i dopiero wtedy przełącza się na nową wersję oraz uruchamia ponownie.
- Wydania są w repozytorium <https://github.com/okty111-ux/OpenDTU-Fusion-SolarEdge> z tagami `fusion-v2`, `fusion-v3`…
  Każde wydanie zawiera `fusion-opendtufusionv2.bin` (Wi-Fi), `fusion-opendtufusionv2_poe.bin` (PoE) i `SHA256SUMS.txt`.
  Fusion sam wybiera plik pasujący do płytki.
- Fusion sprawdza GitHuba 2 minuty po starcie i potem raz na dobę; gdy jest nowsza wersja, w menu przy
  „Aktualizacja z GitHuba” pojawia się plakietka `vN`. Instalacja zawsze wymaga kliknięcia.
- Ustawienia, historia i karta SD zostają. Gdy pobieranie się przerwie lub suma się nie zgodzi, zostaje obecna wersja.
- Na tej stronie można zmienić repozytorium (np. własny fork) i prefiks tagu.
- Potrzebny jest dostęp do internetu. Pierwszą wersję z tą funkcją (fusion-v2) trzeba wgrać raz ręcznie (sposób A, B lub C).

## Chmura Tuya

- Menu → **Chmura Tuya** (`/fusion/tuya`). Fusion wysyła co 60 s (do ustawienia 10–3600 s) dane jako urządzenie TuyaLink
  przez MQTT TLS (port 8883) — widać je w aplikacji Smart Life / Tuya i można ich użyć w scenach.
- Na <https://platform.tuya.com>: Product → Create → **TuyaLink**, w modelu dodaj właściwości (typ Value, skala 0, tylko odczyt):
  `pv_power` W, `hoymiles_power` W, `solaredge_power` W, `grid_power` W (+ eksport / − pobór),
  `energy_today` Wh, `energy_month` kWh, `energy_total` kWh. Potem Device → Add device i skopiuj **Device ID** i **Device Secret**.
- Na stronie Fusion: zaznacz **Wysyłanie włączone**, wybierz centrum danych (Polska: Europa Centralna), wpisz ID i Secret,
  **Zapisz i połącz**. Status pokazuje liczbę wysłanych i potwierdzonych raportów oraz kod odpowiedzi
  (np. 2003 = właściwość nie jest zdefiniowana w modelu).
- Device Secret nie jest pokazywany na stronie. Wymagany jest poprawny czas z NTP (domyślnie włączony w OpenDTU).

## Wykresy z przybliżaniem

- Na pulpicie wykres (Dzień, Tydzień, Miesiąc, Rok, Łącznie) można przybliżać:
  - komputer: kółko myszy nad wykresem (przybliża w miejscu kursora), przeciągnięcie przesuwa, podwójne kliknięcie pokazuje całość;
  - telefon: rozsunięcie dwóch palców, przesuwanie palcem w poziomie;
  - przyciski **+**, **−** i **⛶** (całość) nad wykresem; obok widać przybliżony zakres, np. `10:30–12:00`.
- Skala pionowa dopasowuje się do widocznego fragmentu. Przy dużym przybliżeniu krzywej dnia widać punkty co 5 minut,
  a nad słupkami wartości w kWh.

## Autor

Przygotował Okty z materiałów dostępnych na GitHub (OpenDTU — tbnobody, gałąź MIT-5000-8T — smoe,
dokumentacja OpenDTU Fusion — markusdd). Licencja GPL-2.0-or-later, jak OpenDTU.
