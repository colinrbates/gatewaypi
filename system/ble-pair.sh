#!/bin/bash
# GatewayPi: one-time pairing helper for the M-Vave Chocolate.
#
# Usage:  sudo ble-pair.sh            (scans for a device named *Chocolate*/FootCtrl*)
#         sudo ble-pair.sh AA:BB:CC:DD:EE:FF   (pair a specific MAC)
#
# On success the MAC is written to /var/lib/gatewaypi/config.json and the
# gatewaypi-blemidi service keeps it connected from then on.
set -euo pipefail

CONFIG=/var/lib/gatewaypi/config.json
MAC="${1:-}"

if [ -z "$MAC" ]; then
  echo "Put the Chocolate in Bluetooth mode, scanning for 15 s..."
  bluetoothctl --timeout 15 scan on > /dev/null 2>&1 || true
  MAC=$(bluetoothctl devices | grep -iE "chocolate|footctrl|m-?vave" | head -1 | awk '{print $2}')
  if [ -z "$MAC" ]; then
    echo "No Chocolate found. Is it on and in BT mode? (Its name should contain"
    echo "'Chocolate' or 'FootCtrl' — otherwise rerun with the MAC as argument:"
    bluetoothctl devices
    exit 1
  fi
  echo "Found pedal at $MAC"
fi

bluetoothctl pair "$MAC" || true   # BLE-MIDI devices often skip classic pairing
bluetoothctl trust "$MAC"
bluetoothctl connect "$MAC"

python3 - "$MAC" << 'EOF'
import json, sys
path = "/var/lib/gatewaypi/config.json"
try:
    cfg = json.load(open(path))
except Exception:
    cfg = {}
cfg["bleMidiMac"] = sys.argv[1]
json.dump(cfg, open(path, "w"), indent=2)
print(f"Saved bleMidiMac={sys.argv[1]} to {path}")
EOF

echo "Done. The gatewaypi-blemidi service will now auto-reconnect this pedal."
