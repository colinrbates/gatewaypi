# GatewayPi

A standalone guitar amp appliance for the Raspberry Pi 5: **NAM A2 captures +
cabinet IRs** through the Gateway/NAMix plugin experience, fullscreen on a
touchscreen, with an **M-Vave Chocolate** as the foot controller. Power on →
playing in ~25 seconds.

Built on [gateway-linux (NAMix)](https://github.com/rations/gateway-linux), a
JUCE 8 port of the official Gateway plugin (GPL v3, NAM core MIT). GatewayPi
adds an appliance layer: a 4-slot preset bank, MIDI foot control, a kiosk
shell, boot-to-app system integration, and model/IR import.

## Hardware

| Part | Notes |
|---|---|
| Raspberry Pi 5 (4 GB+) | with the official Active Cooler |
| Class-compliant USB audio interface | e.g. PreSonus AudioBox iTwo — instrument input, gain knobs |
| Raspberry Pi Touch Display 2 | 7″ DSI, or any touchscreen |
| M-Vave Chocolate | USB (recommended on stage) or Bluetooth |

## Install

Flash **Raspberry Pi OS Lite (64-bit)** (the full Desktop image also works —
the installer disables the desktop session so the amp owns the screen), boot,
get on the network, then:

```sh
git clone https://github.com/colinrbates/gatewaypi.git && cd gatewaypi
sudo ./install.sh
sudo reboot
```

The first build takes a while on the Pi (~15–30 min); re-runs are fast. The
installer is idempotent — safe to re-run after changing anything.

## Using it

- **Touchscreen** — the Gateway panel (model/IR pickers, gate, EQ, levels)
  with a preset strip above it: 4 slots, bank arrows, BYP / TUNE / SAVE,
  AUDIO (device picker) and OFF (safe shutdown). TUNE mutes the output and
  shows a vintage-style analog needle tuner — ivory dial, damped needle,
  flat/tune/sharp lamps, LCD note window (McLeod pitch method, accurate to
  well under a cent, down to a 5-string's low B).
- **Dial in a tone**: pick a model + IR, tweak — **changes autosave to the
  active slot** (a couple of seconds behind you, and always flushed before a
  preset switch). SAVE forces it immediately. Set `"autosavePresets": false`
  for classic tweak-freely-save-explicitly behaviour.
- **Footswitches — no pedal programming needed**: tap **LEARN**, tap an
  action (Preset 1-4, bank up/down, bypass, tuner), press a footswitch —
  the app binds whatever the pedal sends (PC, CC or note) and remembers it
  in `config.json`. With the Chocolate's factory settings its four switches
  cover the four preset slots; getting distinct hold-messages for the other
  four actions is a pedal-side feature (one CubeSuite visit), or map them
  to a second bank/mode of the pedal. The defaults, if you'd rather program
  the Chocolate to match:

| Switch | Message | Action |
|---|---|---|
| A/B/C/D tap | PC 0–3 | Preset slot 1–4 |
| A hold | CC 80 | Bank down |
| D hold | CC 81 | Bank up |
| B hold | CC 82 | Bypass (dry through) |
| C hold | CC 83 | Tuner (mutes output, overlay on screen) |

- **Add models/IRs**: browse to `http://<hostname>.local:8080` from your
  phone and upload TONE3000 downloads, or plug in a USB stick (`.nam` files
  anywhere; IR `.wav`s in a folder named `GatewayPi` or containing `IR`).
- **Bluetooth pedal**: `sudo /opt/gatewaypi/bin/ble-pair.sh` once; it
  auto-reconnects from then on. USB needs nothing.

## Configuration

`/var/lib/gatewaypi/config.json`:

```json
{
  "midiChannel": 0,            // 0 = respond on any channel
  "ccBankDown": 80, "ccBankUp": 81, "ccBypass": 82, "ccMute": 83,
  "audioDeviceMatch": "iTwo",  // substring of your interface's ALSA name
  "sampleRate": 48000,
  "bufferSize": 128,           // try 64 once stable; 256 if you hear xruns
  "tunerReference": 440,       // A4 reference pitch
  "autosavePresets": true,     // tweaks persist to the active slot
  "midiMap": {},               // written by the LEARN screen
  "bleMidiMac": ""             // set by ble-pair.sh
}
```

Restart after editing: `sudo systemctl restart gatewaypi-kiosk`.

Presets are plain JSON in `/var/lib/gatewaypi/presets/`, named `NN-name.json`
(NN = 01–04 → bank 1, 05–08 → bank 2, …). Edit names freely.

## Troubleshooting

- **Logs**: `journalctl -u gatewaypi-kiosk -f`
- **No audio / wrong device**: tap AUDIO on-screen, or set
  `audioDeviceMatch` to a substring of the name shown there.
- **Crackles/xruns**: raise `bufferSize` to 256; check the interface is on a
  USB 3 port by itself; confirm `threadirqs` is in `/boot/firmware/cmdline.txt`.
- **Boots to the Raspberry Pi desktop instead of the amp**: you're on a
  desktop image and installed with an older installer — run
  `sudo systemctl disable lightdm && sudo reboot` (or re-run `install.sh`,
  which now does this for you). Restore the desktop later with
  `sudo systemctl enable lightdm`.
- **Blank screen**: cage needs XWayland (`sudo apt install xwayland`); check
  `journalctl -u gatewaypi-kiosk`.
- **BLE pedal won't reconnect**: it may be paired to your phone — forget it
  there, rerun `ble-pair.sh`. On stage, use USB.
- **Start over**: `sudo ./uninstall.sh` (keeps your library) or `--purge`.

## Power-loss hardening (optional, recommended once dialed in)

Enable the overlay filesystem so yanking the power cord can't corrupt the SD
card: `sudo raspi-config nonint enable_overlayfs && sudo reboot`. Note this
freezes the whole filesystem including the library — disable it
(`disable_overlayfs`) when you want to add models or save presets, or move
`/var/lib/gatewaypi` to a second writable partition.

## Layout

```
install.sh              one-command installer (idempotent)
uninstall.sh            remover
patches/gatewaypi.patch small patch to upstream (hooks, tuner tap, kiosk editor)
src/gatewaypi/          appliance layer: PresetManager, MidiEngine,
                        KioskShell + PresetBar, TunerEngine + overlay, Config
system/                 systemd units, udev rule, RT limits, BLE scripts
webui/upload_server.py  phone upload page (Python stdlib only)
config/                 default config.json + 4 empty preset slots
```

## Licence

GPL v3 (inherited from gateway-linux). NAM core and AudioDSPTools are MIT,
JUCE under its own licence terms.
