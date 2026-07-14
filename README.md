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
| Raspberry Pi 5 (4 GB+) | recommended, with the official Active Cooler |
| Raspberry Pi 4 (4 GB+) | supported — installer auto-uses a larger JACK buffer (256) for headroom |
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
- **Dial in a tone**: pick a model + IR, tweak level/EQ/gate — **every dial
  is saved per preset** and autosaves to the active slot (a couple of seconds
  behind you, always flushed before a preset switch). SAVE forces it
  immediately. Set `"autosavePresets": false` for classic
  tweak-freely-save-explicitly behaviour.
- **Preset names follow the capture**: load a capture into a slot and the
  nickname becomes the capture's filename; swap the capture and the name
  updates too. Long-press a slot to type a custom name — that pins it
  (`customName` in the preset JSON), so swapping captures won't rename it.
- **Reorder presets**: long-press a slot and use **◀ MOVE / MOVE ▶** to swap
  it with the neighbouring slot (its whole state — capture, IR, dials, name —
  moves with it). Or edit the `NN-` prefix of the files in
  `/var/lib/gatewaypi/presets/` over SSH/SFTP.
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

- **Add captures/IRs**: browse to `http://<hostname>.local:8080` (or the
  Pi's IP, `:8080`) from your phone/Mac and upload TONE3000 downloads —
  single files *or* whole folders (subfolders are preserved). Or plug in a
  USB stick (`.nam` files anywhere; IR `.wav`s in a folder named `GatewayPi`
  or containing `IR`). Files land in `~/Captures` and `~/IRs` — plain folders
  in your home directory, next to `Desktop`, so you can also manage them in
  the file manager or over SSH/SFTP.
- **AUDIO button**: in-window panel showing the live input/output channels
  and latency. Audio runs through **JACK** (jackd owns the interface and
  clocks full-duplex cleanly — raw ALSA drifts and warbles on some USB
  interfaces); buffer/rate are set by the jackd service, not the app.
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
- **No audio / wrong device**: tap AUDIO on-screen and pick your interface;
  the panel shows whether the input is live. Or set `audioDeviceMatch` in
  config.json to a substring of the interface name.
- **No input signal**: the installer masks PipeWire/PulseAudio so they can't
  grab the USB interface; if you re-enabled desktop audio, the amp and the
  desktop will fight over the device.
- **Crackle / warble / FM-like wobble**: audio runs through JACK precisely
  to avoid this (raw ALSA full-duplex drifts on some USB interfaces). Check
  jackd is up: `systemctl status gatewaypi-jack` and `jack_lsp -c` (you
  should see `system:capture_1 -> NAMix:in_1` and `NAMix:out_1/2 ->
  system:playback_1/2`). If jackd won't start, confirm the ALSA card id in
  its unit (`-dhw:<id>`, from `cat /proc/asound/cards`) matches your
  interface. Raise jackd's period (`-p256`) if it logs xruns under load.
- **Note on power**: a crackle ALSO present in a direct
  `aplay -D hw:CARD=<iface>,DEV=0` test (bypassing app + JACK) is hardware/
  power — use the official 27W/5A Pi 5 supply or a powered USB hub.
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
system/                 systemd units (incl. jackd), udev rule, RT limits, BLE scripts
webui/upload_server.py  phone upload page (Python stdlib only)
config/                 default config.json + 4 empty preset slots
```

## Licence

GPL v3 (inherited from gateway-linux). NAM core and AudioDSPTools are MIT,
JUCE under its own licence terms.
