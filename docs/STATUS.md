# Status (2026-10-27)

Where things stand, to resume from. Update this file at the end of a session. Design and how-tos: [DESIGN.md](DESIGN.md).

## Panels
| Panel | Build | Notes |
|---|---|---|
| A | `ui-refresh` cb50822 + uncommitted SD work (2026-10-28), SD card with fonts | `p4-ota` default target |
| B (test panel) | `ui-refresh` cb50822 + uncommitted SD work (2026-10-28), SD card with fonts | usually on USB (CH343, was COM10): flash and log it first |
| D | `ui-refresh` cb50822 + uncommitted SD work (2026-10-28), SD card with fonts | added 2026-10-25 (USB flash on COM12, factory demo's NVS erased first); first-boot defaults |

Panel C was faulty and returned. IPs and MACs: comments in `ota.ini` (git-ignored). Flash any panel by IP: `pio run -e p4-ota -t upload --upload-port <ip>`. Location (radar ZIP) and settings are per panel.

## Done and tested
- Pages: Desktop, NAS, Storage, Docker, Printer (camera first, full-screen camera).
- Apps: Macro Pad, Stream Deck (Companion), Weather Radar (NOAA loop over Esri map, ZIP location sheet, NWS conditions/forecast/alerts with alert sheet), Launcher (2 wide tiles over 3).
- Performance: redraw only on change; keep-alive HTTP; filtered JSON; camera decode on core 0; Glances sensors/gpu every 6 s; touch I2C 400 kHz; idle-friendly task waits.
- Stability fixes: watchdog (radar label decode per frame; HTTPClient busy-wait -> radar task at idle priority, timeouts < 5 s, MQTT socket timeout 3 s); RAM (camera/radar take turns; radar frees TLS on stop; JSON in PSRAM; MQTT paused during radar).
- Network speed computed from Glances byte counters (panels now agree).

## New: Spotify app (2026-10-27; working on panel D, committed)
Launcher app and Function (`FN_SPOTIFY`): `src/spotify.cpp` + the screen in ui.cpp; setup in SETUP.md (the user's Spotify developer app, Premium, `tools/spotify_auth.py`; done, values in config.h). The launcher became two wide tiles over three. Tested on D: state, art, next/previous, volume and seek by tap or drag (a drag that starts on a slider sets it on release: `uiDrag`, `GEST_DRAG`). Fixed on the way:
- Opening it wedged the Wi-Fi link (internal RAM 35-39 KB): mbedTLS now allocates in PSRAM (`tlsCalloc` in main.cpp), for every TLS user. Printer MQTT dropped from ~35 KB to ~10 KB internal.
- Album art fetched whole dipped RAM to 40 KB (TCP window queued in internal RAM for an idle-priority reader): now 16 KB Range pieces. Lowest seen since: 105 KB.
- HTTP 204 replies (volume, seek) hung the task: HTTPClient read on with no Content-Length. Not read now.
The screen is now Car Thing style (art-tinted background, big title, button strip, volume slider in the strip), and every icon on the panel is a Material Symbol (`tools/mkfonts.py icons`).
Not yet tried: like (`/me/library` scopes), shuffle/repeat, a phone as the device. Panels A and B don't have this build.

## Open: camera watchdog crash (2026-10-26; fix on all panels 2026-10-27, not yet proven)
Panel D crashed the same way on 2026-10-27 (core dump: `camTask` -> `uiCamDecode`, IDLE0 starved). A, B and D all run the fix now (committed 2026-10-27); it still needs a long printer-page session to confirm.
Panel B rebooted on the task watchdog (IDLE0 starved 5 s) while the camera task (priority 1, core 0) decoded frames back to back: after a Wi-Fi hiccup, queued frames arrive in a burst and each 1280x720 decode is long CPU work. Fix in `printer.cpp`: camera task at `tskIDLE_PRIORITY`, skip a frame when another is already waiting, 10 ms pause after each decode. To do: confirm with long printer-page sessions (log running on B).

## Open: RAM low of 27 KB (likely explained 2026-10-27, re-test)
Likely cause: TLS buffers in internal RAM plus big downloads (radar PNGs, camera frames) queuing in the 64 KB TCP window while an idle-priority task reads; see the Spotify notes. With TLS now in PSRAM, re-run the test below on the new build; if the floor holds near 100 KB, close this and consider relaxing the MQTT pause and the camera/radar turn-taking.
Seen once on panel B after ~3 min on the printer page (camera) then ~80 s on the radar incl. a 5-min refresh; a later short run only reached 66 KB. Not yet reproduced with the low-water log.
- **Test:** start a capture first (background, ~30 min), e.g.
  `"$USERPROFILE/.platformio/penv/Scripts/python.exe" tools/serial_log.py --seconds 1800 --no-touch > <scratchpad>/log.txt`
  then on panel B: ~6 min on Weather Radar (spans a refresh), ~5 min on the printer page (camera running), then radar again.
- **Read:** each `RAM: new low` line sits next to what happened (`printer:`, `radar:`, camera lines). Lows ~66-75 KB are normal (handshake peaks).
- **Suspects:** camera drop + reconnect handshake while MQTT and fetches run; leaving the radar for a page other than the printer keeps its TLS for the 30 s linger while MQTT reconnects; radar 5-min refresh hopping hosts (Esri/NOAA/NWS handshakes).
- **Fix options, cheapest first:** stop the radar at once whenever its app closes (not only for the camera); a global "one TLS handshake at a time" semaphore across tasks; mbedTLS dynamic buffers / PSRAM allocation (custom sdkconfig, ~8 min rebuilds, affects all TLS).
- Afterwards: drop or keep the low-water log in `loop()` (cheap), and flash panel A.

## Desk enclosure (cad/)
A Fusion script builds a five-module arc (screen, screen, centre, screen, screen) for the four panels, with print-ready STLs; see [cad/README.md](../cad/README.md). It's verified in Fusion (no overlaps, fits probed) but not printed yet.
Alternative first pass, `cad/CaseStand/CaseStand.py`: imports `p4_case.stl` (a known-good back shell) unchanged, tilts it to 65 degrees and slides it into a stand: a frame (20 mm USB bays each side, top and bottom caps, flush with the case face, 0.6 mm clearance per side for printing) on a shelled wedge, cables leaving the right bay through the wedge; five modules on the 800R arc, the centre one holding a blank plate shelled open at the back. A separate 3 mm bezel over the face holds the case in: four bosses with M3 heat-set inserts drop through the frame's face into the bays and are screwed from behind through tabs (M3 x 8). A removable L cover closes the stand's bottom and back (cable slot at its foot, vent slots high up), held by four M3 x 8 countersunk screws into inserts in blocks on the stand's side walls. Insert holes are 1 mm deeper than the insert with a tapered mouth; holes across the print direction are teardrops (stand and cover assumed printed desk side down). The pocket floor has 3.4 mm holes in line with the case's four back plate holes (the board's 156 x 89 pattern) to screw case and stand together. Neighbours bolt together through their side walls (two M3 x 10 and nuts per joint; the end modules show two unused holes on their outer sides). The bezel's outer front edges and the frame's top side edges have a 1 mm chamfer so neighbours meet in a V groove, and the opening has a 1.5 mm bevel toward the glass. STLs (as modelled, not turned for printing) are in `cad/CaseStand/stl/`. Builds cleanly in Fusion (no overlaps); not printed.
- **Next:** print the `coupon_*` set, check the fits, then tune the constants. Unconfirmed so far: that the board is centred on the glass (hole positions), the post depth (15 mm), the USB-C cable's hole pitch, and edge swipes with the glass recessed 0.5 mm.
- Fusion has a built-in MCP server on `127.0.0.1:27182/mcp` (not registered with Claude Code; reachable over HTTP with JSON-RPC). `claude mcp add --transport http fusion http://127.0.0.1:27182/mcp` would register it.

## Next / backlog
- **Paused 2026-10-28: making the UI cohesive with the Spotify screen.** Mockups and where the user landed: end of [UI_REFRESH.md](UI_REFRESH.md) (likes the Cohesive Docker and Settings; the Blend page for the rest awaits review). Resume there.
- Spotify library and devices (2026-10-28, branch `ui-refresh`, working on panel D with the new token: library loads, playing a playlist from it works): library sheet ("Jump back in" from recently played, "Your playlists") with covers, tap to play (falls back to the first device, a computer first, when none is active); devices sheet, tap to move playback. Token re-made with the new scopes. Spotify's own playlists (ids `37i9dQZ...`) answer 404 to apps, so they never appear in "Jump back in". An empty row shows Spotify's error in amber. Spotify's recommendations, featured playlists and Made for You mixes are closed to new apps (Nov 2024). The sheets follow the Car Thing clone's browse screens (tabs across the top, green underline, the green note back to the player). Full-screen gradients are dithered and cached per colour (`gradient()`), which fixed the banding on the player.
- UI refresh: built on branch `ui-refresh` and flashed to panel D 2026-10-28 (plan and mockups: [UI_REFRESH.md](UI_REFRESH.md)). To do: user review on the panel, commit, merge, flash A and B. The day palette is now drawn as designed (THEME_DAY no longer dims it; night = THEME_NIGHT / THEME_DAY).
- microSD card: works on panel D (2026-10-28; mount, read, write tested; `sdProbe` logs it at boot). It runs at 40 MHz: at 20 MHz writes timed out under Wi-Fi traffic (the two SD slots share a clock divider). Japanese fonts are on panel D's card (7 of 7 load at boot); Japanese on the real screen is still to be seen (play a Japanese track). Files go onto a card over Wi-Fi with `python tools/sd_upload.py <ip> [folder]` (37 MB in 30 s). All three panels have cards with the fonts (7 of 7 load). Panel B's card is slower (fonts load in 3 s at boot vs 0.7 s on D): writing to it while Wi-Fi traffic streamed in wedged the C6 link, so uploads now go in 1 MB pieces written between transfers. Internal RAM still dips to ~46 KB while a piece comes in (the wedge was ~35 KB): watch it. Plan in [SDCARD.md](SDCARD.md) (logs, Japanese fonts, history, printer timelapse, caches, recovery). Waiting for cards; Japanese text for Spotify waits on it (decided 2026-10-27: fonts on the card, not in flash).
- Git: local repository since 2026-10-25, branch `main`, no remote yet. Secrets are git-ignored (`src/config.h`, `ota.ini`); generated previews too. Before adding a remote, decide whether the LAN IPs and home ZIP in `config.example.h` should be genericised.
- Ideas mentioned, not started: none pending from the user. Launcher is full again (a sixth app needs a new layout).
- Alert test ZIPs change daily: find current ones via `api.weather.gov/alerts/active` (an Alaska ZIP like 99772 shows several alerts but no radar: CONUS only).

## User decisions (don't re-ask)
- Printer page stays a page (an app wouldn't perform better). Camera is the priority on that page.
- Radar extras: weather on the radar screen only (no background alert polling, no global alert badge).
- Pause only the printer MQTT during the radar (stats keep polling; printer graph gaps are fine).
- Radar touch targets are fine as they are. Panels poll in step (same values side by side).
- Visual: dark slate theme, Inter font, separate graph lanes (no overlays), offline states explicit.
- Enclosure: 800R arc, faces at 65 degrees from the desk, panels mounted 180 degrees from Waveshare's drawing (like `p4_case.stl`, USB on the right), active area centred.
- Enclosure: modules are glued together (no joining hardware); the back panel and floor stay one L part; the screen modules stay linked copies of one component; bezels are separate parts printed face-up for ironing.
