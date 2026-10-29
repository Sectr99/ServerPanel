---
name: flash
description: Build and flash the ServerPanel firmware to the Waveshare ESP32-P4 board, over Wi-Fi (OTA, the default) or its CH343 UART port, then optionally read the boot log. Use when asked to flash, upload, or deploy firmware to the panel.
---

# Flash the P4 panel

## 0. Default: over Wi-Fi (OTA)
The firmware accepts OTA updates (`platformio.ini` `[ota]`: IP and password; `env:p4-ota`). No cable needed:

```bash
cd /d/Dev/ServerPanel && PYTHONIOENCODING=utf-8 PYTHONUTF8=1 timeout 300   "$USERPROFILE/.platformio/penv/Scripts/pio.exe" run -e p4-ota -t upload 2>&1   | grep -E "Result|Success|rror|SUCCESS|FAILED" | tail -6
```

- **Success looks like** `Result attempt 1: 'OK'` and `[SUCCESS]`. The board reboots into the new firmware, which the OTA slot scheme keeps safe: a failed transfer leaves the old firmware running.
- **Use the IP, not `serverpanel.local`.** `.local` names don't resolve on this network. If the panel's IP changed (no router reservation), read it from the settings page or the boot log ("Wi-Fi up, IP ...") and pass `--upload-port <ip>`.
- **Fall back to USB (steps 1-2)** when the panel is offline, an OTA upload fails, the firmware being flashed might not boot (it then can't take another OTA), or `partitions.csv` changed (the partition table only gets written over USB).
- `p4-ota` has its own build directory, so after editing code both envs rebuild separately; that's expected.

## 1. Find the port (USB)
The board's UART port is a CH343 (VID:PID 1A86:55D3); it has been COM6 and COM10. Another ESP32 may be plugged in too, so look it up rather than assuming:

```bash
"$USERPROFILE/.platformio/penv/Scripts/pio.exe" device list | grep -B1 "CH343"
```

No CH343 listed means the cable is on the board's built-in USB port (303A:1001) or unplugged: ask the user to move it to the UART port.

## 2. Build and flash (USB)
Before you flash, make sure the user isn't building from VS Code at the same time. Two builds running at once clobber `managed_components`.

```bash
cd /d/Dev/ServerPanel && PYTHONIOENCODING=utf-8 PYTHONUTF8=1 timeout 300 \
  "$USERPROFILE/.platformio/penv/Scripts/pio.exe" run -e p4 -t upload --upload-port COMx 2>&1 \
  | grep -E "Wrote|Hash|rror|SUCCESS|FAILED" | tail -12
```

- **Always set the UTF-8 variables.** When output is piped, Windows Python falls back to cp1252. esptool's progress bar then raises `UnicodeEncodeError` and the upload hangs forever without exiting.
- **Keep the `timeout 300`.** A normal run takes about 45 s (an incremental build plus about 20 s of writing). A full rebuild after toolchain or sdkconfig changes can take about 8 minutes; for those, raise the timeout or run it in the background.
- **Success looks like** four "Hash of data verified." lines and `[SUCCESS]`. The board resets itself afterwards.

## 3. If it fails
- **Swap the USB cable first, then confirm with a swap test.** Screen glitching, crashes inside the ROM, garbled serial, watchdog resets, and esptool failing to sync or dropping out partway through can come from a bad cable or from a faulty board. Try a different known-good cable (and USB port). Then plug a known-good panel into the same cable and port: if it connects and the suspect board doesn't, the board is faulty. Don't take one good run after a swap as a fix: a faulty board can work for a while.
- **`Connecting....` then a failure:** the port is held by something else, such as a VS Code serial monitor. Ask the user to close it.
- **To check the board responds**, run `esptool.exe --chip esp32p4 --port COMx chip-id` (in `penv/Scripts`).
- **Stuck process from an earlier run:** look for `esptool.exe` or `pio.exe` with `Get-CimInstance Win32_Process` and stop it.

## 4. Optional: read the boot log
Use `tools/serial_log.py`: it finds the CH343 port and opens it with DTR/RTS low (either asserted reboots the board).

```bash
"$USERPROFILE/.platformio/penv/Scripts/python.exe" tools/serial_log.py --reset --seconds 20
```

Drop `--reset` to listen without rebooting; add `--no-touch` to hide gesture lines; run long captures in the background.

Healthy boot: Wi-Fi connects through the C6, then fetch logs for the PC and NAS appear. A failed version RPC to the C6 is harmless, because its firmware is older than the host's.
