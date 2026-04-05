# Heltec V4 Meshtastic / LoRa Diagnostics

Diagnostic toolkit for `Heltec V4` nodes that can hear the LoRa mesh but do not
get reliable replies back from neighboring Meshtastic nodes.

This repository includes:

- a raw-radio diagnostic sketch for `Heltec V4`
- a host-side Meshtastic network check script
- saved Meshtastic configs used during recovery
- a local Meshtastic `factory.bin` for restoring the board

Russian instructions:

- [README_RU.md](README_RU.md)

## Use cases

This project is useful when a node:

- hears LoRa traffic
- sees neighbors in Meshtastic
- but fails on `ACK`, `traceroute`, or looks suspicious on transmit

There are two main workflows:

1. Flash the diagnostic sketch and test the RF path directly.
2. Restore Meshtastic and run real network checks against nearby nodes.

## Requirements

- Windows
- Python `3.10+`
- Git
- PlatformIO Core
- USB access to the board

Python dependencies:

```powershell
python -m pip install -r requirements.txt
```

Install PlatformIO Core:

```powershell
python -m pip install platformio
```

## Repository layout

- `src/main.cpp`
  Raw LoRa diagnostic sketch for direct SX1262 / RF-path testing.
- `tools/meshtastic_diag.py`
  Host-side script for `ACK`, `traceroute`, and control broadcast checks
  against ONEmesh.
- `meshtastic-backup-2026-04-05.yaml`
  Config backup exported before changes.
- `meshtastic-onemesh-yar-2026-04-05.yaml`
  Earlier OneMesh config used during testing.
- `meshtastic-onemesh-yar-current-2026-04-05.yaml`
  Example Meshtastic config for `RU / MEDIUM_FAST`.
- `firmware_restore/firmware-heltec-v4-2.7.18.fb3bf78.factory.bin`
  Local factory image used to restore the board.
- `docs/DIAGNOSTICS.md`
  Summary of checks and findings.
- `docs/GITHUB_METADATA.md`
  Suggested GitHub description, About text, and topics.

## Flashing the diagnostic sketch

1. Connect the board over USB.
2. Check the serial port, for example `COM4`.
3. If needed, update `platformio.ini`.
4. Build:

```powershell
python -m platformio run
```

5. Upload:

```powershell
python -m platformio run -t upload
```

6. Open serial monitor:

```powershell
python -m platformio device monitor
```

## Diagnostic sketch commands

- `1` / `2` / `3` - select profile
- `c` - CAD scan all profiles
- `r` - RX on active profile
- `a` - RX on all profiles
- `g` - boosted RX on active profile
- `s` - sync-word sweep (`0x12` and `0x34`)
- `t` - send one packet
- `b` - send TX burst
- `p` - TX power sweep
- `w` - 8-second continuous carrier
- `x` - full diagnostic cycle
- `i` - show current status
- `h` - help

Most useful commands:

- `c` to confirm LoRa activity on the expected frequencies
- `p` when you have an SDR, tinySA, or a second LoRa board nearby
- `w` when you want the cleanest proof that the board is radiating

## Meshtastic network checks

When the board is already running Meshtastic:

```powershell
python tools\meshtastic_diag.py --port COM4 --root-topic msh/RU/YAR
```

The script:

- connects to the node
- selects a suitable neighbor
- sends a message with `ACK`
- runs `traceroute`
- sends a control broadcast
- checks whether ONEmesh sees that broadcast

## Restoring Meshtastic

Flash the factory image:

```powershell
python C:\Users\Admin\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --port COM4 --baud 921600 write_flash 0x0 firmware_restore\firmware-heltec-v4-2.7.18.fb3bf78.factory.bin
```

Apply the config:

```powershell
python -m meshtastic --port COM4 --configure meshtastic-onemesh-yar-current-2026-04-05.yaml
```

Verify:

```powershell
python -m meshtastic --port COM4 --info
```

## Notes

- `onemesh-map.html`, `onemesh-mqtt.html`, and `onemesh-v2.1.7.min.js` are
  local snapshots saved during troubleshooting.
- If you continue investigating the hardware, the strongest next test is to
  observe `CW` or `power sweep` on an SDR, tinySA, or a second LoRa node.
