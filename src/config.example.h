// Template for src/config.h (git-ignored): copy it to config.h and fill in the blank values.
#pragma once

// --- Wi-Fi (2.4 GHz only) ---
#define WIFI_SSID ""
#define WIFI_PASS ""
#define TZ_INFO   "CST6CDT,M3.2.0,M11.1.0"  // US Central, POSIX TZ string

// --- Desktop: LibreHardwareMonitor remote web server ---
#define PC_HOST "192.168.1.20"
#define PC_PORT 8085
#define PC_NAME "WINTERMUTE"
#define PC_SUB  "i7-9700K / RTX 3090"
// Sensor IDs from http://PC_HOST:8085/data.json ("SensorId" fields). docs/SETUP.md has a one-liner to list them.
#define LHM_CPU_LOAD "/intelcpu/0/load/0"         // CPU Total
#define LHM_CPU_TEMP "/intelcpu/0/temperature/8"  // CPU Package (index = core count on Intel)
#define LHM_GPU_LOAD "/gpu-nvidia/0/load/7"       // D3D 3D, matches Task Manager. "/gpu-nvidia/0/load/0" = GPU Core (NVIDIA's busy-time, also counts CUDA)
#define LHM_GPU_TEMP "/gpu-nvidia/0/temperature/0"
#define LHM_RAM_LOAD "/ram/load/0"                // Memory

// --- NAS: Glances container (CPU, RAM, temp, network) ---
#define NAS_HOST "192.168.1.99"
#define GLANCES_PORT 61208
#define NAS_NAME "SAGA"
#define NAS_SUB  "Ryzen 5 5600X / RTX 3080"
#define NAS_IFACE "eth0"                  // physical NIC
#define NAS_CPU_TEMP_LABEL "CPU"          // Glances sensor label: the board's CPU sensor (Tctl reads ~10 C higher). Fallback: first temp sensor

// --- NAS: Unraid API (disk usage + temps without spinning disks up). Empty = disk panel off ---
#define UNRAID_API_KEY ""

// --- Display tuning ---
#define GRAPH_MINUTES 15  // default graph window: 15 / 30 / 60. Changeable in settings
#define START_PAGE 0      // default server stats page: 0 desktop, 1 NAS, 2 storage, 3 docker
#define PANEL_FUNCTION 0  // default Function (what the panel shows at start-up): 0 server stats, 1 macro pad, 2 launcher, 3 stream deck, 4 weather radar, 5 spotify
#define PAGE_SECONDS 0     // default auto-rotate (s); 0 = off. Changeable in settings
#define TOUCH_FLIP_X 1     // this board's touch panel is mounted 180 degrees from the display
#define TOUCH_FLIP_Y 1
#define SLEEP_MINUTES 10   // default screen sleep (min); 0 = never. Changeable in settings

// --- Font: anti-aliased VLW family from tools/mkfonts.py (comment out for the built-in bitmap fonts) ---
#define FONT_HEADER "fonts/inter.h"

// --- Display defaults (first boot only; after that the panel's settings page and its saved values win) ---
#define CLOCK_24H   0
#define NIGHT_ON    0      // night mode: dimmer palette + backlight between NIGHT_FROM and NIGHT_TO
#define NIGHT_FROM  22
#define NIGHT_TO    7
#define THEME_DAY   0.75f  // day palette brightness: 0.75 = the palette as designed
#define THEME_NIGHT 0.5f   // night: the palette dimmed by THEME_NIGHT / THEME_DAY
#define BL_DAY      160    // backlight PWM, 0-255
#define BL_NIGHT    40     // backlight in night mode

// --- Stream Deck: a Bitfocus Companion surface over its Satellite API (Companion 3.4+; PNG keys need 4.x) ---
// Companion's address (the PC or the NAS container running it). Empty = deck off. Port 16622 is Companion's default.
#define COMPANION_HOST PC_HOST  // Companion runs on the desktop
#define COMPANION_PORT 16622

// --- 3D printer: Bambu Lab P1S over its LAN MQTT broker (printer.cpp). Leave PRINTER_IP empty to turn it off ---
// IP and LAN access code: the printer's Settings > WLAN/Network. Serial number: Settings > Device.
#define PRINTER_IP   "192.168.1.19"
#define PRINTER_SN   ""
#define PRINTER_CODE ""  // LAN access code
#define PRINTER_NAME "P1S"
#define PRINTER_SUB  "Bambu Lab 3D printer"

// --- Weather radar app: NOAA MRMS radar (US only) over an Esri dark gray map. These are first-boot defaults: the
// location can be changed on the panel (radar app > gear), which looks the ZIP up at api.zippopotam.us ---
#define RADAR_ZIP   "64105"
#define RADAR_PLACE "Kansas City, MO"
#define RADAR_LAT   39.0997f
#define RADAR_LON   -94.5786f

// --- Macro pad: a USB keyboard on the board's USB-A port (connect it to the PC with a USB-A to USB-A cable) ---
// {label, hint, MK_KEY, modifiers, key[, icon]} or {label, hint, MK_MEDIA, 0, media key[, icon]}. Icons: ICON_*
// names from src/fonts/icons.h (add others to ICONS in tools/mkfonts.py); media keys default to their own. Key codes are HID usages:
// A-Z = 0x04-0x1D, 1-9 = 0x1E-0x26, 0 = 0x27, Esc = 0x29, F1-F12 = 0x3A-0x45, F13-F24 = 0x68-0x73.
// F13-F24 exist on no real keyboard, so any app can bind them without clashing. Media: next 0xB5, previous 0xB6,
// play/pause 0xCD, mute 0xE2, volume up 0xE9, volume down 0xEA. 12 keys fill a 4 x 3 grid.
#define MACRO_KEYS {                                                        \
    {"Previous", "media", MK_MEDIA, 0, 0xB6},                               \
    {"Play / Pause", "media", MK_MEDIA, 0, 0xCD},                           \
    {"Next", "media", MK_MEDIA, 0, 0xB5},                                   \
    {"Mute", "media", MK_MEDIA, 0, 0xE2},                                   \
    {"Volume -", "media", MK_MEDIA, 0, 0xEA},                               \
    {"Volume +", "media", MK_MEDIA, 0, 0xE9},                               \
    {"Mic Mute", "Win+Alt+K", MK_KEY, MOD_GUI | MOD_ALT, 0x0E, ICON_MIC_OFF_FILL},             \
    {"Snip", "Win+Shift+S", MK_KEY, MOD_GUI | MOD_SHIFT, 0x16, ICON_SCREENSHOT_REGION},             \
    {"Task Manager", "Ctrl+Shift+Esc", MK_KEY, MOD_CTRL | MOD_SHIFT, 0x29, ICON_MONITORING}, \
    {"Desktop", "Win+D", MK_KEY, MOD_GUI, 0x07, ICON_DESKTOP_WINDOWS_FILL},                            \
    {"Lock PC", "Win+L", MK_KEY, MOD_GUI, 0x0F, ICON_LOCK_FILL},                            \
    {"Custom", "F13: bind it in any app", MK_KEY, 0, 0x68, ICON_KEYBOARD_COMMAND_KEY}}

// --- Spotify app: now playing and controls over the Spotify Web API. Empty SPOTIFY_REFRESH = off ---
// Your own Spotify developer app (its owner needs Premium) and a refresh token from tools/spotify_auth.py: see the
// README's Spotify section.
#define SPOTIFY_CLIENT_ID     ""
#define SPOTIFY_CLIENT_SECRET ""
#define SPOTIFY_REFRESH       ""
