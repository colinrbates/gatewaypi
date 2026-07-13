#!/bin/bash
# GatewayPi: import NAM captures and IRs from a USB stick.
#
# Copies from the stick into the home-level library:
#   *.nam  anywhere on the stick        -> ~/Captures/
#   *.wav  inside a folder whose path contains "GatewayPi", "IR" or "irs"
#          (case-insensitive)           -> ~/IRs/
# The .wav restriction avoids hoovering up backing tracks and recordings.
set -u

DEV="$1"
DATA=/var/lib/gatewaypi

# Resolve the library folders from config.json, else the app-user home.
APP_USER=$(stat -c "%U" "$DATA")
APP_HOME=$(getent passwd "$APP_USER" | cut -d: -f6)
CAPTURES=$(python3 -c 'import json;print(json.load(open("'"$DATA"'/config.json")).get("capturesDir",""))' 2>/dev/null)
IRS=$(python3 -c 'import json;print(json.load(open("'"$DATA"'/config.json")).get("irsDir",""))' 2>/dev/null)
[ -n "$CAPTURES" ] || CAPTURES="$APP_HOME/Captures"
[ -n "$IRS" ] || IRS="$APP_HOME/IRs"
mkdir -p "$CAPTURES" "$IRS"

MNT=$(mktemp -d /run/gatewaypi-usb.XXXXXX)
cleanup() { umount "$MNT" 2>/dev/null; rmdir "$MNT" 2>/dev/null; }
trap cleanup EXIT
mount -o ro "$DEV" "$MNT" || exit 0

count=0
while IFS= read -r -d '' f; do
  cp -n "$f" "$CAPTURES/" && count=$((count + 1))
done < <(find "$MNT" -maxdepth 5 -iname "*.nam" -print0 2>/dev/null)

while IFS= read -r -d '' f; do
  case "$(dirname "$f" | tr '[:upper:]' '[:lower:]')" in
    *gatewaypi*|*ir*) cp -n "$f" "$IRS/" && count=$((count + 1)) ;;
  esac
done < <(find "$MNT" -maxdepth 5 -iname "*.wav" -print0 2>/dev/null)

chown -R "$APP_USER:$APP_USER" "$CAPTURES" "$IRS"
logger -t gatewaypi "USB import from $DEV: $count file(s) copied"
