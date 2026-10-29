# ServerPanel design

The stable reference: goals, screens, look, architecture and how to extend them. Current state and open work: [STATUS.md](STATUS.md). Commands and hard rules: [../CLAUDE.md](../CLAUDE.md).

## Goals
- A showpiece "command center" of 7" touch panels under the user's 45" LG ultrawide, plus the server area.
- Each panel runs the same firmware and picks its **Function** in settings (Server Stats, Macro Pad, Launcher, Stream Deck, Weather Radar, Spotify). No fixed left/centre/right roles (the user's explicit choice).
- At a glance: live and recent state of the PC, NAS, containers, 3D printer and local weather. Detail is one tap away, never crowded onto the main view.
- Polish matters: smooth graphs, consistent palette, no glitches, no stale or misleading data (offline states are explicit).
- Lean on resources: internal RAM is the tight budget (see Memory). Download only what's on screen.

## Hardware
Waveshare ESP32-P4-WIFI6-Touch-LCD-7B: P4 rev v3.2 (eco7 ROM), 32 MB PSRAM, 32 MB flash (16 MB used), 1024x600 EK79007 MIPI-DSI, GT911 touch (mounted 180°), ESP32-C6 for Wi-Fi over esp_hosted (SDIO), CH343 USB-UART (log + flashing), USB-A port for the macro pad (TinyUSB HID). Pins and quirks: CLAUDE.md.

## Screens
```
boot/wake -> Function (settings) ─┬─ Server Stats: pages, swipe L/R, dots in header
                                  │    Desktop | NAS | Storage | Docker | Printer
                                  │    tap a gauge/disk/container -> full-screen graph (any tap closes)
                                  │    Printer: tap camera -> full-screen camera
                                  ├─ Macro Pad: 4x3 USB keys
                                  ├─ Stream Deck: 5x3 Companion keys
                                  ├─ Weather Radar: map + loop; place chip -> location sheet (ZIP keypad);
                                  │    alert banner -> alert sheet (tap = next alert)
                                  ├─ Spotify: album art + track, seek bar, transport, like, volume
                                  └─ Launcher: clock + app tiles, 2 wide over 3 (full)
swipe up anywhere -> Launcher. Gear (header, launcher) -> Settings (left: display; right: Function, status, sources).
```
| Page | Content |
|---|---|
| Desktop (LHM) | 5 ring gauges (CPU, CPU temp, GPU, GPU temp, memory) + LOAD and TEMPERATURE graph cards |
| NAS (Glances) | 6 gauges (+ array) + NETWORK (down/up, sqrt axis, own peaks) + TEMPERATURE cards |
| Storage (Unraid API) | array summary + disk rows (temp pill, "asleep" when spun down) |
| Docker (Glances) | container rows (status dot, CPU %, memory), sorted by name |
| Printer (Bambu P1S) | camera 704x396 (error banner on it) + progress strip + sidebar (job, nozzle/bed with targets, fans, speed, filament, error) |
| Weather Radar | 1000x528 map: place chip, conditions card (now + 2 forecast periods or "No NWS alerts"), alert banner, zoom (Local/Regional/Wide), frame time + loop bar, dBZ legend + credits |
| Spotify | Car Thing style, no header or cards: background gradient from the art's dominant hue (darkened so white text reads), art 420x420 with a shadow, album / huge title (2 lines) / artist / "Playing on X" (errors amber); thin full-width progress bar (tap or drag), strip of 7 buttons (like, shuffle, prev, play/pause, next, repeat, volume); volume opens a slider in the strip (close with X) |

## Look
- Dark slate palette `BASE[]` in ui.cpp, drawn as is by day; night mode scales it by `THEME_NIGHT / THEME_DAY`. Each screen has an accent tinting the top of its background (`background()`); layout and components follow [UI_REFRESH.md](UI_REFRESH.md): `statusBar`, `tabStrip`, `heroCard`, `barCard`, `chart` (dithered fade), `pill`, `chip`. Roles: bg, card, track, grid, text, muted, dim; series cyan `#2FB1FF` (1) and pink `#FF7BAD` (2); gradients COOL blue->cyan->teal (CPU-type), WARM violet->pink->orchid (GPU-type, no yellow); status amber `#FFD050` and red `#FF6A4A` are reserved for warn/crit and alerts, never series. Text stays in text ink; status colour goes on dots, pills and fills. Re-check contrast/CVD before palette changes.
- Font: Inter, anti-aliased VLW, 8 roles `F_SMALL`..`F_CLOCK` (`tools/mkfonts.py`, `FONT_HEADER` in config.h). Glyphs: ASCII + degree sign only (no ©, no arrows). Icons: Material Symbols Rounded, drawn as text everywhere (`ICON_*` in `I_S` 28 px header/controls, `I_M` 40 px Spotify and media keys, `I_L` 80 px launcher; list in `ICONS`, `tools/mkfonts.py icons`). No hand-drawn icons left.
- Grid: margin `M`=16, status bar 0-56 (`TOP`=64), `BOTTOM`=584, `GAP`=16, cards radius 20; Server Stats ends at `STATS_BOTTOM`=524 above the 64 px tab strip.
- Components (ui.cpp): `card`, `appIcon`, `ring` (270° gauge, gradient arc), `lane` (smoothed line over flat fill, autoscaled y via `fitRange`, colour on the absolute scale), `pill`, `segmented`, `stepper`, `toggle`, `textFit` (truncate "..."), `wrapText`, `roundCorners`, `drawHeader` (status dot, title, subtitle or OFFLINE + last seen, date/clock, page dots, gear).
- Graphs: 360 points per window (15/30/60 min), each point the mean of its slot's readings, display-only 9-point smoothing; gaps stay gaps.

## Data sources
| Source | Protocol | Cadence | Task | Notes |
|---|---|---|---|---|
| PC "WINTERMUTE" | LibreHardwareMonitor HTTP `:8085/Sensor` | 2 s, 5 requests, keep-alive | fetch (core 0, prio 1) | sensor IDs in config.h; dropped request keeps the last value |
| NAS "SAGA" | Glances `:61208/api/4` | quicklook+network 2 s; sensors+gpu 6 s; containers 10 s | fetch | network rate from byte counters over our own interval (Glances' rate fields are per-any-client) |
| Unraid API | GraphQL POST `/graphql` | 30 s | fetch | API key in config.h |
| Printer P1S | MQTT over TLS 8883, `device/<SN>/report`, `pushall` on connect | ~1 s pushes | printer (prio 1) | partial reports merged; paused while the radar app shows |
| Printer camera | TLS 6000, 80-byte login, 16-byte header + JPEG | ~1 fps 1280x720 | printer-cam (prio 1) | only while printer page/full camera shows; decoded at 1/2 on core 0 |
| Radar map | Esri World Dark Gray tiles (Base jpg + Reference png), HTTPS | once per place+zoom, kept | radar (idle prio) | CARTO now needs an API key (watermark) |
| Radar frames | NOAA MRMS `conus_bref_qcd` WMS GetMap PNG, HTTPS, `TIME=` | 10 frames x 6 min, every 5 min | radar | CONUS only |
| Weather | api.weather.gov `/points`, station obs, forecast, `/alerts/active?point=` | obs+forecast 10 min, alerts 2 min | radar | needs User-Agent; °F/mph |
| ZIP lookup | api.zippopotam.us (HTTP) | on demand | radar | saved in Settings |
| Spotify | Web API `GET /me/player` (HTTPS), controls `/me/player/*`, Liked Songs `/me/library` | 2 s playing, 5 s idle; 700 ms after a control | spotify (idle prio, only while shown) | token from `accounts.spotify.com` hourly; art from `i.scdn.co` over plain HTTP |
| Companion | Satellite API TCP 16622 | event-driven | companion (prio 1) | keys as PNG; taps via queue |
| NTP | pool.ntp.org | system | - | TZ in config.h |

All panels poll on even wall-clock seconds and redraw on the second, so side-by-side panels match.

## Architecture
- `loop()` (core 1): touch every 20 ms; copies `shared` + printer state; samples history once a second; redraws only on change (new data round, camera/radar frame, minute, touch) within a ~1/s budget, plus a follow-up redraw 200 ms after a tap; OTA; sleep/backlight/night.
- Image work stays in ui.cpp (platform-free, so the preview runs it): camera half-size decode `uiCamDecode`, radar `uiRadarLayer`/`uiRadarCompose`. Tasks decode on core 0 and hand finished sprites over under a mutex; `loop()` only try-locks (never waits).
- Hand-over pattern for a new background source: own task + lock-protected slot + `xxxPump()` called from `loop()` that calls a `ui*` setter and returns "redraw?".

## Memory (internal RAM is the budget)
- ~140 KB free after boot. mbedTLS allocates in PSRAM (allocator swapped in `setup()`), so a TLS connection costs ~10 KB of internal RAM, not ~35 KB (+30 KB per handshake) as before 2026-10-27. The older safeguards stay for now: MQTT pauses while the radar app shows; camera, radar and Spotify take turns.
- Received TCP data queues in internal RAM (64 KB window per socket): a large fast download read by an idle-priority task dips it (Spotify album art did, to 40 KB; now 16 KB Range pieces).
- Everything big lives in PSRAM: sprites (`setPsram(true)`), JSON (`JsonDocument doc(&psram)`), buffers (`ps_malloc`), structs over 4 KB (heap).
- Library busy-waits (HTTPClient, PubSubClient) + the 5 s task watchdog: keep network timeouts under 5 s above idle priority.
- The log's "RAM: new low" / "RAM free X KB, lowest Y KB" lines are the gauge. Healthy floor so far: ~66-75 KB before the PSRAM TLS swap; ~105 KB after (panel D, Spotify playing, printer connected).

## Extending
- **Page:** append to `Page` (saved by number), add a `draw*Page` + case in `uiDraw`, a `startL` label, history `Hist` if graphed (add to `ALL_HIST`), preview shot (gear tap moves 22 px left per page).
- **App:** append to `Function` (+ `fnL` label), tile in `drawLauncher` `apps[]` (the 2 + 3 layout is full: a sixth needs a new one), dispatch in `uiDraw` like `FN_RADAR`, icon function.
- **Setting:** append a field to `Settings` (never reorder), default in the `settings` initializer + config.h, clamp in `loadSettings`.
- **Network source:** its own task or the fetch task; timeouts < 5 s; TLS only if it fits the budget above; JSON via PSRAM allocator and an ArduinoJson filter.
- **Verify:** preview (`tools/build-preview.sh ... samples`; sample data via `tools/preview_data.py`), build, flash the test panel, read the log (`tools/serial_log.py`). The preview draws in the panel's real pixel format (rgb565_nonswapped), so format bugs show up there.
