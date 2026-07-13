#!/bin/bash
# GatewayPi installer — turns a fresh Raspberry Pi OS Lite (64-bit, Bookworm+)
# into a standalone NAM A2 guitar amp appliance.
#
#   sudo ./install.sh
#
# What it does:
#   1. Installs build/runtime packages (JUCE deps, cage kiosk, bluez).
#   2. Clones gateway-linux (NAMix) at a pinned commit, applies the GatewayPi
#      patch, adds the appliance sources, and builds the standalone app.
#   3. Installs the app to /opt/gatewaypi and the library to /var/lib/gatewaypi.
#   4. Installs and enables systemd units: kiosk (cage), CPU tuning, BLE-MIDI
#      reconnect, web upload page, USB import.
#   5. Applies realtime audio limits and adds `threadirqs` to the kernel
#      command line.
#
# Re-running is safe: the build refreshes, config/presets are never overwritten.
set -euo pipefail

UPSTREAM_REPO="https://github.com/rations/gateway-linux.git"
UPSTREAM_COMMIT="ffeb0d3e69818510e71b1c3f76b6a14aa1596683"

BUNDLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC=/opt/gatewaypi/src
BIN=/opt/gatewaypi/bin
DATA=/var/lib/gatewaypi

if [ "$(id -u)" -ne 0 ]; then
  echo "Run with sudo: sudo ./install.sh" >&2
  exit 1
fi

# The user the kiosk runs as: whoever invoked sudo, falling back to 'pi'.
APP_USER="${SUDO_USER:-pi}"
if ! id "$APP_USER" > /dev/null 2>&1; then
  echo "User '$APP_USER' not found — pass one via: sudo APP_USER=<name> ./install.sh" >&2
  exit 1
fi
echo "==> Installing GatewayPi for user: $APP_USER"

# ---------------------------------------------------------------------------
echo "==> 1/5 Installing packages"
# ---------------------------------------------------------------------------
export DEBIAN_FRONTEND=noninteractive
apt-get update
PKGS=(
  git build-essential cmake pkg-config
  libasound2-dev
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev
  libxcomposite-dev libxrender-dev
  libfontconfig1-dev
  libgl1-mesa-dev libglu1-mesa-dev mesa-common-dev
  cage xwayland
  python3 bluez alsa-utils
)
# freetype dev package name differs across Debian releases
if apt-cache show libfreetype-dev > /dev/null 2>&1; then
  PKGS+=(libfreetype-dev)
else
  PKGS+=(libfreetype6-dev)
fi
apt-get install -y "${PKGS[@]}"

# ---------------------------------------------------------------------------
echo "==> 2/5 Fetching and building the app (this takes a while on first run)"
# ---------------------------------------------------------------------------
mkdir -p "$SRC" "$BIN"
if [ ! -d "$SRC/.git" ]; then
  git clone "$UPSTREAM_REPO" "$SRC"
fi
cd "$SRC"
git fetch origin
git checkout -f "$UPSTREAM_COMMIT"
git submodule update --init --recursive --depth 1 || git submodule update --init --recursive

# Apply the GatewayPi patch (skip when already applied on a re-run).
if git apply --check "$BUNDLE_DIR/patches/gatewaypi.patch" 2>/dev/null; then
  git apply "$BUNDLE_DIR/patches/gatewaypi.patch"
  echo "    patch applied"
else
  echo "    patch already applied (or tree dirty from a previous run) — continuing"
fi

# Appliance sources (always refreshed from the bundle).
rm -rf src/gatewaypi
cp -R "$BUNDLE_DIR/src/gatewaypi" src/

cmake -B build -DCMAKE_BUILD_TYPE=Release -DGATEWAYPI=ON
cmake --build build --target NAMixLinux_Standalone -j"$(nproc)"

install -m 755 build/NAMixLinux_artefacts/Release/Standalone/NAMix "$BIN/gatewaypi"
echo "    installed $BIN/gatewaypi"

# ---------------------------------------------------------------------------
echo "==> 3/5 Library and helper scripts"
# ---------------------------------------------------------------------------
mkdir -p "$DATA"/{models,irs,presets}
[ -f "$DATA/config.json" ] || cp "$BUNDLE_DIR/config/config.json" "$DATA/"
for p in "$BUNDLE_DIR"/config/presets/*.json; do
  [ -f "$DATA/presets/$(basename "$p")" ] || cp "$p" "$DATA/presets/"
done
chown -R "$APP_USER:$APP_USER" "$DATA"

install -m 755 "$BUNDLE_DIR/system/ble-midi-connect.sh" "$BIN/"
install -m 755 "$BUNDLE_DIR/system/ble-pair.sh" "$BIN/"
install -m 755 "$BUNDLE_DIR/system/usb-import.sh" "$BIN/"
install -m 755 "$BUNDLE_DIR/webui/upload_server.py" "$BIN/"

# ---------------------------------------------------------------------------
echo "==> 4/5 System integration"
# ---------------------------------------------------------------------------
usermod -aG audio,video,render,input "$APP_USER" 2>/dev/null \
  || usermod -aG audio,video "$APP_USER"

install -m 644 "$BUNDLE_DIR/system/95-gatewaypi-audio.conf" /etc/security/limits.d/

sed "s/@USER@/$APP_USER/g" "$BUNDLE_DIR/system/gatewaypi-sudoers" > /etc/sudoers.d/gatewaypi
chmod 0440 /etc/sudoers.d/gatewaypi
visudo -cf /etc/sudoers.d/gatewaypi > /dev/null

for unit in gatewaypi-kiosk gatewaypi-tuning gatewaypi-blemidi gatewaypi-webui; do
  sed "s/@USER@/$APP_USER/g" "$BUNDLE_DIR/system/$unit.service" \
    > "/etc/systemd/system/$unit.service"
done
install -m 644 "$BUNDLE_DIR/system/gatewaypi-usb-import@.service" /etc/systemd/system/
install -m 644 "$BUNDLE_DIR/system/99-gatewaypi-usb-import.rules" /etc/udev/rules.d/

# threadirqs: lets IRQ handlers be scheduled, reducing audio xruns.
CMDLINE=/boot/firmware/cmdline.txt
[ -f "$CMDLINE" ] || CMDLINE=/boot/cmdline.txt
if [ -f "$CMDLINE" ] && ! grep -q threadirqs "$CMDLINE"; then
  sed -i '1 s/$/ threadirqs/' "$CMDLINE"
  echo "    added threadirqs to $CMDLINE"
fi

# ---------------------------------------------------------------------------
echo "==> 5/5 Enabling services"
# ---------------------------------------------------------------------------
systemctl daemon-reload
udevadm control --reload
systemctl disable getty@tty1.service > /dev/null 2>&1 || true

# On a desktop image (full Raspberry Pi OS) the display manager owns the
# screen at boot and the kiosk can't take the seat.  Disable it — the amp
# owns the screen now.  Deliberately not stopped here in case this install
# is running inside that desktop session; it stays up until reboot.
if [ -L /etc/systemd/system/display-manager.service ]; then
  DM=$(basename "$(readlink -f /etc/systemd/system/display-manager.service)")
  systemctl disable "$DM" > /dev/null 2>&1 || true
  echo "    desktop session ($DM) disabled so the amp owns the screen"
  echo "    (restore the desktop anytime: sudo systemctl enable $DM && sudo reboot)"
fi

systemctl enable gatewaypi-kiosk gatewaypi-tuning gatewaypi-blemidi gatewaypi-webui
systemctl set-default graphical.target > /dev/null

cat << EOF

GatewayPi installed.

  Start now:        sudo systemctl start gatewaypi-kiosk
  (or just reboot — everything starts automatically)

  Library:          $DATA/{models,irs,presets}
  Web upload:       http://$(hostname).local:8080  (from your phone/Mac)
  USB import:       plug in a stick with .nam files (IR .wavs in a folder
                    named 'GatewayPi' or containing 'IR')
  Pair the pedal:   sudo $BIN/ble-pair.sh   (USB needs no setup at all)
  Config:           $DATA/config.json  (MIDI CCs, audio device match,
                    buffer size — restart the kiosk after editing)

Chocolate setup (once, in the M-Vave CubeSuite app):
  A/B/C/D tap  -> PC 0/1/2/3
  A/D hold     -> CC 80/81 (bank down/up),  B/C hold -> CC 82/83 (bypass/mute)
EOF
