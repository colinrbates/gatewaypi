#!/bin/bash
# GatewayPi: keep the M-Vave Chocolate connected over BLE-MIDI.
#
# Reads the pedal's MAC from /var/lib/gatewaypi/config.json ("bleMidiMac").
# Pair the pedal once with /opt/gatewaypi/bin/ble-pair.sh, which stores the
# MAC there.  If no MAC is configured this service just idles.
set -u

CONFIG=/var/lib/gatewaypi/config.json

get_mac() {
  python3 -c '
import json, sys
try:
    print(json.load(open("'"$CONFIG"'")).get("bleMidiMac", ""))
except Exception:
    print("")
' 2>/dev/null
}

while true; do
  MAC=$(get_mac)
  if [ -z "$MAC" ]; then
    sleep 30
    continue
  fi
  if ! bluetoothctl info "$MAC" 2>/dev/null | grep -q "Connected: yes"; then
    bluetoothctl connect "$MAC" > /dev/null 2>&1
  fi
  sleep 5
done
