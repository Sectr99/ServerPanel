# ServerPanel setup

Homelab dashboard for the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (7" 1024×600 touch). Five pages: desktop, NAS, storage, Docker containers and a Bambu Lab 3D printer, with 15/30/60-minute history graphs. Swipe left/right to change pages; tap a gauge, disk or container for a full-screen detail graph.

Besides the server stats, the panel can be a USB macro pad, a Stream Deck surface for Bitfocus Companion, a weather radar, a Spotify controller, or a launcher that opens any of them. The **Function** setting picks what each panel shows at start-up and on waking.

## 1. Desktop (Windows): LibreHardwareMonitor
1. Install LibreHardwareMonitor and run it as admin.
2. Options → **Run on Windows startup**, **Start minimized**, **Minimize to tray**.
3. Options → **Remote Web Server** → **Run** (port 8085). Check it's still ticked after a reboot.
4. Allow the port through the firewall (admin PowerShell):
   ```powershell
   New-NetFirewallRule -DisplayName "LibreHardwareMonitor" -Direction Inbound -Protocol TCP -LocalPort 8085 -Profile Private -Action Allow
   ```
5. List your sensor IDs, then paste the right ones into `src/config.h`:
   ```powershell
   function walk($n){ if($n.SensorId){ "{0,-38} {1,-26} {2}" -f $n.SensorId,$n.Text,$n.Value }; $n.Children | % { walk $_ } }; walk (irm http://localhost:8085/data.json)
   ```

## 2. NAS (Unraid)
- **Glances**: Apps → search "Glances" → install with **Network Type: Host**. Check `http://NAS_IP:61208/api/4/quicklook` returns JSON.
  - Find your CPU temp label with `http://NAS_IP:61208/api/4/sensors` and set `NAS_CPU_TEMP_LABEL` to it.
  - The Docker page needs `/var/run/docker.sock` mounted in the Glances container (the Unraid template does this).
  - GPU stats (NVIDIA): run Glances from `nicolargo/glances:ubuntu-latest-full` (the Alpine image can't load NVIDIA's glibc libraries) with `--runtime=nvidia`, `NVIDIA_VISIBLE_DEVICES=all` and `NVIDIA_DRIVER_CAPABILITIES=all`, plus the Nvidia Driver plugin on Unraid. Check `http://NAS_IP:61208/api/4/gpu` isn't `[]`.
- **Storage page (Unraid API, built into 7.2+, otherwise install the Unraid Connect plugin)**: Settings → Management Access → API Keys → create a key with read access to the array, then paste it into `UNRAID_API_KEY`. Disk temps come from Unraid's cache, so spun-down disks stay asleep.

## 3D printer (optional): Bambu Lab P1 series
The printer page is built around the printer's camera (tap it for full screen), with progress, layers, time left and finish time underneath and nozzle/bed temperatures, fans, speed, filament and errors beside it. It reads the printer's local MQTT broker (TLS, port 8883) and its camera stream (TLS, port 6000). No cloud account or extra software is needed. The camera only streams while the page is on screen.
1. On the printer: Settings → WLAN/Network shows its IP and **Access Code**; Settings → Device shows its serial number.
2. For the camera, turn on **LAN Only Liveview** in the printer's network settings.
3. Put them in `PRINTER_IP`, `PRINTER_CODE` and `PRINTER_SN` in `src/config.h`, and give the printer a DHCP reservation. Leave `PRINTER_IP` empty to turn the page's connection off.

Each panel opens its own connection to the printer. If connections keep dropping, the printer may be limiting how many local MQTT clients it accepts at once; try closing other local connections (another panel, the Elecrow screen, a Home Assistant integration).

## Weather radar
A launcher app (also available as a panel Function) that loops the past hour of NOAA radar over a dark map, with Local / Regional / Wide zoom. It needs no accounts or keys:
- Radar: NOAA's MRMS base reflectivity mosaic ([NCEP OpenGeo](https://opengeo.ncep.noaa.gov/)), US only.
- Map: Esri's World Dark Gray canvas (Esri, HERE, Garmin, © OpenStreetMap contributors).
- Location: tap the place name on the map to type a US ZIP code; it's looked up at [Zippopotam.us](https://api.zippopotam.us) and saved on the panel. `RADAR_ZIP` and friends in `src/config.h` are the first-boot default.

It only downloads while it's on screen.

## Spotify (optional)
A launcher app (also available as a panel Function) that shows what's playing on your Spotify account, on any device, with album art. Its controls are play/pause, previous/next, shuffle, repeat, Liked Songs, tap-to-seek and volume. It uses the Spotify Web API, which needs your own (free) developer app, and that app's owner needs **Spotify Premium**.
1. At [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard), create an app. Give it any name and description, set the Redirect URI to `http://127.0.0.1:8888/callback`, and tick **Web API**.
2. Run `python tools/spotify_auth.py` and paste the app's Client ID and Client secret when it asks. Log in in the browser tab it opens. The script saves `SPOTIFY_CLIENT_ID`, `SPOTIFY_CLIENT_SECRET` and `SPOTIFY_REFRESH` into `src/config.h`.
3. Build and flash. Every panel can share the same values.

It only polls Spotify while it's on screen. Some devices, most phones among them, don't let the Web API set their volume; the volume bar then shows "fixed". Track names use the panel's font, which only has plain ASCII characters, so accents and other scripts don't show.

## Stream Deck (optional): Bitfocus Companion
The Stream Deck Function makes the panel a 5×3 button surface for [Bitfocus Companion](https://bitfocus.io/companion), which draws the keys and runs the actions.
1. Install Companion 4.x on the PC and start it. Its Satellite API listens on TCP 16622 by default.
2. Allow that port through the firewall (admin PowerShell):
   ```powershell
   New-NetFirewallRule -DisplayName "Companion Satellite" -Direction Inbound -Protocol TCP -LocalPort 16622 -Profile Private -Action Allow
   ```
3. Set `COMPANION_HOST` in `src/config.h` (it defaults to `PC_HOST`) and pick **Stream Deck** as the panel's Function in settings, or open it from the launcher.
4. The panel appears in Companion's **Surfaces** tab as "ServerPanel"; assign it a page there.

## Macro pad (optional)
The Macro Pad Function makes the board a USB keyboard with a 4×3 grid of keys (media keys, Windows shortcuts, F13–F24). Connect the board's **USB-A** port to the PC with a USB-A to USB-A cable. Edit the keys in `MACRO_KEYS` in `src/config.h`.

## 3. Flash the panel
1. Copy `src/config.example.h` to `src/config.h` and fill it in: Wi-Fi (2.4 GHz), IPs, sensor IDs, API key, printer. Copy `ota.example.ini` to `ota.ini` and set an OTA password. Both copies are git-ignored, so your secrets stay out of the repository. Give the PC and NAS DHCP reservations.
2. Plug into the board's **UART** USB-C port (CH343 serial adapter; the log goes there), then build and flash:
   ```
   pio run -e p4 -t upload --upload-port COMx
   pio device monitor -p COMx
   ```
   The first build takes a few minutes; later ones about a minute. Keep the project in a path without spaces.
3. After that, flash over Wi-Fi: reserve an IP for the panel in the router, set it and the OTA password in `platformio.ini` under `[ota]`, then run `pio run -e p4-ota -t upload`. Changes to `partitions.csv` still need USB.

## Settings
Tap the gear in the header for Function, brightness, screen sleep, auto-rotate, start page (any of the five), graph window (15/30/60 min), 12/24 h clock and night mode, plus a status view of Wi-Fi and every data source. Changes are saved on the board. The matching values in `src/config.h` are only first-boot defaults.

Only in `src/config.h`:
- `TOUCH_FLIP_X` / `TOUCH_FLIP_Y`: this board's touch panel is mounted 180° from the display, so both are `1`.
- `THEME_DAY` / `THEME_NIGHT`: palette brightness for day and night mode.
- Wi-Fi, IP addresses, sensor IDs and the Unraid API key (deliberately not editable on the panel).
- The macro pad keys (`MACRO_KEYS`).
- Color thresholds: the `sev(value, warn, crit, color)` calls in `src/ui.cpp`.

## Preview on the PC
`tools/preview.cpp` renders every page, the detail views, settings, launcher and macro pad with sample data to `preview*.png`, using the same `src/ui.cpp` as the firmware. Build it with zig and the SDL2 MinGW dev zip (LovyanGFX's desktop backend needs the SDL headers); it uses the LovyanGFX copy from `.pio/libdeps/p4`, so build the firmware once first:
```
ZIG=path/to/zig.exe sh tools/build-preview.sh path/to/SDL2-2.32.10/x86_64-w64-mingw32
```
Add `samples` as a second argument to render every screen in five data scenarios (normal, hot, idle, offline, night) to `samples/<scenario>/`.
