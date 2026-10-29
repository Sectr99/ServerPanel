# microSD card plan

Status (2026-10-28): the card works on panel D (8 GB SDHC). The variant's defaults (slot 0, IO_MUX pins, LDO 4, power pin 45) mount it in 244 ms after Wi-Fi starts, with Wi-Fi unaffected; 1 MB wrote in 95 ms and read back intact in 128 ms (with checking). The firmware has a boot-time probe that logs the card and its root (`sdProbe` in main.cpp). Was: plan only (2026-10-27). Japanese text (use 2) is built: `python tools/mkfonts.py sd` writes `sd/fonts/<role>.vlw` (36 MB: accented Latin from Inter, JIS kana and level 1+2 kanji from Noto Sans JP) to copy to the card's root; at boot `uiLoadExtraFonts("/sdcard/fonts")` puts each behind its Inter role (`FallbackFont` in ui.cpp, glyphs read from the card as drawn). Text truncation and wrapping are UTF-8 safe; Japanese wraps between characters. Copy them with `python tools/sd_upload.py <ip>` (Wi-Fi upload to the panel's `/sd` endpoint in main.cpp, digest auth with the OTA password; 1 MB pieces, each received into PSRAM and then appended to `<path>.part` while the sender waits, renamed after the last; the camera, radar and Spotify streams stay off meanwhile; the panel reboots afterwards). Streaming straight to the card wedged panel B's Wi-Fi link (its card is slower), and holding whole files in PSRAM ran B out of it on the 15 MB font. The card runs at 40 MHz (`SDMMC_FREQ_HIGHSPEED`), the Wi-Fi slot's speed: the slots share one host clock divider that the driver switches per transaction, and at 20 MHz the card timed out under Wi-Fi traffic. Without a card (tested 2026-10-28): the mount gives up in 250 ms, Wi-Fi is unaffected, text draws from Inter alone (Japanese as boxes), and the `/sd` endpoints answer 503 "no SD card" before buffering anything. If a glyph read ever comes up short (card pulled while running; not tested), every role drops back to Inter at once (`StdioFile::failed` in ui.cpp) instead of going to the card for each glyph. Nothing here is built. When work starts, begin with the foundation, then go in priority order. Record progress in [STATUS.md](STATUS.md).

## Why the card helps
- **Space:** the app slot is 6 MB, with 1.8 MB used. A card holds gigabytes.
- **Writes:** writing to the board's own flash stalls the PSRAM cache and glitches the display, which is why settings saves are debounced and Wi-Fi doesn't persist. Card writes go over the SD controller instead, so logging, caching and recording become possible.
- **Reboots:** history, caches and logs survive reboots and OTA updates.
- **Editing on a PC:** files on the card can be changed without rebuilding the firmware.

## Foundation (needed by everything below)
- **`src/sdcard.cpp`:** mounts the card at boot with `SD_MMC` in 4-bit mode.
  - The framework's ESP32-P4 variant already sets slot 0 (IO_MUX pins), on-chip LDO channel 4 and power pin 45 (`variants/esp32p4/pins_arduino.h`).
  - Check against Waveshare's 7B schematic that the board is wired the same way.
- **Without a card:** every feature degrades to today's behaviour. Nothing depends on the card to boot.
- **Status:** an "SD card" row on the settings page shows the size and space used, or "none".
- **Layout:** one top-level folder per user. `/fonts`, `/history`, `/printer`, `/radar`, `/spotify`, `/logs`, `/macro`, `/screenshots`, `/update`.
- **Logging:** the boot log reports mount time, card size and read speed.

**To verify on the first card:**
- **Coexistence with Wi-Fi:** the Wi-Fi co-processor (ESP32-C6) uses the same SD host controller on slot 1. Both slots are supported, but heavy card I/O may delay Wi-Fi traffic. Measure camera fps and fetch timing while writing.
- **Internal RAM:** FATFS and the SD driver take some internal RAM (each open file and the volume work buffer). Check the "RAM free / lowest" lines before and after mounting, and keep the number of files held open small.
- **Write timing:** writes should come from background tasks at idle priority, never from `loop()` mid-frame.

## Uses, by priority

### 1. Crash and event log (all panels): high value, small effort
- **What:** append the serial log to `/logs/YYYY-MM-DD.txt`, with a few days kept, plus each core dump as a file after a crash reboot.
- **Why:** the open investigations (the camera watchdog, the 27 KB RAM low) need long captures. Today that means a panel on USB and a PC recording. With the card, every panel records all the time, and the logs can be read later, even from panels mounted in the enclosure.
- **How:**
  - Add a `Print` sink beside `Serial` that buffers lines in PSRAM and flushes every few seconds from a background task.
  - At boot, copy the `coredump` partition to `/logs/` if it holds a new dump.
  - Settings shows the latest crash time.

### 2. Japanese (and accented) text: high value, small to medium effort
- **What:** Noto Sans JP glyphs, made by `tools/mkfonts.py` as VLW files in `/fonts/`, one per text size.
  - **Glyphs:** kana plus the standard JIS level 1 and 2 kanji (6,355), about 11 MB.
  - **Latin:** Latin-1 and Latin Extended-A accents for Inter come along in the same pass.
- **How:**
  - At boot, load the files into PSRAM. That's about 11 MB of the 32 MB, at roughly 1 s.
  - A small fallback `IFont` wrapper (LovyanGFX's font interface is four methods) draws each character from Inter when Inter has it, and from the Japanese font otherwise.
  - The preview reads the same files from a folder.
- **Benefits:** Spotify titles, artists and albums; printer job names; container names. Anything showing outside text.

### 3. Graph history that survives reboots, and longer windows: high value, medium effort
- **What:**
  - Snapshot the history ring buffers (`Hist` in ui.cpp) to `/history/` every minute, and restore them at boot. An OTA update no longer wipes the graphs.
  - Keep a downsampled long-term series: 1 point per minute for 7 days, about 5 MB.
  - Add 6 h, 24 h and 7 d choices to the graph window setting. The `Settings.graphMin` options can grow without breaking the saved-settings rule (append only).
- **Optional:** a daily CSV of all readings, for analysis on the PC.
- **Watch:** history is in ui.cpp, which has no Arduino code. Keep the file I/O in main.cpp and pass buffers in and out through new `ui*` functions.

### 4. Printer timelapse and print history: high value, medium effort
- **Timelapse:**
  - **Recording:** during a print, save a camera JPEG to `/printer/<job>/` once per layer, from `layer_num` in the MQTT report. The frames already arrive as JPEGs, so saving needs no decoding. A 300-layer print at ~60 KB per frame is about 18 MB.
  - **Camera use:** the camera must stream for the whole print, not only while the page shows. That's one TLS connection for hours, so it competes with the radar and Spotify for the RAM budget. Record only while no other TLS app is open, or settle for one frame every N minutes.
  - **Viewing:** play it back on the panel (a "Timelapse" view from the printer page, decoded at half size like the live camera), or copy it off the card.
- **Print history:** append each job (name, start, end, duration, result, filament) to `/printer/history.csv`, and show the last 20 in a detail view.
- **Error snapshot:** save the frame when an error or `FAILED` appears.

### 5. Weather radar cache: medium value, small effort
- **What:** cache the Esri base and label tiles in `/radar/tiles/z/x/y`, refreshed monthly. Cache the last fetched loop too.
- **Why:**
  - The map layers are rebuilt on every boot, place change and zoom. That's about 40 HTTPS tile fetches, and TLS handshakes are what drive the internal RAM peaks.
  - From the cache, the map appears instantly and the radar connection only handles NOAA and NWS.
  - The last loop shows immediately on reopening while a fresh one loads.
- **Optional:** a longer loop (3 h) from saved frames; a temperature graph from saved observations.

### 6. Spotify album art cache: low to medium value, small effort
- **What:** keep album JPEGs by image ID in `/spotify/art/`, oldest deleted past ~200 MB.
- **Why:** a replay or album shows its art without waiting for a fetch. The fetch is plain HTTP, so it saves time but not RAM.

### 7. Macro pad keys from the card: medium value, small effort
- **What:** `/macro/keys.json` replaces the compiled-in `MACRO_KEYS` when present, with an optional PNG icon per key (`/macro/<n>.png`). Keys are edited on the PC, and the change takes effect after a reboot or reinsertion.
- **Optional:** several pages of keys, switched by swiping.

### 8. Screenshots: low value, tiny effort
- **What:** a settings button that saves the screen as a PNG in `/screenshots/`, using the same `createPng` call the preview uses. Good for docs and bug reports.

### 9. Firmware recovery from the card: medium value, small effort
- **What:** at boot, if `/update/firmware.bin` exists, flash it (Arduino `Update`), then rename it to `.done`.
- **Why:** a panel in the enclosure that can't take OTA (a bad build, a lost Wi-Fi config) can be recovered without opening the case and plugging in USB.
- **Safety:** check the image's header and size before writing, and only try it once.

### 10. Ideas, not planned
- **Photo frame app:** a slideshow from `/photos/` as a screensaver or launcher app. The launcher is full, so this needs a new layout first.
- **Per-panel config on the card:** a `config.json` overriding config.h values (Wi-Fi, IPs, keys), so one firmware build suits every panel. The catch: secrets in plain text on a removable card. Decide before building.

## Order of work
1. Foundation, then the crash and event log. This proves the card works alongside Wi-Fi and pays off on the open investigations at once.
2. Japanese text.
3. Graph history.
4. Printer timelapse and history.
5. The small ones (radar cache, album art cache, macro keys, screenshots, recovery) in any order.
