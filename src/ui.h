// Dashboard drawing. No Arduino/ESP dependencies, so the preview tool can build it on a PC too.
#pragma once
#include <LovyanGFX.hpp>
#include <time.h>
#include <math.h>
#include <stdint.h>

constexpr int W = 1024, H = 600, HIST_N = 360, MAX_DISKS = 16, MAX_CTR = 16;

struct Disk { char name[10]; float pct; float temp; float sizeTB; };
struct Container { char name[28]; char status[12]; char uptime[14]; float cpu; float memMB; };
// Bambu printer over its LAN MQTT broker (printer.cpp). state is gcode_state: IDLE, PREPARE, RUNNING, PAUSE, FINISH,
// FAILED. stage is stg_cur (a sub-stage such as "Heating bed"); fan[] = part, aux, chamber in %; -1 = not reported yet.
struct Printer {
  float noz = NAN, nozT = NAN, bed = NAN, bedT = NAN;
  int pct = -1, mins = -1, layer = 0, layers = 0, stage = -1, speed = 0, fan[3] = {-1, -1, -1};
  uint32_t err = 0;
  char state[12] = "", name[48] = "", fil[12] = "";  // fil: external spool type, e.g. "PLA"
};
struct Data {
  float pcCpu = NAN, pcCpuT = NAN, pcGpu = NAN, pcGpuT = NAN, pcRam = NAN;
  float nasCpu = NAN, nasCpuT = NAN, nasGpu = NAN, nasGpuT = NAN, nasRam = NAN, nasRx = NAN, nasTx = NAN;  // rx/tx in bit/s
  Disk disks[MAX_DISKS]; int nDisks = 0;
  double arraySizeKB = 0, arrayFreeKB = 0;
  Container ctr[MAX_CTR]; int nCtr = 0;  // sorted by name
  uint32_t pcOk = 0, nasOk = 0, unraidOk = 0, dockerOk = 0;  // millis() of last success
  Printer pr;
};

// Saved in settings by number (startPage): only append.
enum Page { PAGE_PC, PAGE_NAS, PAGE_STORAGE, PAGE_DOCKER, PAGE_PRINTER, PAGE_COUNT };

// usb: a PC has mounted the macro pad keyboard. deck: Companion has accepted the Stream Deck surface.
// printer: connected to the printer's MQTT broker and it has sent a report. cam: a camera frame arrived in the last 5 s.
// radar: the radar loop was fetched from NOAA in the last 15 min. spotify: Spotify's last answer was a good one.
struct Status { bool pc, nas, unraid, docker, wifi, usb, deck, printer, cam, radar, spotify; };

extern LGFX_Sprite spr;  // the frame being drawn

void uiSample(const Data& d, const Status& s, bool commit);  // call every frame; commit = close this graph slot
// Settings adjustable on the panel (gear icon), saved to flash by main.cpp. First-boot defaults: config.h.
struct Settings {
  uint8_t version;                // bump if this layout changes, so an old saved copy is ignored
  uint8_t brightness;             // backlight 0-255
  uint8_t sleepMin;               // screen sleep after this many idle minutes; 0 = never
  uint8_t nightOn;                // night mode: dimmer palette + backlight between the hours below
  uint8_t nightFrom, nightTo;     // local hours
  uint8_t nightBright;            // backlight at night
  uint8_t rotateSec;              // auto-rotate pages; 0 = off
  uint8_t clock24;
  // Appended later. Only ever add fields at the end: an older saved copy loads its prefix and keeps
  // these at their defaults.
  uint8_t graphMin;               // graph window: 15 / 30 / 60 minutes (360 points either way)
  uint8_t startPage;              // server stats page shown when that app opens at start-up or on waking
  uint8_t function;               // Function: what this panel shows at start-up and on waking (see below)
  char radarZip[6];               // weather radar location: the ZIP the user entered, and what it looked up to
  char radarPlace[28];
  float radarLat, radarLon;
  uint8_t radarZoom;              // index into RADAR_ZOOMS
};
// Saved in settings by number: only append. All but FN_LAUNCHER double as app ids.
enum Function : uint8_t { FN_STATS, FN_MACRO, FN_LAUNCHER, FN_DECK, FN_RADAR, FN_SPOTIFY, FN_COUNT };

// Macro pad keys (config.h MACRO_KEYS). kind MK_KEY: modifiers + a HID keyboard usage; MK_MEDIA: a consumer
// (media) usage. main.cpp sends them over USB; ui.cpp only draws them.
enum : uint8_t { MK_KEY, MK_MEDIA };
enum : uint8_t { MOD_CTRL = 0x01, MOD_SHIFT = 0x02, MOD_ALT = 0x04, MOD_GUI = 0x08 };  // GUI = Windows key
// icon: an ICON_* string (src/fonts/icons.h; add new names to ICONS in tools/mkfonts.py); none = the media key's own.
struct Macro { const char* label; const char* hint; uint8_t kind, mods; uint16_t code; const char* icon = nullptr; };
extern const Macro MACROS[];
extern const int N_MACROS;
extern Settings settings;

// Shown on the settings page. Ages in seconds since a source last answered; UINT32_MAX = never.
struct SysInfo { int rssi; char ip[16]; uint32_t uptimeS, heapKB, psramKB; uint32_t ageS[5]; };  // pc, nas, unraid, docker, printer

// Fonts for what Inter lacks (Japanese, accented Latin) from dir: <role>.vlw files from tools/mkfonts.py sd, e.g. on
// the microSD card. Returns how many roles got one; text falls back to them from then on.
int uiLoadExtraFonts(const char* dir);
void uiDraw(const Data& d, const Status& s, const tm* clock, bool night, const SysInfo& sys);  // clock = nullptr until NTP syncs
bool uiTakeSettingsChanged();  // true once after the user changes a setting (main.cpp saves it)
bool uiTakeRestart();          // true once after "Restart panel" is confirmed
void uiTap(int x, int y);    // page dots jump; gauges/disks/containers open detail views; any tap closes a detail view
void uiSwipe(int dir);       // +1 next page, -1 previous (closes a detail view instead, if one is open)
bool uiDrag(int x0, int y0, int x1);  // a drag that started on a slider sets it where it ended; false: not on one
void uiNextPage();           // for timed rotation (PAGE_SECONDS)
void uiShowPage(Page p);     // jump straight to a page, closing any detail view; PAGE_COUNT = the launcher
void uiShowLauncher();       // home screen (swipe up): a tile per app
void uiHome();               // the panel's Function (settings): at start-up and on waking
int uiTakeMacro();           // index into MACROS once after a macro key is tapped, else -1

// Stream Deck (Bitfocus Companion Satellite surface): DECK_COLS x DECK_ROWS keys, each a DECK_PX square image
// drawn by Companion. main.cpp runs the connection (companion.cpp); ui.cpp only decodes and draws the images.
constexpr int DECK_COLS = 5, DECK_ROWS = 3, DECK_KEYS = DECK_COLS * DECK_ROWS, DECK_PX = 160;
void uiDeckKey(int key, const uint8_t* data, size_t len, bool png);  // png, or raw RGB888 DECK_PX square; len 0 = blank
int uiTakeDeckPress();       // key index once after a deck key is tapped, else -1

// Printer camera. uiCamDecode decodes a JPEG at exactly half size into dst; it touches nothing else, so the camera
// task calls it, keeping the decode off loop()'s task. uiCamFrame then copies that picture in (call it from the
// drawing task); it returns uiCamWanted(), i.e. whether the frame is worth a redraw.
bool uiCamDecode(LGFX_Sprite& dst, const uint8_t* jpg, size_t len);
bool uiCamFrame(const LGFX_Sprite& half);
bool uiCamWanted();          // the printer page or the full-screen camera is showing

// Weather radar app. radar.cpp downloads and times the loop; the image work is here so the preview runs it too.
// The map is RADAR_W x RADAR_H in Web Mercator (256 px tiles), centred on settings.radarLat/Lon.
constexpr int RADAR_W = 1024, RADAR_H = 536;  // full width, above the strip
constexpr int RADAR_ZOOMS[] = {10, 8, 6}, N_RADAR_ZOOMS = 3;  // local (~130 km across), regional, wide
// World pixel of the map's top-left corner at zoom z, and the map's extent in EPSG:3857 metres (for the radar WMS).
void uiRadarOrigin(float lat, float lon, int z, double& left, double& top);
void uiRadarBBox(float lat, float lon, int z, double& minX, double& minY, double& maxX, double& maxY);
// A map tile source: tile (x, y) at zoom z as a JPEG or PNG, or false if it's missing.
typedef bool (*RadarTileFn)(int z, int x, int y, const uint8_t*& img, size_t& len);
// Builds a map layer into dst from tiles, over `fill`: the base map, or the place names on black (RADAR_CLEAR).
constexpr uint32_t RADAR_CLEAR = 0x000000;  // the label layer's transparent colour
bool uiRadarLayer(LGFX_Sprite& dst, float lat, float lon, int z, RadarTileFn tile, uint32_t fill);
// dst = base, then a radar frame (a PNG, transparent where there's no echo), then the label layer, so place names
// stay readable over heavy echoes (nullptr = none). Built once per view, the layers make a frame mostly copying.
bool uiRadarCompose(LGFX_Sprite& dst, const LGFX_Sprite& base, const uint8_t* png, size_t len, const LGFX_Sprite* labels);
// Local weather for the radar screen, from api.weather.gov (radar.cpp fetches it while the radar is open).
// ~5 KB with the alert texts: keep instances on the heap (PSRAM), not in internal RAM.
struct WxAlert {
  char event[48];      // e.g. "Severe Thunderstorm Warning"
  char severity[12];   // Extreme, Severe, Moderate, Minor or Unknown
  time_t ends;         // UTC; 0 = until further notice
  char text[1536];     // headline, description and instructions; paragraphs separated by '\n'
};
struct Weather {
  bool have = false;                          // conditions and forecast fetched for the current place
  float tempF = NAN, windMph = NAN, humidity = NAN;
  char sky[40] = "", windDir[4] = "";         // "Mostly Cloudy", "SE"
  struct Period { char name[24]; int temp; char brief[48]; int pop; } periods[2];  // pop: rain chance %, -1 = none
  int nPeriods = 0;
  bool alertsKnown = false;                   // the alert check has answered (with none, or some)
  int nAlerts = 0;
  WxAlert alerts[3];                          // most severe first
};
void uiRadarWeather(const Weather& w);  // from the drawing task: the latest weather for the radar screen
// From the drawing task: the next frame of the loop (t = its time; frame of frames). Returns uiRadarWanted().
bool uiRadarFrame(const LGFX_Sprite& pic, time_t t, int frame, int frames);
bool uiRadarWanted();        // the radar map is showing (not one of its sheets)
bool uiRadarApp();           // the radar app is showing: the map or one of its sheets
const char* uiTakeRadarZip();  // a ZIP the user entered, once, for radar.cpp to look up; else nullptr
// radar.cpp's answer to that lookup: ok = found (place e.g. "Kansas City, MO"), else not found or no network.
void uiRadarPlace(bool ok, const char* zip, float lat, float lon, const char* place);

// Spotify app: what's playing on the user's account, from the Web API (spotify.cpp polls it while the app is on
// screen), and its controls. Album art goes through uiSpotifyDecode (the Spotify task) and uiSpotifyArt (loop()).
constexpr int SPOTIFY_ART = 420;  // album art drawn SPOTIFY_ART px square
struct Spotify {
  bool active = false;            // a device is playing or paused
  bool playing = false, shuffle = false, liked = false, canVolume = false;
  uint8_t repeat = 0;             // 0 off, 1 context (the playlist or album), 2 this track
  int volume = -1;                // the device's volume in %, -1 = unknown
  uint32_t progressMs = 0, durationMs = 0;
  char id[24] = "", title[96] = "", artist[96] = "", album[96] = "", device[40] = "";
  char error[72] = "";            // why the last request failed, for the header; "" = it didn't
};
// SP_CONTEXT: play library item arg; SP_DEVICE: move playback to device arg (indexes into the lists below).
enum SpotifyCmd : uint8_t { SP_PLAY, SP_PAUSE, SP_NEXT, SP_PREV, SP_SEEK, SP_VOLUME, SP_SHUFFLE, SP_REPEAT, SP_LIKE,
                            SP_CONTEXT, SP_DEVICE };
// The library sheet: recently played albums, playlists and artists, then the user's playlists. Covers come one at a
// time through uiSpotifyCover (index into item[]), decoded by uiSpotifyCoverDecode in the Spotify task.
constexpr int SPOTIFY_RECENT = 6, SPOTIFY_LISTS = 6, SPOTIFY_ITEMS = SPOTIFY_RECENT + SPOTIFY_LISTS, SPOTIFY_COVER = 148;
struct SpotifyItem { char name[56]; char sub[40]; };  // sub: "Album", "Playlist", "Artist", or the owner
struct SpotifyLibrary {  // recent first; error: why a row is empty when Spotify refused it
  bool known = false; int nRecent = 0, nLists = 0; SpotifyItem item[SPOTIFY_ITEMS]; char error[2][72] = {};
};
struct SpotifyDevice { char name[40]; char type[16]; bool active; int volume; };  // type: Computer, Smartphone, Speaker...
struct SpotifyDevices { bool known = false; int n = 0; SpotifyDevice d[8]; };
void uiSpotifyLibrary(const SpotifyLibrary& lib);
void uiSpotifyDevices(const SpotifyDevices& devs);
bool uiSpotifyCoverDecode(LGFX_Sprite& dst, const uint8_t* jpg, size_t len);  // SPOTIFY_COVER px square
void uiSpotifyCover(int i, const LGFX_Sprite& cover);
bool uiSpotifyLibraryOpen();                // the library sheet is showing (spotify.cpp fetches it then)
bool uiSpotifyDevicesOpen();                // the devices sheet is showing (spotify.cpp polls the devices then)
void uiSpotify(const Spotify& s);           // from the drawing task: the latest state
// Album art JPEG to SPOTIFY_ART px square, and its colour for the background (RGB888).
bool uiSpotifyDecode(LGFX_Sprite& dst, const uint8_t* jpg, size_t len, uint32_t& tint);
void uiSpotifyArt(const LGFX_Sprite& art, uint32_t tint);  // from the drawing task: new art (empty sprite = none)
bool uiSpotifyWanted();                     // the Spotify app is showing
// A control tapped, once: cmd + arg (seek: ms; volume: %; shuffle, like: 0/1; repeat: 0-2). False if none.
bool uiTakeSpotify(uint8_t& cmd, int& arg);
