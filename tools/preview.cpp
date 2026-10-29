// Renders the dashboard with sample data to preview.png, using the same ui.cpp as the firmware.
// Build: tools/build-preview.sh (needs zig + SDL2 headers; SDL is only LovyanGFX's desktop backend).
// Usage: preview [scenario [output prefix]]. Scenarios: normal (default), hot (amber/red alerts), idle (quiet night,
// 24 h clock), offline (PC + Docker down, clock not synced), night (night palette). One per run: ui.cpp keeps
// history in statics, so each scenario needs a fresh process.
#include "../src/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LGFX_Sprite spr;

// Weather radar map tiles saved in .pio/preview/radar/ (not in the repo): z_x_y.jpg (base), z_x_y_l.png (labels).
static bool previewTile(const char* fmt, int z, int x, int y, const uint8_t*& img, size_t& len, uint8_t* buf, size_t cap) {
  char path[96];
  snprintf(path, sizeof path, fmt, z, x, y);
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  len = fread(buf, 1, cap, f);
  fclose(f);
  img = buf;
  return true;
}
static bool baseTile(int z, int x, int y, const uint8_t*& img, size_t& len) {
  static uint8_t buf[128 * 1024];
  return previewTile(".pio/preview/radar/%d_%d_%d.jpg", z, x, y, img, len, buf, sizeof buf);
}
static bool labelTile(int z, int x, int y, const uint8_t*& img, size_t& len) {
  static uint8_t buf[128 * 1024];
  return previewTile(".pio/preview/radar/%d_%d_%d_l.png", z, x, y, img, len, buf, sizeof buf);
}

static float noise() {  // deterministic, so the preview is the same every run
  static uint32_t s = 12345;
  s = s * 1664525 + 1013904223;
  return (s >> 8) / float(1 << 24) - 0.5f;
}

int main(int argc, char** argv) {
  enum { NORMAL, HOT, IDLE, OFFLINE, NIGHT } sc = NORMAL;
  const char* scName = argc > 1 ? argv[1] : "normal";
  const char* names[] = {"normal", "hot", "idle", "offline", "night"};
  int found = -1;
  for (int i = 0; i < 5; i++) if (!strcmp(scName, names[i])) found = i;
  if (found < 0) { fprintf(stderr, "unknown scenario %s\n", scName); return 1; }
  sc = decltype(sc)(found);
  const char* prefix = argc > 2 ? argv[2] : "";
  if (sc == IDLE) settings.clock24 = 1;

  spr.setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);  // the panel's frame buffer format, so format bugs show here too
  spr.createSprite(W, H);
  printf("extra fonts: %d of 7 from sd/fonts\n", uiLoadExtraFonts("sd/fonts"));  // tools/mkfonts.py sd

  Data d;
  Status st{true, true, true, true, true, true, true, true, true, true, true};
  for (int k = 0; k < HIST_N * 5; k++) {  // 30 minutes, 5 readings per slot: idle, then a gaming session
    int i = k / 5;
    float t = i / float(HIST_N), game = sc == IDLE ? 0 : sc == HOT ? 1.1f : t > 0.45f ? 1 : 0;
    if (sc == OFFLINE) st.pc = st.docker = st.printer = t < 0.8f;  // went down near the end: the graphs show the gap
    // Printer: heats up halfway in, then prints (idle: finished, cooling down). Late enough that the disk/container
    // history below doesn't push the heat-up out of the graph window.
    float heat = sc == IDLE ? fmaxf(1.5f - t * 1.5f, 0) : fminf(fmaxf((t - 0.5f) * 8, 0), 1);
    d.pr.noz = 26 + heat * 194 + (heat > 0.99f ? 1.5f * noise() : 0);
    d.pr.bed = 25 + heat * 40 + 0.3f * noise();
    d.pcCpu = 6 + game * 30 + 8 * sinf(i * 0.11f) * game + 4 * noise();
    d.pcGpu = 3 + game * (88 + 6 * noise()) + 2 * noise();
    d.pcCpuT = 38 + game * 22 * fminf((t - 0.45f) * 6, 1) + 2 * noise();
    d.pcGpuT = 35 + game * 33 * fminf((t - 0.45f) * 4, 1) + noise();
    float burst = sc == IDLE ? 0 : sc == HOT ? 1.6f : (i > 120 && i < 200) ? 1 : 0;  // a download
    d.nasRx = (25 + burst * 640 + 15 * sinf(i * 0.2f) + 10 * noise()) * 1e6f;
    d.nasTx = (8 + 30 * fmaxf(sinf(i * 0.05f), 0) + 4 * noise()) * 1e6f;
    d.nasCpu = 10 + 25 * burst + 4 * noise();
    d.nasCpuT = 62 + 12 * burst * fminf((i - 120) / 20.0f, 1) + 1.5f * noise();
    d.nasGpu = 5 + 40 * burst + 3 * noise();  // Jellyfin transcode during the download
    d.nasGpuT = 38 + 14 * burst * fminf((i - 120) / 15.0f, 1) + noise();
    d.nasRam = 15 + 0.5f * noise();
    uiSample(d, st, k % 5 == 4);
  }
  d.pcCpu = 31; d.pcCpuT = 61; d.pcGpu = 94; d.pcGpuT = 68; d.pcRam = 47;
  d.nasCpu = 12; d.nasCpuT = 54; d.nasGpu = 38; d.nasGpuT = 51; d.nasRam = 15;
  if (sc == HOT) {  // every gauge kind past warn (amber) or crit (red)
    d.pcCpu = 88; d.pcCpuT = 92; d.pcGpu = 99; d.pcGpuT = 81; d.pcRam = 96;
    d.nasCpu = 97; d.nasCpuT = 84; d.nasGpu = 92; d.nasGpuT = 88; d.nasRam = 90;
  } else if (sc == IDLE) {
    d.pcCpu = 3; d.pcCpuT = 36; d.pcGpu = 1; d.pcGpuT = 33; d.pcRam = 21;
    d.nasCpu = 2; d.nasCpuT = 41; d.nasGpu = 0; d.nasGpuT = 34; d.nasRam = 14; d.nasRx = 0.2e6f; d.nasTx = 0.1e6f;
  }

  // Printer: mid-print (hot: paused on a filament runout with an error and Ludicrous speed; idle: finished).
  Printer& p = d.pr;
  strcpy(p.name, "Benchy_PLA_0.2mm_plate_1");
  strcpy(p.fil, "PLA");
  strcpy(p.state, "RUNNING");
  p.stage = 0; p.pct = 42; p.mins = 83; p.layer = 118; p.layers = 284; p.speed = 2;
  p.noz = 219; p.nozT = 220; p.bed = 65; p.bedT = 65;
  p.fan[0] = 100; p.fan[1] = 40; p.fan[2] = 30;
  if (sc == HOT) { strcpy(p.state, "PAUSE"); p.stage = 6; p.err = 0x07008011; p.speed = 4; p.fan[0] = 0; }
  // Camera: a real frame if one was saved (.pio/preview/camera.jpg, not in the repo: it shows your printer).
  // Without it the page shows its "connecting" placeholder.
  if (FILE* f = fopen(".pio/preview/camera.jpg", "rb")) {
    static uint8_t jpg[256 * 1024];
    size_t n = fread(jpg, 1, sizeof jpg, f);
    fclose(f);
    LGFX_Sprite pic;  // the same path the firmware's camera task takes
    if (uiCamDecode(pic, jpg, n)) uiCamFrame(pic);
  }
  if (sc == IDLE) {
    strcpy(p.state, "FINISH");
    p.pct = 100; p.mins = 0; p.layer = p.layers; p.noz = 28; p.nozT = 0; p.bed = 27; p.bedT = 0;
    p.fan[0] = p.fan[1] = p.fan[2] = 0;
  }

  const struct { const char* name; float pct, temp, tb; } disks[] = {
      {"disk1", 91, 38, 12}, {"disk2", 84, 36, 12}, {"disk3", 77, NAN, 12}, {"disk4", 62, NAN, 12},
      {"disk5", 40, 35, 12}, {"disk6", 12, NAN, 12}, {"cache", 46, 47, 2}};
  for (auto& k : disks) {
    Disk& x = d.disks[d.nDisks++];
    strncpy(x.name, k.name, sizeof x.name);
    x.pct = k.pct; x.temp = k.temp; x.sizeTB = k.tb;
    if (sc == HOT) { x.pct = fminf(k.pct + 6, 99); if (k.temp > 0) x.temp = k.temp + 13; }  // 97% red, 53 C red
    if (sc == IDLE) x.temp = NAN;  // all spun down
  }
  d.arraySizeKB = 6 * 12e9;
  d.arrayFreeKB = d.arraySizeKB * 0.356;

  const struct { const char* name; const char* status; const char* up; float cpu, mb; } ctrs[] = {
      {"binhex-official-pihole", "healthy", "a week", 0.3f, 610}, {"binhex-radarr", "healthy", "a week", 0.1f, 577},
      {"binhex-sabnzbd", "healthy", "a week", 18.4f, 251}, {"binhex-sonarr", "healthy", "a week", 0.1f, 772},
      {"Glances", "running", "45 mins", 0.8f, 136}, {"Jellyfin", "healthy", "a week", 42.7f, 3480},
      {"overseerr", "running", "a week", 0.0f, 400}, {"scrypted", "exited", "", 0.0f, 0}};
  for (auto& c : ctrs) {
    Container& x = d.ctr[d.nCtr++];
    strncpy(x.name, c.name, sizeof x.name); strncpy(x.status, c.status, sizeof x.status);
    strncpy(x.uptime, c.up, sizeof x.uptime);
    x.cpu = sc == IDLE ? 0 : c.cpu; x.memMB = c.mb;
    if (sc == HOT && c.cpu > 10) x.cpu = 97;
  }

  for (int k = 0; k < 600; k++) {  // ~10 min of disk + container history so their detail views have data
    for (int i = 0; i < d.nDisks; i++) if (d.disks[i].temp > 0) d.disks[i].temp += 0.02f * sinf(k * 0.05f + i);
    d.ctr[5].cpu = sc == IDLE ? 0.1f : sc == HOT ? 90 + 6 * noise() : 30 + 15 * sinf(k * 0.04f) + 5 * noise();  // Jellyfin
    d.ctr[5].memMB = 3200 + k * 0.5f;
    uiSample(d, st, k % 5 == 4);
  }

  // Stream Deck keys, through the same PNG path Companion's images take.
  const struct { const char* l1; const char* l2; uint32_t bg; } keys[DECK_KEYS] = {
      {"SCENE", "Main", 0x1E3A5F}, {"SCENE", "Game", 0x1E3A5F}, {"SCENE", "BRB", 0x1E3A5F}, {"REC", "", 0xC0392B},
      {"LIVE", "", 0x2D2D2D}, {"MIC", "on", 0x1E7F4F}, {"CAM", "on", 0x1E7F4F}, {"DISCORD", "Mute", 0x5865F2},
      {"DISCORD", "Deafen", 0x5865F2}, {"LIGHTS", "Desk", 0xB7791F}, {"LIGHTS", "Off", 0x3A3A3A},
      {"SPOTIFY", "Next", 0x1DB954}, {"VOL", "-", 0x2D2D2D}, {"VOL", "+", 0x2D2D2D}, {"PAGE", "2 >", 0x000000}};
  for (int i = 0; i < DECK_KEYS; i++) {
    LGFX_Sprite k;
    k.setColorDepth(24);
    k.createSprite(DECK_PX, DECK_PX);
    k.fillScreen(keys[i].bg);
    k.setTextColor(0xFFFFFF);
    k.setTextDatum(middle_center);
    k.setFont(&fonts::FreeSansBold12pt7b);
    k.drawString(keys[i].l1, DECK_PX / 2, *keys[i].l2 ? DECK_PX / 2 - 16 : DECK_PX / 2);
    k.setFont(&fonts::FreeSans12pt7b);
    k.drawString(keys[i].l2, DECK_PX / 2, DECK_PX / 2 + 18);
    size_t len = 0;
    void* png = k.createPng(&len, 0, 0, DECK_PX, DECK_PX);
    uiDeckKey(i, (const uint8_t*)png, len, true);
    free(png);
  }

  tm clock = {};
  clock.tm_hour = 21; clock.tm_min = 47; clock.tm_mday = 10; clock.tm_mon = 9; clock.tm_year = 126; clock.tm_wday = 6;
  clock.tm_isdst = -1;
  mktime(&clock);  // fills in tm_yday (and tm_isdst), as the panel's clock has them
  // Weather radar at the regional zoom: base map and a NOAA frame from .pio/preview/radar/ (saved tiles and
  // radar_<zoom>.png, not in the repo), composed by the same ui.cpp code the firmware's radar task uses. Without
  // them the radar screen shows its "Loading" state.
  settings.radarZoom = 1;
  {
    static LGFX_Sprite base, labels, pic;
    const int z = RADAR_ZOOMS[settings.radarZoom];
    char path[64];
    snprintf(path, sizeof path, ".pio/preview/radar/radar_%d.png", z);
    static uint8_t png[512 * 1024];
    FILE* f = fopen(path, "rb");
    if (f && uiRadarLayer(base, settings.radarLat, settings.radarLon, z, baseTile, 0x242424u) &&
        uiRadarLayer(labels, settings.radarLat, settings.radarLon, z, labelTile, RADAR_CLEAR)) {
      size_t n = fread(png, 1, sizeof png, f);
      tm t = clock;
      t.tm_min -= 4;  // NOAA's latest frame is a few minutes old
      t.tm_isdst = -1;  // let mktime work out DST, or the time is an hour off half the year
      if (uiRadarCompose(pic, base, png, n, &labels))
        uiRadarFrame(pic, mktime(&t), 9, 10);
    }
    if (f) fclose(f);
  }
  {  // Local weather on the radar screen; the hot scenario adds a warning and a watch (the banner, the alert sheet).
    static Weather w;  // static: ~5 KB
    w.have = true;
    w.tempF = sc == HOT ? 97 : 82; w.windMph = 8; w.humidity = 51;
    strcpy(w.sky, "Mostly Cloudy"); strcpy(w.windDir, "SE");
    w.periods[0] = {"This Afternoon", 85, "Mostly Sunny", 7};
    w.periods[1] = {"Tonight", 63, "Chance Showers And Thunderstorms", 40};
    w.nPeriods = 2;
    w.alertsKnown = true;
    if (sc == HOT) {
      tm e = clock;
      e.tm_hour += 1; e.tm_min = 15; e.tm_isdst = -1;
      w.alerts[0] = {"Severe Thunderstorm Warning", "Severe", mktime(&e),
                     "Severe Thunderstorm Warning until 10:15 PM CDT for northern Bexar County\n"
                     "* WHAT...A severe thunderstorm capable of producing 60 mph wind gusts and quarter size hail.\n"
                     "* WHERE...Northern Bexar County, including Universal City and Live Oak.\n"
                     "* IMPACTS...Hail damage to vehicles is expected. Expect wind damage to roofs, siding and trees.\n"
                     "For your protection move to an interior room on the lowest floor of a building."};
      e = clock;
      e.tm_mday += 1; e.tm_hour = 7; e.tm_min = 0; e.tm_isdst = -1;
      w.alerts[1] = {"Flood Watch", "Moderate", mktime(&e),
                     "Flood Watch in effect through Sunday morning\n"
                     "* WHAT...Flash flooding caused by excessive rainfall is possible.\n"
                     "* WHERE...Portions of south central Texas."};
      w.nAlerts = 2;
    }
    uiRadarWeather(w);
  }
  // Spotify: a track mid-way (hot: paused, liked, shuffle and repeat-one on; offline: nothing playing). Album art
  // from .pio/preview/spotify.jpg if saved (not in the repo), through the same decode the firmware's task uses.
  if (sc != OFFLINE) {
    Spotify s;
    s.active = true;
    s.playing = sc != HOT;
    s.liked = s.shuffle = sc == HOT;
    s.repeat = sc == HOT ? 2 : 1;
    s.volume = 64;
    s.canVolume = sc != IDLE;
    s.progressMs = 83000;
    s.durationMs = 247000;
    strcpy(s.id, "4uLU6hMCjMI75M1A2tKUQC");
    // idle: Japanese, from the microSD card's fonts (and a title long enough to wrap between characters)
    strcpy(s.title, sc == HOT ? "A Rather Long Song Title That Needs Two Lines To Fit" : sc == IDLE ? "夜に駆ける、あの日の約束を忘れないように"
                                                                                        : "Midnight City");
    strcpy(s.artist, sc == IDLE ? "YOASOBI" : "M83");
    strcpy(s.album, sc == IDLE ? "THE BOOK  /  Café Sessions" : "Hurry Up, We're Dreaming");
    strcpy(s.device, "WINTERMUTE");
    uiSpotify(s);
    if (FILE* f = fopen(".pio/preview/spotify.jpg", "rb")) {
      static uint8_t jpg[256 * 1024];
      size_t n = fread(jpg, 1, sizeof jpg, f);
      fclose(f);
      LGFX_Sprite art;
      uint32_t tint;
      if (uiSpotifyDecode(art, jpg, n, tint)) uiSpotifyArt(art, tint);
      LGFX_Sprite cover;  // every library item shares the sample cover, but Liked Songs (a placeholder)
      if (uiSpotifyCoverDecode(cover, jpg, n))
        for (int i = 0; i < SPOTIFY_ITEMS; i++) if (i != 1) uiSpotifyCover(i, cover);
    }
    SpotifyLibrary lib;
    const char* const items[][2] = {{"Hurry Up, We're Dreaming", "M83"}, {"Liked Songs", "Playlist"},
        {"Late Night Coding", "You"}, {"M83", "Artist"}, {"Random Access Memories", "Daft Punk"}, {"Synthwave Drive", "You"},
        {"Late Night Coding", "You"}, {"Synthwave Drive", "You"}, {"Workout", "You"}, {"Chill Sunday", "You"},
        {"Road Trip 2026", "You"}, {"Focus", "You"}};
    for (int i = 0; i < SPOTIFY_ITEMS; i++) { strcpy(lib.item[i].name, items[i][0]); strcpy(lib.item[i].sub, items[i][1]); }
    lib.known = true; lib.nRecent = SPOTIFY_RECENT; lib.nLists = SPOTIFY_LISTS;
    uiSpotifyLibrary(lib);
    for (int i = 0; i < SPOTIFY_ITEMS; i++) if (i != 1) {  // uiSpotifyLibrary clears the covers: hand them again
      if (FILE* f = fopen(".pio/preview/spotify.jpg", "rb")) {
        static uint8_t jpg[256 * 1024];
        size_t n = fread(jpg, 1, sizeof jpg, f);
        fclose(f);
        static LGFX_Sprite cover;
        if (i == 0 && !uiSpotifyCoverDecode(cover, jpg, n)) break;
        uiSpotifyCover(i, cover);
      }
    }
    SpotifyDevices devs;
    const struct { const char* name; const char* type; bool active; int vol; } dv[] = {
        {"WINTERMUTE", "Computer", true, 64}, {"Pixel 9", "Smartphone", false, -1}, {"Living Room", "Speaker", false, 35},
        {"Office TV", "TV", false, 20}};
    for (auto& x : dv) {
      SpotifyDevice& e = devs.d[devs.n++];
      strcpy(e.name, x.name); strcpy(e.type, x.type); e.active = x.active; e.volume = x.vol;
    }
    devs.known = true;
    uiSpotifyDevices(devs);
  }
  // Pages, plus detail views opened by simulated taps (tapX < 0 = no tap) on the page as drawn,
  // which also exercises the tap targets: first gauge, first disk row, first container row.
  const struct { Page page; int tapX, tapY; const char* file; int tap2X = -1, tap2Y = 0; } shots[] = {
      {PAGE_PC, -1, 0, "preview.png"}, {PAGE_NAS, -1, 0, "preview-nas.png"},
      {PAGE_STORAGE, -1, 0, "preview-storage.png"}, {PAGE_DOCKER, -1, 0, "preview-docker.png"},
      {PAGE_PRINTER, -1, 0, "preview-printer.png"},
      {PAGE_PC, 200, 260, "preview-detail-gauge.png"},       // the CPU load lane
      {PAGE_PRINTER, 830, 230, "preview-detail-printer.png"},  // the nozzle card
      {PAGE_PRINTER, 300, 200, "preview-camera.png"},          // the camera image: full-screen view
      {PAGE_STORAGE, 130, 280, "preview-detail-disk.png"},
      {PAGE_DOCKER, 300, 130, "preview-detail-container.png"},
      {PAGE_PC, 990, 28, "preview-settings.png"},  // the gear, far right of the status bar
      {PAGE_PC, 990, 28, "preview-settings-function.png", 880, 104},  // then the Function dropdown
      {PAGE_COUNT, -1, 0, "preview-launcher.png"},     // PAGE_COUNT = the launcher
      {PAGE_COUNT, 510, 470, "preview-macro.png"},     // launcher, then the Macro Pad tile
      {PAGE_COUNT, 850, 470, "preview-deck.png"},      // launcher, then the Stream Deck tile
      {PAGE_COUNT, 170, 470, "preview-spotify.png"},   // launcher, then the Spotify tile
      {PAGE_COUNT, 170, 470, "preview-spotify-volume.png", 853, 554},  // then its volume button: the slider
      {PAGE_COUNT, 170, 470, "preview-spotify-library.png", 57, 554},  // then the library button
      {PAGE_COUNT, 170, 470, "preview-spotify-devices.png", 967, 554}, // then the devices button
      {PAGE_COUNT, 760, 300, "preview-radar.png"},     // launcher, then the Weather Radar tile
      {PAGE_COUNT, 760, 300, "preview-radar-location.png", 80, 88},  // then its place chip: the location sheet
      {PAGE_COUNT, 760, 300, "preview-radar-alert.png", 480, 88}};   // then the alert banner (hot): the alert sheet
  for (auto& shot : shots) {
    uiShowPage(shot.page);
    SysInfo sys = {-58, "192.168.1.50", 3 * 86400 + 4 * 3600, 402, 31200, {2, 1, 14, 6, 1}};
    if (sc == OFFLINE) sys.ageS[0] = sys.ageS[3] = sys.ageS[4] = 372;
    if (sc == IDLE) { clock.tm_hour = 3; clock.tm_min = 12; }
    const tm* clk = sc == OFFLINE ? nullptr : &clock;
    bool night = sc == NIGHT;
    st.usb = st.deck = st.cam = sc != OFFLINE;
    uiDraw(d, st, clk, night, sys);
    if (shot.tapX >= 0) { uiTap(shot.tapX, shot.tapY); uiDraw(d, st, clk, night, sys); }
    if (shot.tap2X >= 0) { uiTap(shot.tap2X, shot.tap2Y); uiDraw(d, st, clk, night, sys); }
    char file[512];
    snprintf(file, sizeof file, "%s%s", prefix, shot.file);
    size_t len = 0;
    void* png = spr.createPng(&len, 0, 0, W, H);
    FILE* f = fopen(file, "wb");
    if (!png || !f) { fprintf(stderr, "PNG write failed\n"); return 1; }
    fwrite(png, 1, len, f);
    fclose(f);
    free(png);
    printf("wrote %s (%zu bytes)\n", file, len);
  }
  return 0;
}
