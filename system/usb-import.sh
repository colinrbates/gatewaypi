#!/bin/bash
# GatewayPi: import NAM models and IRs from a USB stick.
#
# Copies from the stick into /var/lib/gatewaypi:
#   *.nam  anywhere on the stick        -> models/
#   *.wav  inside a folder whose path contains "GatewayPi", "IR" or "irs"
#          (case-insensitive)           -> irs/
# The .wav restriction avoids hoovering up backing tracks and recordings.
set -u

DEV="$1"
DATA=/var/lib/gatewaypi
MNT=$(mktemp -d /run/gatewaypi-usb.XXXXXX)
OWNER=$(stat -c "%U:%G" "$DATA")

cleanup() {
  umount "$MNT" 2>/dev/null
  rmdir "$MNT" 2>/dev/null
}
trap cleanup EXIT

mount -o ro "$DEV" "$MNT" || exit 0

count=0
while IFS= read -r -d '' f; do
  cp -n "$f" "$DATA/models/" && count=$((count + 1))
done < <(find "$MNT" -maxdepth 4 -iname "*.nam" -print0 2>/dev/null)

while IFS= read -r -d '' f; do
  case "$(dirname "$f" | tr '[:upper:]' '[:lower:]')" in
    *gatewaypi*|*ir*) cp -n "$f" "$DATA/irs/" && count=$((count + 1)) ;;
  esac
done < <(find "$MNT" -maxdepth 4 -iname "*.wav" -print0 2>/dev/null)

chown -R "$OWNER" "$DATA/models" "$DATA/irs"
logger -t gatewaypi "USB import from $DEV: $count file(s) copied"
