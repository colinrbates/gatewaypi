#!/bin/bash
# GatewayPi uninstaller.  Leaves /var/lib/gatewaypi (models/IRs/presets) alone
# unless you pass --purge.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Run with sudo: sudo ./uninstall.sh [--purge]" >&2
  exit 1
fi

systemctl disable --now gatewaypi-kiosk gatewaypi-tuning gatewaypi-blemidi \
  gatewaypi-webui 2>/dev/null || true
rm -f /etc/systemd/system/gatewaypi-*.service
rm -f /etc/udev/rules.d/99-gatewaypi-usb-import.rules
rm -f /etc/security/limits.d/95-gatewaypi-audio.conf
rm -f /etc/sudoers.d/gatewaypi
rm -rf /opt/gatewaypi
systemctl daemon-reload
udevadm control --reload
systemctl enable getty@tty1.service 2>/dev/null || true

if [ "${1:-}" = "--purge" ]; then
  rm -rf /var/lib/gatewaypi
  echo "Removed /var/lib/gatewaypi (models, IRs, presets)."
else
  echo "Kept /var/lib/gatewaypi (models, IRs, presets). Use --purge to remove."
fi
echo "GatewayPi uninstalled."
