# Heltec V4 Meshtastic / LoRa Diagnostics

Temporary workspace for diagnosing a `Heltec V4` node that could hear the local
LoRa mesh but was not getting reliable replies back from neighboring nodes.

This repository contains:

- a raw-radio diagnostic sketch for `Heltec V4`
- a host-side Meshtastic verification script
- saved Meshtastic configs used during recovery
- a local `factory.bin` used to restore the board after testing

## Current board state

As of `2026-04-05`, the board was returned to Meshtastic and configured like this:

- firmware: `2.7.18.fb3bf78`
- region: `RU`
- preset: `MEDIUM_FAST`
- hop limit: `7`
- `configOkToMqtt = true`
- `module_config.mqtt.enabled = false`
- MQTT root kept as `msh/RU/YAR` for compatibility with local OneMesh settings

The config file for this restored state is:

- `meshtastic-onemesh-yar-current-2026-04-05.yaml`

## Repository layout

- `src/main.cpp`
  Raw LoRa diagnostic sketch for direct RF-path testing.
- `tools/meshtastic_diag.py`
  Host-side script that runs Meshtastic checks such as ACK, traceroute, and
  control broadcast verification against ONEmesh.
- `meshtastic-backup-2026-04-05.yaml`
  Backup exported before changes.
- `meshtastic-onemesh-yar-2026-04-05.yaml`
  Earlier OneMesh config used during active tests.
- `meshtastic-onemesh-yar-current-2026-04-05.yaml`
  Current restored config with MQTT uplink disabled on the node.
- `firmware_restore/firmware-heltec-v4-2.7.18.fb3bf78.factory.bin`
  Local Meshtastic factory image used to restore the board.
- `docs/DIAGNOSTICS.md`
  Short write-up of what was tested and what the results suggest.

## Raw diagnostic sketch

The sketch in `src/main.cpp` is not meant to join Meshtastic. It is a bench
tool for checking the SX1262 radio path directly.

Features:

- CAD scan on likely `RU / MEDIUM_FAST` frequencies
- RX windows with `RSSI`, `SNR`, and frequency error
- boosted RX mode with CRC disabled
- sync-word sweep (`0x12` and `0x34`)
- single TX packet
- TX burst
- TX power sweep
- continuous carrier (`CW`) for SDR / tinySA testing
- explicit external FEM switching between RX and TX

Serial commands:

- `1` / `2` / `3`: select profile
- `c`: CAD scan all profiles
- `r`: RX on active profile
- `a`: RX on all profiles
- `g`: boosted RX on active profile
- `s`: sync-word sweep on active profile
- `t`: send one packet
- `b`: send TX burst
- `p`: TX power sweep
- `w`: 8-second continuous carrier
- `x`: full diagnostic cycle
- `i`: show current status
- `h`: help

Build and flash:

```powershell
python -m platformio run
python -m platformio run -t upload
```

## Meshtastic diagnostic script

`tools/meshtastic_diag.py` is intended for use when the board is already
running Meshtastic.

What it does:

- connects to the node over serial
- selects a direct or near-direct neighbor
- sends an ACK-requesting message
- runs `traceroute`
- sends a control broadcast
- polls ONEmesh API for that broadcast

Example:

```powershell
python tools\meshtastic_diag.py --port COM4 --root-topic msh/RU/YAR
```

## Restoring Meshtastic

Factory image restore:

```powershell
python C:\Users\Admin\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --port COM4 --baud 921600 write_flash 0x0 firmware_restore\firmware-heltec-v4-2.7.18.fb3bf78.factory.bin
```

Apply the current config:

```powershell
python -m meshtastic --port COM4 --configure meshtastic-onemesh-yar-current-2026-04-05.yaml
```

## Notes

- `onemesh-map.html`, `onemesh-mqtt.html`, and `onemesh-v2.1.7.min.js` are
  local reference snapshots captured during troubleshooting.
- This workspace was focused on proving whether the problem was configuration,
  protocol mismatch, or hardware/RF-path related behavior.
