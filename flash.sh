#!/usr/bin/env bash
# Wgrywanie OpenDTU Fusion v2 + MIT-5000-8T + SolarEdge
# Użycie: ./flash.sh [wifi|poe] [port]     np. ./flash.sh wifi /dev/ttyACM0
set -e
VARIANT=${1:-wifi}; PORT=${2:-}
DIR="$(cd "$(dirname "$0")" && pwd)"
case "$VARIANT" in
  wifi) IMG="$DIR/wifi/opendtufusionv2_factory.bin" ;;
  poe)  IMG="$DIR/poe/opendtufusionv2_poe_factory.bin" ;;
  *) echo "Wariant: wifi albo poe"; exit 1 ;;
esac
python3 -m esptool version >/dev/null 2>&1 || pip3 install --user esptool
PORTARG=(); [ -n "$PORT" ] && PORTARG=(--port "$PORT")
echo "Kasowanie flash..."
python3 -m esptool --chip esp32s3 "${PORTARG[@]}" erase-flash
echo "Wgrywanie $IMG ..."
python3 -m esptool --chip esp32s3 "${PORTARG[@]}" --baud 921600 write-flash 0x0 "$IMG"
echo "Gotowe. Odłącz i podłącz zasilanie, potem połącz się z Wi-Fi OpenDTU-xxxxxx (hasło openDTU42)."
