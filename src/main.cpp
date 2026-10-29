// Homelab status panel: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (ESP32-P4 + C6 for Wi-Fi, 1024x600 MIPI-DSI).
// Polls LibreHardwareMonitor (desktop), Glances + Unraid API (NAS) over Wi-Fi; ui.cpp draws the dashboard.
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "psram_json.h"
#include <Wire.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <algorithm>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_ldo_regulator.h>
#include <mbedtls/platform.h>
#include "config.h"
#include "ui.h"
#include "companion.h"
#include "printer.h"
#include "radar.h"
#include "spotify.h"

// ---------- shared data (fetch task writes, UI reads) ----------
Data shared;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t fetchGen = 0;  // bumped after each fetch round (even a failed one): loop() redraws on change

// ---------- fetching ----------
// One kept-alive (HTTP/1.1) connection per server polled every round. Opening a new one for each of a round's
// ~9 requests loaded LHM's small web server enough that it dropped the odd one. The Unraid API (every 30 s) doesn't
// bother.
struct Conn { WiFiClient net; HTTPClient http; };
static Conn lhmConn, glancesConn;

// conn: reuse that server's connection. filter: keep only these fields while parsing.
static bool getJson(const String& url, JsonDocument& doc, const char* body = nullptr, int* status = nullptr,
                    Conn* conn = nullptr, const JsonDocument* filter = nullptr, uint16_t timeoutMs = 2000) {
  HTTPClient local;
  HTTPClient& http = conn ? conn->http : local;
  int code = -1;
  for (int attempt = 0; attempt < 2; attempt++) {
    bool reused = conn && conn->net.connected();
    http.setConnectTimeout(800);
    http.setTimeout(timeoutMs);
    http.setReuse(conn != nullptr);
    if (!(conn ? http.begin(conn->net, url) : http.begin(url))) return false;
    if (body) {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("x-api-key", UNRAID_API_KEY);
      code = http.POST(body);
    } else {
      code = http.GET();
    }
    if (code > 0 || !reused) break;
    http.end();  // the server had closed the kept connection while idle: reconnect once
    conn->net.stop();
  }
  if (status) *status = code;
  bool ok = code == 200 && !(filter ? deserializeJson(doc, http.getString(), DeserializationOption::Filter(*filter))
                                    : deserializeJson(doc, http.getString()));
  http.end();
  if (!ok) Serial.printf("%s -> HTTP %d\n", url.c_str(), code);
  return ok;
}

static float num(JsonVariantConst v) {  // GraphQL BigInt may arrive as a string
  if (v.isNull()) return NAN;
  return v.is<const char*>() ? atof(v.as<const char*>()) : v.as<float>();
}

// GET replies are {"value":..,"min":..,"max":..}. A bad sensor ID only blanks that gauge.
// A connection failure (code < 0) keeps the previous value: LHM drops the odd request when several panels poll it,
// and the 15 s pcOk check in loop() still blanks the page if the PC is really gone.
static float lhm(const char* id, bool& ok, float prev) {
  if (!ok) return prev;  // host already failed this round, don't stack timeouts
  JsonDocument doc(&psram);
  int code = -1;
  bool got = getJson(String("http://" PC_HOST ":") + PC_PORT + "/Sensor?action=Get&id=" + id, doc, nullptr, &code,
                     &lhmConn);
  ok = code > 0;
  if (!ok) return prev;
  return got ? num(doc["value"]) : NAN;
}

static void fetchPC(Data& d) {
  bool ok = true;
  d.pcCpu = lhm(LHM_CPU_LOAD, ok, d.pcCpu);
  d.pcCpuT = lhm(LHM_CPU_TEMP, ok, d.pcCpuT);
  d.pcGpu = lhm(LHM_GPU_LOAD, ok, d.pcGpu);
  d.pcGpuT = lhm(LHM_GPU_TEMP, ok, d.pcGpuT);
  d.pcRam = lhm(LHM_RAM_LOAD, ok, d.pcRam);
  if (ok) d.pcOk = millis();
}

static void fetchGlances(Data& d) {
  const String base = String("http://" NAS_HOST ":") + GLANCES_PORT + "/api/4/";
  JsonDocument doc(&psram);
  if (!getJson(base + "quicklook", doc, nullptr, nullptr, &glancesConn)) return;
  d.nasCpu = num(doc["cpu"]);
  d.nasRam = num(doc["mem"]);

  if (getJson(base + "network/interface_name/value/" NAS_IFACE, doc, nullptr, nullptr, &glancesConn)) {
    // Rates from Glances' cumulative byte counters over this panel's own polling interval. Its *_rate_per_sec fields
    // cover the time since *any* client last asked, so with two panels polling in step, one got a few-ms window and
    // a rate that was mostly noise.
    JsonVariantConst n = doc[NAS_IFACE][0];
    static double lastRx = -1, lastTx = -1;
    static uint32_t lastMs = 0;
    double rx = n["bytes_recv_gauge"] | -1.0, tx = n["bytes_sent_gauge"] | -1.0;
    uint32_t now = millis();
    if (rx >= lastRx && tx >= lastTx && lastRx >= 0 && now - lastMs >= 500) {  // skips the first poll and counter resets
      float dt = (now - lastMs) / 1000.0f;
      d.nasRx = (rx - lastRx) * 8 / dt;
      d.nasTx = (tx - lastTx) * 8 / dt;
    }
    lastRx = rx; lastTx = tx; lastMs = now;
  }
  // Temperatures change slowly and each graph point averages several readings: every third round (6 s) is plenty.
  // Between fetches the last values carry over (the fetch task starts each round from shared).
  static uint8_t rounds = 0;
  if (rounds++ % 3) { d.nasOk = millis(); return; }
  static JsonDocument sensorFilter;  // a full sensors list is several KB; three fields of each entry are used
  if (sensorFilter.isNull()) for (const char* k : {"type", "label", "value"}) sensorFilter[0][k] = true;
  if (getJson(base + "sensors", doc, nullptr, nullptr, &glancesConn, &sensorFilter)) {
    float fallback = NAN, t = NAN;
    for (JsonVariantConst s : doc.as<JsonArrayConst>()) {
      if (s["type"] != "temperature_core") continue;
      if (s["label"] == NAS_CPU_TEMP_LABEL) t = num(s["value"]);
      if (isnan(fallback)) fallback = num(s["value"]);
    }
    d.nasCpuT = isnan(t) ? fallback : t;
  }
  // GPU (RTX 3080): Glances only sees it when its container runs with --runtime=nvidia; otherwise "[]".
  d.nasGpu = d.nasGpuT = NAN;
  if (getJson(base + "gpu", doc, nullptr, nullptr, &glancesConn) && doc[0]) {
    d.nasGpu = num(doc[0]["proc"]);
    d.nasGpuT = num(doc[0]["temperature"]);
  }
  d.nasOk = millis();
}

static void fetchDocker(Data& d) {
  JsonDocument filter, doc(&psram);
  for (const char* k : {"name", "status", "uptime", "cpu_percent", "cpu_limit", "memory_usage", "memory_inactive_file"})
    filter[0][k] = true;
  if (!getJson(String("http://" NAS_HOST ":") + GLANCES_PORT + "/api/4/containers", doc, nullptr, nullptr, &glancesConn,
               &filter, 3000))  // docker stats can be slow
    return;
  d.nCtr = 0;
  for (JsonVariantConst c : doc.as<JsonArrayConst>()) {
    if (d.nCtr >= MAX_CTR) break;
    Container& x = d.ctr[d.nCtr++];
    strlcpy(x.name, c["name"] | "?", sizeof x.name);
    strlcpy(x.status, c["status"] | "?", sizeof x.status);
    strlcpy(x.uptime, c["uptime"] | "", sizeof x.uptime);
    // Glances reports CPU like `docker stats`: 100% = one thread, so up to 1200% on the 12-thread 5600X.
    // Divide by the threads the container may use (cpu_limit) to get a share of the whole CPU, like the NAS gauge.
    x.cpu = (c["cpu_percent"] | 0.0f) / std::max(1.0f, c["cpu_limit"] | 1.0f);
    x.memMB = ((c["memory_usage"] | 0.0) - (c["memory_inactive_file"] | 0.0)) / 1048576;  // like `docker stats`: minus page cache
  }
  std::sort(d.ctr, d.ctr + d.nCtr, [](const Container& a, const Container& b) { return strcasecmp(a.name, b.name) < 0; });
  d.dockerOk = millis();
}

static void fetchUnraid(Data& d) {
  if (!*UNRAID_API_KEY) return;
  JsonDocument doc(&psram);
  const char* q =
      R"({"query":"{array{disks{name status temp fsSize fsFree} caches{name status temp fsSize fsFree}}}"})";
  if (!getJson("http://" NAS_HOST "/graphql", doc, q) || doc["data"]["array"].isNull()) return;
  d.nDisks = 0;
  d.arraySizeKB = d.arrayFreeKB = 0;
  for (const char* group : {"disks", "caches"}) {
    for (JsonVariantConst k : doc["data"]["array"][group].as<JsonArrayConst>()) {
      float size = num(k["fsSize"]), freeKB = num(k["fsFree"]);
      if (k["status"] == "DISK_NP" || !(size > 0) || d.nDisks >= MAX_DISKS) continue;
      Disk& x = d.disks[d.nDisks++];
      strlcpy(x.name, k["name"] | "?", sizeof x.name);
      x.pct = 100.0f * (size - freeKB) / size;
      x.sizeTB = size / 1e9f;
      x.temp = num(k["temp"]);  // null/NaN when spun down
      if (group[0] == 'd') { d.arraySizeKB += size; d.arrayFreeKB += freeKB; }
    }
  }
  d.unraidOk = millis();
}

static void fetchTask(void*) {
  Data d;
  uint32_t lastUnraid = 0, lastDocker = 0;
  bool wasUp = false;
  for (;;) {
    uint32_t t0 = millis();
    bool up = WiFi.status() == WL_CONNECTED;
    if (up != wasUp) Serial.println(up ? "Wi-Fi up, IP " + WiFi.localIP().toString() : String("Wi-Fi down"));
    wasUp = up;
    if (up) {
      portENTER_CRITICAL(&mux); d = shared; portEXIT_CRITICAL(&mux);
      fetchPC(d);
      fetchGlances(d);
      if (!lastUnraid || millis() - lastUnraid > 30000) { lastUnraid = millis(); fetchUnraid(d); }
      if (!lastDocker || millis() - lastDocker > 10000) { lastDocker = millis(); fetchDocker(d); }
      portENTER_CRITICAL(&mux); shared = d; portEXIT_CRITICAL(&mux);
    }
    fetchGen++;  // also without Wi-Fi, so the header's "Wi-Fi connecting..." shows promptly
    // Once NTP has synced, poll on even wall-clock seconds so side-by-side panels show the same sample.
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int32_t wait = tv.tv_sec > 1700000000 ? int32_t(2000 - (tv.tv_sec % 2) * 1000 - tv.tv_usec / 1000)
                                          : 2000 - int32_t(millis() - t0);
    vTaskDelay(pdMS_TO_TICKS(wait > 100 ? wait : 100));
  }
}

// ---------- display: double-buffered, LovyanGFX sprite draws straight into the back frame buffer ----------
esp_lcd_panel_handle_t panel;
void* fbs[2];
int back = 0;
LGFX_Sprite spr;

// EK79007 over 2 MIPI-DSI lanes. Settings from Waveshare's Arduino examples for this board
// (waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B, libraries/displays) and ESPHome's model entry.
constexpr int BL_PIN = 32;  // backlight PWM, inverted (low = bright)

static void initPanel() {
  esp_ldo_channel_handle_t ldo;  // LDO channel 3 powers the MIPI D-PHY at 2.5 V
  esp_ldo_channel_config_t ldoCfg = {};
  ldoCfg.chan_id = 3;
  ldoCfg.voltage_mv = 2500;
  Serial.printf("display: PSRAM %u KB free, heap %u KB free\n", ESP.getFreePsram() / 1024, ESP.getFreeHeap() / 1024);
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldoCfg, &ldo));
  Serial.println("display: LDO ok");

  pinMode(33, OUTPUT);  // panel reset: low 10 ms, then 20 ms before commands
  digitalWrite(33, LOW);
  delay(10);
  digitalWrite(33, HIGH);
  delay(20);

  esp_lcd_dsi_bus_config_t busCfg = {};
  busCfg.bus_id = 0;
  busCfg.num_data_lanes = 2;
  busCfg.phy_clk_src = (mipi_dsi_phy_pllref_clock_source_t)0;  // 0 = let IDF pick (differs on rev 3.x silicon)
  busCfg.lane_bit_rate_mbps = 1000;
  esp_lcd_dsi_bus_handle_t bus;
  ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&busCfg, &bus));
  Serial.println("display: DSI bus ok");

  esp_lcd_dbi_io_config_t dbiCfg = {};
  dbiCfg.virtual_channel = 0;
  dbiCfg.lcd_cmd_bits = 8;
  dbiCfg.lcd_param_bits = 8;
  esp_lcd_panel_io_handle_t io;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(bus, &dbiCfg, &io));

  esp_lcd_dpi_panel_config_t c = {};
  c.virtual_channel = 0;
  c.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  c.dpi_clock_freq_mhz = 52;
  c.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
  c.num_fbs = 2;
  c.video_timing.h_size = W;
  c.video_timing.v_size = H;
  c.video_timing.hsync_pulse_width = 10; c.video_timing.hsync_back_porch = 160; c.video_timing.hsync_front_porch = 160;
  c.video_timing.vsync_pulse_width = 1;  c.video_timing.vsync_back_porch = 23;  c.video_timing.vsync_front_porch = 12;
  c.flags.use_dma2d = true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(bus, &c, &panel));
  Serial.println("display: DPI panel ok");

  static const struct { uint8_t cmd, val; } vendor[] = {
      {0xB2, 0x10}, {0x80, 0x8B}, {0x81, 0x78}, {0x82, 0x84}, {0x83, 0x88}, {0x84, 0xA8}, {0x85, 0xE3}, {0x86, 0x88}};
  for (auto& v : vendor) ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, v.cmd, &v.val, 1));
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x11, nullptr, 0));  // sleep out
  delay(120);
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x29, nullptr, 0));  // display on
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &fbs[0], &fbs[1]));
  Serial.println("display: panel on");

  spr.setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
  spr.setBuffer(fbs[back], W, H);
  ledcAttach(BL_PIN, 5000, 8);  // Waveshare's frequency for this backlight driver
  ledcOutputInvert(BL_PIN, true);
}

static bool isNight(const tm* t) {  // false until the clock syncs, or with night mode off
  if (!t || !settings.nightOn) return false;
  int h = t->tm_hour, from = settings.nightFrom, to = settings.nightTo;
  return from > to ? (h >= from || h < to) : (h >= from && h < to);
}

// ---------- settings persistence (NVS) ----------
Preferences prefs;

static void loadSettings() {
  prefs.begin("panel", false);
  // Load by length: a copy saved before newer fields were appended fills the start and the rest keep
  // their defaults.
  size_t n = prefs.isKey("settings") ? prefs.getBytesLength("settings") : 0;
  Settings saved = settings;
  if (n >= 1 && n <= sizeof saved && prefs.getBytes("settings", &saved, n) == n && saved.version == settings.version)
    settings = saved;
  // Saved before Function existed: "Launcher" used to be a start page (PAGE_COUNT). It's a Function now.
  if (n <= offsetof(Settings, function) && settings.startPage == PAGE_COUNT) settings.function = FN_LAUNCHER;
  settings.startPage %= PAGE_COUNT;
  settings.function %= FN_COUNT;
  settings.radarZoom %= N_RADAR_ZOOMS;
  settings.radarZip[sizeof settings.radarZip - 1] = settings.radarPlace[sizeof settings.radarPlace - 1] = 0;
}

// ---------- macro pad: USB keyboard on the USB-A (OTG HS) port ----------
static USBHIDKeyboard keyboard;
static USBHIDConsumerControl mediaKeys;

static void sendMacro(int i) {
  const Macro& m = MACROS[i];
  Serial.printf("macro: %s%s\n", m.label, USB ? "" : " (USB not connected)");
  if (!USB) return;
  if (m.kind == MK_MEDIA) { mediaKeys.press(m.code); mediaKeys.release(); return; }
  KeyReport r = {m.mods, 0, {uint8_t(m.code)}};
  keyboard.sendReport(&r);
  delay(15);  // a press the PC can see
  keyboard.releaseAll();
}

static void saveSettings() {
  prefs.putBytes("settings", &settings, sizeof settings);
  Serial.println("settings: saved");
}

static void present() {  // show the frame we just drew, draw the next one into the other buffer
  esp_lcd_panel_draw_bitmap(panel, 0, 0, W, H, fbs[back]);
  back ^= 1;
  spr.setBuffer(fbs[back], W, H);
}

// ---------- touch: GT911 over I2C, polled. Only taps matter, so no coordinates ----------
uint8_t gtAddr = 0;
bool touchOk = false;

static bool gtRead(uint16_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;  // repeated start into the read
  if (Wire.requestFrom(gtAddr, n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static bool gtWrite(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

// SDA 7, SCL 8, 400 kHz (the GT911's fast mode: each 20 ms poll takes a quarter of the bus time it did at
// Waveshare's 100 kHz; go back to 100000 if touches start dropping). RST/INT aren't wired to the P4, so just give
// the GT911 time to boot (Waveshare's Arduino examples do the same) and probe both addresses.
static void resetTouch() {
  Wire.begin(7, 8, 400000);
  Wire.setTimeOut(20);
  delay(200);
}

static void logTouchInfo() {  // diagnostic: identity and config; a GT911 with an empty config never scans
  uint8_t id[11] = {}, cfg[7] = {};
  bool idOk = gtRead(0x8140, id, 10), cfgOk = gtRead(0x8047, cfg, 7);
  Serial.printf("touch: id %s \"%.4s\" fw 0x%02X%02X sensor %ux%u\n", idOk ? "ok" : "FAILED", id, id[5], id[4],
                id[6] | id[7] << 8, id[8] | id[9] << 8);
  Serial.printf("touch: config %s version 0x%02X output %ux%u max touches %u\n", cfgOk ? "ok" : "FAILED", cfg[0],
                cfg[1] | cfg[2] << 8, cfg[3] | cfg[4] << 8, cfg[5] & 0x0F);
}

// Found = its product ID register (0x8140) reads back "911". No more false positives from a bare ACK.
static bool startTouch() {
  touchOk = false;
  for (uint8_t a : {0x5D, 0x14}) {
    gtAddr = a;
    uint8_t id[5] = {};
    bool ok = gtRead(0x8140, id, 4);
    Serial.printf("touch: product ID at 0x%02X: %s \"%s\"\n", a, ok ? "read" : "failed", id);
    if (ok && !memcmp(id, "911", 3)) { touchOk = true; break; }
  }
  Serial.printf("touch: GT911 %s\n", touchOk ? (gtAddr == 0x5D ? "at 0x5D" : "at 0x14") : "not found, retrying every 30 s");
  if (touchOk) logTouchInfo();
  return touchOk;
}

// Gestures. The GT911 reports the finger's position every scan while it's down, but now and then reports
// "no touch" mid-press; treating those as lifts turned one tap into several. So a gesture only ends once
// no touch has been seen for 80 ms, and is then classed as a tap (barely moved) or a swipe (mostly sideways).
enum Gesture { GEST_NONE, GEST_TAP, GEST_SWIPE_LEFT, GEST_SWIPE_RIGHT, GEST_SWIPE_UP, GEST_DRAG };  // DRAG: neither

static Gesture readGesture(int& x, int& y, int& ex) {  // x, y: where it started; ex: where it ended
  static bool down = false;
  static int sx, sy, lx, ly;
  static uint32_t t0, lastSeen, retryAt = 3000;
  if (!touchOk) {
    if (millis() < retryAt) return GEST_NONE;
    retryAt = millis() + 30000;  // slow: each failed attempt logs I2C errors
    if (!startTouch()) return GEST_NONE;
  }
  // Status register 0x814E: bit 7 = new report, low nibble = touch count; first point's x/y at 0x8150.
  uint8_t st, pt[4];
  bool touching = false;
  if (gtRead(0x814E, &st, 1) && (st & 0x80)) {
    touching = (st & 0x0F) > 0 && gtRead(0x8150, pt, 4);
    gtWrite(0x814E, 0);
  }
  uint32_t now = millis();
  if (touching) {
    int px = pt[0] | pt[1] << 8, py = pt[2] | pt[3] << 8;
    if (TOUCH_FLIP_X) px = W - 1 - px;
    if (TOUCH_FLIP_Y) py = H - 1 - py;
    if (!down) { down = true; sx = px; sy = py; t0 = now; }
    lx = px; ly = py; lastSeen = now;
    return GEST_NONE;
  }
  if (!down || now - lastSeen < 80) return GEST_NONE;  // still pressed, or never was
  down = false;
  int dx = lx - sx, dy = ly - sy;
  Gesture g = abs(dx) >= 80 && abs(dx) > 2 * abs(dy) ? (dx < 0 ? GEST_SWIPE_LEFT : GEST_SWIPE_RIGHT)
            : dy <= -80 && -dy > 2 * abs(dx)         ? GEST_SWIPE_UP
            : abs(dx) < 30 && abs(dy) < 30           ? GEST_TAP
                                                     : GEST_DRAG;  // only a slider takes these
  Serial.printf("touch: %d,%d -> %d,%d in %u ms: %s\n", sx, sy, lx, ly, unsigned(lastSeen - t0),
                g == GEST_TAP ? "tap" : g == GEST_SWIPE_LEFT ? "swipe left" : g == GEST_SWIPE_RIGHT ? "swipe right" : g == GEST_SWIPE_UP ? "swipe up" : "drag");
  x = sx; y = sy; ex = lx;
  return g;
}

// ---------- microSD card (docs/SDCARD.md) ----------
// A first probe: mount the card and log what's there. The P4 variant's defaults: slot 0 on its IO_MUX pins, powered
// by on-chip LDO 4. The Wi-Fi co-processor uses the same SD host on slot 1, so this runs after Wi-Fi has started.
// 40 MHz like the Wi-Fi slot: the two slots share the host's clock divider, which the driver switches on every change of
// slot, and at 20 MHz the card timed out under Wi-Fi traffic (sdmmc "handle_idle_state_events unhandled: 00000100",
// then every write failing until a reboot). At 40 MHz nothing switches; writes run at ~6 MB/s.
static void sdProbe() {
  uint32_t t0 = millis();
  // Up to 12 files open: the 7 extra fonts stay open, and the rest of the plan (logs, caches) will want a few.
  if (!SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_HIGHSPEED, 12)) { Serial.printf("sd: no card, or it didn't mount (%u ms)\n", unsigned(millis() - t0)); return; }
  const uint8_t type = SD_MMC.cardType();
  struct Peek : fs::SDMMCFS {  // the card's real clock: SD_MMC keeps the card handle to itself
    static int khz() { return static_cast<Peek&>(SD_MMC)._card->real_freq_khz; }
  };
  Serial.printf("sd: %s card, %llu MB, %llu MB used, %d kHz, mounted in %u ms\n",
                type == CARD_MMC ? "MMC" : type == CARD_SD ? "SD" : type == CARD_SDHC ? "SDHC/SDXC" : "unknown",
                SD_MMC.cardSize() >> 20, SD_MMC.usedBytes() >> 20, Peek::khz(), unsigned(millis() - t0));
  File root = SD_MMC.open("/");
  int n = 0;
  for (File f = root.openNextFile(); f && n < 12; f = root.openNextFile(), n++)
    Serial.printf("sd:   %s%s  %u bytes\n", f.name(), f.isDirectory() ? "/" : "", unsigned(f.size()));
  if (!n) Serial.println("sd:   (empty)");
  t0 = millis();
  const int fonts = uiLoadExtraFonts("/sdcard/fonts");  // Japanese and accents (tools/mkfonts.py sd)
  Serial.printf("sd: %d of 7 extra fonts loaded in %u ms\n", fonts, unsigned(millis() - t0));
  Serial.printf("sd: RAM free %u KB, lowest %u KB\n", ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024);
}

// Files onto the card over Wi-Fi (tools/sd_upload.py), for panels without a card reader nearby. Digest auth as user
// "panel" with the OTA password. A file comes in pieces of up to 1 MB: POST /sd?offset=N&total=T, a multipart upload
// whose file name is the path on the card. Each piece lands in PSRAM and goes onto the card once it's all here, while
// the sender waits for the reply: writing to panel B's (slower) card while Wi-Fi traffic streamed in wedged the link
// to the Wi-Fi chip, which shares the card's SD host, until a reboot. Pieces append to <path>.part, renamed after the
// last, so a broken upload never leaves half a file. DELETE /sd?path= removes a file. Runs in loop(): the screen holds
// still meanwhile. A file that's open (a loaded font) can't be replaced: POST /reboot afterwards.
static const size_t SD_PIECE = 1 << 20;
static uint32_t sdUploadMs = 0;  // last piece: the camera, radar and Spotify stay off for a while after
static WebServer sdWeb(80);
static bool sdWebAuth() {
  if (sdWeb.authenticate("panel", OTA_PASSWORD)) return true;
  sdWeb.requestAuthentication(DIGEST_AUTH, "ServerPanel");
  return false;
}
static bool sdWebCard() {  // answers 503 when no card mounted at boot
  if (SD_MMC.cardType() != CARD_NONE) return true;
  sdWeb.send(503, "text/plain", "no SD card (insert one and reboot)\n");
  return false;
}
static void sdWebBegin() {
  static uint8_t* buf = (uint8_t*)heap_caps_malloc(SD_PIECE, MALLOC_CAP_SPIRAM);
  static size_t len = 0;
  static String target, error;
  sdWeb.on("/sd", HTTP_GET, [] {  // list a folder: name<TAB>size per line
    if (!sdWebAuth() || !sdWebCard()) return;
    File dir = SD_MMC.open(sdWeb.hasArg("path") ? sdWeb.arg("path") : "/");
    if (!dir || !dir.isDirectory()) { sdWeb.send(404, "text/plain", "no such folder\n"); return; }
    String out;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile())
      out += String(f.name()) + (f.isDirectory() ? "/" : "") + "\t" + f.size() + "\n";
    sdWeb.send(200, "text/plain", out);
  });
  sdWeb.on("/sd", HTTP_POST, [] {
    if (!sdWebAuth() || !sdWebCard()) return;
    if (error.length()) sdWeb.send(400, "text/plain", error + "\n");
    else sdWeb.send(200, "text/plain", "ok " + target + "\n");
  }, [] {
    HTTPUpload& u = sdWeb.upload();
    if (u.status == UPLOAD_FILE_START) {
      error = "";
      len = 0;
      target = u.filename;
      if (!sdWeb.authenticate("panel", OTA_PASSWORD)) error = "unauthorized";  // the body arrives before the handler checks
      else if (SD_MMC.cardType() == CARD_NONE) error = "no SD card";  // nothing is buffered; sdWebCard answers
      else if (!target.startsWith("/") || target.indexOf("..") >= 0) error = "bad path " + target;
      else if (!buf) error = "no PSRAM for the buffer";
      else {
        // Other streams off for the upload: their traffic queues in internal RAM too (loop() keeps them off for a
        // while after each piece, see sdUploadMs).
        printerCamStop();
        radarStop();
        spotifyStop();
      }
    } else if (u.status == UPLOAD_FILE_WRITE && !error.length()) {
      if (len + u.currentSize > SD_PIECE) { error = "piece over 1 MB"; return; }
      memcpy(buf + len, u.buf, u.currentSize);
      len += u.currentSize;
    } else if (u.status == UPLOAD_FILE_ABORTED) {
      error = "aborted";
    } else if (u.status == UPLOAD_FILE_END && !error.length()) {
      sdUploadMs = millis();
      const size_t offset = sdWeb.arg("offset").toInt(), total = sdWeb.arg("total").toInt();
      const String partName = target + ".part";
      if (!offset)
        for (int i = target.indexOf('/', 1); i > 0; i = target.indexOf('/', i + 1)) SD_MMC.mkdir(target.substring(0, i));
      File part = SD_MMC.open(partName, offset ? FILE_APPEND : FILE_WRITE);
      if (!part) { error = "can't write " + target; return; }
      if (part.size() != offset) error = "out of order: have " + String(unsigned(part.size())) + " bytes";
      else if (part.write(buf, len) != len) error = "card full or failed";
      part.close();
      if (error.length()) { SD_MMC.remove(partName); return; }
      if (offset + len < total) return;  // more pieces to come
      if (SD_MMC.exists(target) && !SD_MMC.remove(target)) error = "in use, reboot first: " + target;
      else if (!SD_MMC.rename(partName, target)) error = "rename failed";
      Serial.printf("sd: %s %s, %u bytes\n", error.length() ? error.c_str() : "got", target.c_str(), unsigned(total));
    }
  });
  sdWeb.on("/sd", HTTP_DELETE, [] {
    if (!sdWebAuth() || !sdWebCard()) return;
    const String path = sdWeb.arg("path");
    const bool ok = path.startsWith("/") && path.indexOf("..") < 0 && SD_MMC.remove(path);
    sdWeb.send(ok ? 200 : 404, "text/plain", ok ? "removed\n" : "can't remove\n");
  });
  sdWeb.on("/reboot", HTTP_POST, [] {
    if (!sdWebAuth()) return;
    sdWeb.send(200, "text/plain", "rebooting\n");
    delay(200);
    ESP.restart();
  });
  sdWeb.begin();
}

// ---------- main ----------
// TLS memory in PSRAM. This framework build allocates all of mbedTLS in internal RAM (MBEDTLS_INTERNAL_MEM_ALLOC):
// ~35 KB per connection plus ~30 KB during a handshake, which ran internal RAM out and wedged the Wi-Fi link (the
// camera, the radar, then Spotify). Swapping the allocator at runtime does what MBEDTLS_EXTERNAL_MEM_ALLOC would,
// without rebuilding the framework; the crypto drivers already bounce PSRAM buffers for DMA. Internal RAM is the
// fallback if PSRAM is ever full.
static void* tlsCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void setup() {
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);  // before any TLS connection exists
  Serial.begin(115200);
  delay(300);
  Serial.println("boot");
  loadSettings();
  uiHome();
  keyboard.begin();
  mediaKeys.begin();
  USB.productName("ServerPanel Macro Pad");
  USB.begin();
  resetTouch();  // before the display starts
  initPanel();
  delay(300);
  startTouch();
  WiFi.persistent(false);  // no flash writes: flash ops stall PSRAM cache and glitch the panel
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  sdProbe();
  // OTA updates (platformio.ini [ota]): flash over Wi-Fi to OTA_HOST.local. The transfer runs inside loop(), so the
  // screen stays dark (backlight off, see onStart) until the board reboots into the new firmware.
  ArduinoOTA.setHostname(OTA_HOST);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  // Flash writes stall the PSRAM the panel scans out of, so the screen flickers (teal) during an update: go dark
  // instead. loop() sets the backlight again on its next frame if the update fails; on success the board reboots.
  ArduinoOTA.onStart([] { ledcWrite(BL_PIN, 0); Serial.println("OTA: update starting"); });
  ArduinoOTA.onEnd([] { Serial.println("OTA: done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA: error %d\n", e); });
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
  companionBegin();
  printerBegin();
  radarBegin();
  spotifyBegin();
  xTaskCreatePinnedToCore(fetchTask, "fetch", 12288, nullptr, 1, nullptr, 0);
}

void loop() {
  static uint32_t lastSample = 0, lastTick = 0, lastFlip = 0, lastTouch = 0, saveAt = 0;
  static bool asleep = false;
  static Data d;  // static: too big for the loop task's stack now that it holds containers

  static bool otaUp = false;
  if (WiFi.status() == WL_CONNECTED) {  // OTA needs the network: start it on the first connection
    if (!otaUp) { ArduinoOTA.begin(); sdWebBegin(); otaUp = true; Serial.println("OTA: ready at " OTA_HOST ".local"); }
    ArduinoOTA.handle();
    sdWeb.handleClient();
  }

  int tx, ty, ex;
  Gesture g = readGesture(tx, ty, ex);  // polled every 20 ms so touch feels instant; the screen redraws once a second
  bool tap = g != GEST_NONE;
  uint32_t now = millis();
  if (tap) {
    lastTouch = lastFlip = now;
    if (asleep) { asleep = false; uiHome(); }  // waking only wakes, onto the panel's Function
    else if (g == GEST_TAP) {
      uiTap(tx, ty);
      if (int m = uiTakeMacro(); m >= 0) sendMacro(m);
      if (int k = uiTakeDeckPress(); k >= 0) companionPress(k);
    }
    else if (g == GEST_SWIPE_UP) uiShowLauncher();
    else if (uiDrag(tx, ty, ex)) {}  // a sideways drag from a slider sets it, rather than changing page
    else if (g != GEST_DRAG) uiSwipe(g == GEST_SWIPE_LEFT ? 1 : -1);  // swipe left = next page, like a phone
    uint8_t cmd;
    int arg;
    if (uiTakeSpotify(cmd, arg)) spotifyCommand(cmd, arg);
  }
  // Save settings 3 s after the last change, so tapping + a few times is one flash write, not several.
  if (uiTakeSettingsChanged()) saveAt = now + 3000;
  if (saveAt && int32_t(now - saveAt) >= 0) { saveAt = 0; saveSettings(); }
  if (uiTakeRestart()) { saveSettings(); Serial.println("restarting (requested from settings)"); delay(100); ESP.restart(); }

  // tick: once per wall-clock second (once NTP has synced, so panels change together) for history and page rotation.
  static time_t lastSec = 0;
  time_t sec = time(nullptr);
  bool tick = sec > 1700000000 ? sec != lastSec : now - lastTick >= 1000;
  bool deckNew = companionPump();  // Stream Deck key images: redraw as soon as Companion changes one
  // The camera, the radar and Spotify each need a TLS connection (~35 KB of internal RAM) on top of the printer's MQTT
  // one; three at once ran the RAM out and wedged the link to the Wi-Fi co-processor until a reboot. So they take
  // turns: opening one stops the others at once (no 30 s linger), and it only connects once they have closed.
  const bool idle = !asleep && !(sdUploadMs && now - sdUploadMs < 10000);  // and none mid-upload to the card
  const bool camOn = uiCamWanted() && idle, radarOn = uiRadarWanted() && idle, spotOn = uiSpotifyWanted() && idle;
  printerPause(uiRadarApp() && !asleep);  // the radar gets MQTT's TLS RAM too; asleep, the printer's graph keeps going
  if (radarOn || spotOn) printerCamStop();
  if (camOn || spotOn) radarStop();
  if (camOn || radarOn) spotifyStop();
  printerCamWant(camOn && !radarOpen() && !spotifyOpen());
  bool camNew = printerCamPump();  // printer camera: a new frame while it's on screen
  spotifyWant(spotOn && !printerCamOpen() && !radarOpen());
  spotifyBrowse(uiSpotifyLibraryOpen(), uiSpotifyDevicesOpen());
  bool spotNew = spotifyPump();  // Spotify: new state or album art, and the progress bar once a second
  radarWant(radarOn && !printerCamOpen() && !spotifyOpen(), settings.radarLat, settings.radarLon,
            RADAR_ZOOMS[settings.radarZoom]);
  if (const char* zip = uiTakeRadarZip()) radarLookup(zip);
  bool radarNew = radarPump();  // weather radar: the loop's next frame, or a ZIP lookup's answer

  // Redraw only when something on screen may have changed: new data, a camera frame, the minute, a touch. Those
  // that aren't a touch share a ~1/s budget, so a camera frame and a data round in the same second are one frame.
  // Each redraw writes ~1.2 MB into the PSRAM the display scans out of, so skipped ones matter.
  static uint32_t seenFetch = 0, seenPrinter = 0, lastDraw = 0, followAt = 0;
  static time_t seenMin = 0;
  static bool pending = true;
  uint32_t fetched = fetchGen, reported = printerLastMs();
  time_t minute = sec / 60;
  pending |= camNew || radarNew || spotNew || fetched != seenFetch || minute != seenMin || (reported != seenPrinter && uiCamWanted());
  seenFetch = fetched; seenPrinter = reported; seenMin = minute;
  if (tap) followAt = now + 200;  // a tapped key is highlighted until the next frame: draw again to clear it
  bool follow = followAt && int32_t(now - followAt) >= 0;
  bool draw = tap || deckNew || follow || (pending && now - lastDraw >= 900);
  if (!draw && !tick) { delay(20); return; }

  portENTER_CRITICAL(&mux); d = shared; portEXIT_CRITICAL(&mux);
  printerRead(d.pr);  // kept apart from shared: the fetch task writes all of shared back after each round
  auto fresh = [&](uint32_t t, uint32_t maxAge) { return t && now - t < maxAge; };
  Status s{fresh(d.pcOk, 15000), fresh(d.nasOk, 15000), fresh(d.unraidOk, 90000), fresh(d.dockerOk, 45000),
           WiFi.status() == WL_CONNECTED, bool(USB), companionUp(), printerUp(), printerCamUp(), radarUp(), spotifyUp()};

  if (tick) {  // history keeps recording while the screen sleeps
    // Internal RAM is the scarce kind (TLS, the Wi-Fi co-processor link): log each new low, to see what caused it.
    static uint32_t lowLogged = UINT32_MAX;
    uint32_t low = ESP.getMinFreeHeap();
    if (low + 2048 < lowLogged) {
      lowLogged = low;
      Serial.printf("RAM: new low %u KB (free now %u KB)\n", unsigned(low / 1024), unsigned(ESP.getFreeHeap() / 1024));
    }
    lastTick = now;
    lastSec = sec;
    bool commit = now - lastSample >= settings.graphMin * 60000UL / HIST_N;  // 360 points per window
    if (commit) lastSample = now;
    uiSample(d, s, commit);
    if (settings.rotateSec && now - lastFlip >= settings.rotateSec * 1000UL) { uiNextPage(); lastFlip = now; draw = true; }
  }

  if (settings.sleepMin && now - lastTouch >= settings.sleepMin * 60000UL) {
    if (!asleep) { asleep = true; ledcWrite(BL_PIN, 0); Serial.println("screen: sleeping"); }
    return;  // nothing to draw on a dark screen
  }
  if (!draw) return;
  lastDraw = now;
  pending = false;
  if (follow) followAt = 0;

  struct tm t;
  const tm* clock = getLocalTime(&t, 0) ? &t : nullptr;
  bool night = isNight(clock);
  ledcWrite(BL_PIN, night ? settings.nightBright : settings.brightness);

  SysInfo sys{};
  sys.rssi = s.wifi ? WiFi.RSSI() : 0;
  snprintf(sys.ip, sizeof sys.ip, "%s", WiFi.localIP().toString().c_str());
  sys.uptimeS = now / 1000;
  sys.heapKB = ESP.getFreeHeap() / 1024;
  sys.psramKB = ESP.getFreePsram() / 1024;
  const uint32_t oks[5] = {d.pcOk, d.nasOk, d.unraidOk, d.dockerOk, printerLastMs()};
  for (int i = 0; i < 5; i++)  // the printer task can stamp a report just after `now` was read: count that as 0 s
    sys.ageS[i] = !oks[i] ? UINT32_MAX : int32_t(now - oks[i]) > 0 ? (now - oks[i]) / 1000 : 0;
  uiDraw(d, s, clock, night, sys);
  present();
}
