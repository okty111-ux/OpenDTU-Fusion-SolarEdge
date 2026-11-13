# OpenDTU Fusion v2 + Hoymiles MIT-5000-8T + SolarEdge

Firmware dla płytki OpenDTU Fusion v2: odczyt Hoymiles MIT-5000-8T (radio 868 MHz) i SolarEdge (Modbus TCP lub RS485),
pulpit w stylu aplikacji, historia produkcji, wykresy z przybliżaniem, karta microSD, wyświetlacz SSD1322,
język polski, wysyłanie do chmury Tuya i **aktualizacja z GitHuba**.

Wersja eksperymentalna — kompilowana i sprawdzana  działa
## Pobieranie

Najnowsza wersja: [Releases](../../releases/latest). Każde wydanie `fusion-vN` zawiera:

| Plik | Do czego |
|---|---|
| `fusion-opendtufusionv2.factory.bin` | pierwsze wgranie przez USB pod adres 0x0 (Wi-Fi) |
| `fusion-opendtufusionv2_poe.factory.bin` | pierwsze wgranie, wersja z PoE |
| `fusion-opendtufusionv2.bin`, `fusion-opendtufusionv2_poe.bin` | aktualizacja OTA, używane przez „Aktualizacja z GitHuba” |
| `SHA256SUMS.txt` | sumy kontrolne, sprawdzane przez Fusion przed instalacją |

Instrukcja wgrywania i podłączenia: [INSTRUKCJA.md](INSTRUKCJA.md).

## Aktualizacja z GitHuba

Na Fusion: menu → **Aktualizacja z GitHuba** → **Sprawdź aktualizacje** → **Zainstaluj vN**.
Fusion porównuje swój numer wersji z najwyższym tagiem `fusion-vN` w tym repozytorium.

## Nowa wersja (dla autora)

1. Zmiany w kodzie → nowa łatka w `patches/` (`git format-patch` względem gałęzi `opendtu-base`).
2. `git tag -a fusion-v3 -m "opis zmian"` i `git push origin main fusion-v3`.
3. GitHub Actions („Wydanie Fusion”) buduje oba warianty z `FUSION_VERSION=3` i publikuje wydanie.
   Opis z tagu trafia do opisu wydania i jest widoczny na stronie aktualizacji w Fusion.

## Budowanie lokalnie

```
git clone -b opendtu-base https://github.com/okty111-ux/OpenDTU-Fusion-SolarEdge.git odtu && cd odtu
git am ../patches/*.patch
pio run -e opendtufusionv2 -e opendtufusionv2_poe
```

Katalogi `include/` i `src/` zawierają pliki Fusion do podglądu; `narzedzia/gen.py` generuje strony WWW
(`include/FusionPages.h`, `include/HistoryPage.h`).

| Moduł | Pliki |
|---|---|
| SolarEdge Modbus TCP / RS485 RTU (SunSpec) | SolarEdge, WebApi_solaredge |
| Historia dzień/miesiąc/rok, pulpit `/history` | EnergyHistory, PowerLog, HistoryPage |
| Karta microSD, CSV, `/sd` | SdLogger, WebApi_sd |
| Ustawienia `/fusion/settings`, MPPT i panele | FusionPages, HoymilesChannels |
| OLED SSD1322 | FusionDisplay, FusionDisplayPages |
| Aktualizacja z GitHuba `/fusion/update` | FusionUpdate, FusionVersion.h, WebApi_fusion |
| Chmura Tuya (TuyaLink MQTT) `/fusion/tuya` | FusionTuya, WebApi_fusion |
| Język polski | FusionLang, LangPackPl.h, lang/pl.lang.json |

## Autor

Przygotował Okty z materiałów dostępnych na GitHub (OpenDTU — tbnobody, gałąź MIT-5000-8T — smoe,
dokumentacja OpenDTU Fusion — markusdd). Licencja GPL-2.0-or-later, jak OpenDTU.

## Strona startowa (od fusion-v3)

Po wpisaniu adresu Fusion (np. `http://192.168.0.222`) od razu otwiera się pulpit Fusion (`/history`) z menu po lewej.
Klasyczny widok OpenDTU jest pod `http://<IP_FUSION>/?opendtu` oraz w menu: Urządzenie → OpenDTU.
W wersji fusion-v2 strona startowa była jeszcze klasyczna, a pulpit trzeba było otworzyć ręcznie pod `/history`.
