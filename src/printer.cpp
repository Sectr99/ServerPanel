// Bambu printer (P1 series) status from its LAN MQTT broker: TLS on port 8883, user "bblp", password = the LAN access
// code from the printer's network settings. Reports arrive on device/<serial>/report. P1 printers send only the fields
// that changed, so the state is merged field by field, and a "pushall" request on connect fills in everything once.
// The connection runs in a task of its own so a slow or absent printer never stalls the screen or the HTTP fetches;
// loop() copies the latest state out under a lock.
// The camera (P1 "LAN liveview") is a second TLS connection, port 6000: an 80-byte login, then a stream of
// 16-byte headers (JPEG length in the first 3 bytes, little-endian) each followed by a 1280x720 JPEG, about 1/s.
// It has its own task too, and only streams while the camera is on screen, so idle panels don't load the printer.
#include "printer.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "psram_json.h"

static Printer cur;  // this task's working copy, merged from each report
static Printer shared;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t lastMs = 0;
static volatile bool up = false;  // set by this task only, so loop() never touches the socket
static volatile bool paused = false;  // printerPause()
// ponytail: TLS without a cert check (the printer's cert is Bambu-signed); pin their CA if this ever leaves the LAN.
static WiFiClientSecure net;
static PubSubClient mqtt(net);

template <class T> static void take(JsonObjectConst r, const char* k, T& v) {
  if (!r[k].isNull()) v = r[k].as<T>();
}
static void takeStr(JsonObjectConst r, const char* k, char* buf, size_t n) {
  if (const char* s = r[k]) snprintf(buf, n, "%s", s);
}

static JsonDocument filter;  // the fields shown: a full report is 20-30 KB of JSON, of which ~20 values are used

static void onReport(char*, byte* p, unsigned n) {
  JsonDocument doc(&psram);
  if (deserializeJson(doc, p, n, DeserializationOption::Filter(filter))) return;
  JsonObjectConst r = doc["print"];
  if (r.isNull()) return;  // other report types (system, info) carry nothing shown here
  take(r, "nozzle_temper", cur.noz);  take(r, "nozzle_target_temper", cur.nozT);
  take(r, "bed_temper", cur.bed);     take(r, "bed_target_temper", cur.bedT);
  take(r, "mc_percent", cur.pct);     take(r, "mc_remaining_time", cur.mins);
  take(r, "layer_num", cur.layer);    take(r, "total_layer_num", cur.layers);
  take(r, "stg_cur", cur.stage);      take(r, "print_error", cur.err);
  take(r, "spd_lvl", cur.speed);
  takeStr(r, "gcode_state", cur.state, sizeof cur.state);
  takeStr(r, "subtask_name", cur.name, sizeof cur.name);
  takeStr(r["vt_tray"], "tray_type", cur.fil, sizeof cur.fil);
  const char* fanKeys[] = {"cooling_fan_speed", "big_fan1_speed", "big_fan2_speed"};
  for (int i = 0; i < 3; i++)  // sent as strings "0".."15"; Bambu's app shows them in 10% steps
    if (const char* s = r[fanKeys[i]]) cur.fan[i] = (atoi(s) * 100 / 15 + 5) / 10 * 10;
  portENTER_CRITICAL(&mux); shared = cur; portEXIT_CRITICAL(&mux);
  lastMs = millis();
}

static void task(void*) {
  char id[32];
  snprintf(id, sizeof id, "serverpanel-%012llX", (unsigned long long)ESP.getEfuseMac());  // one per panel, or they kick each other off
  for (const char* k : {"nozzle_temper", "nozzle_target_temper", "bed_temper", "bed_target_temper", "mc_percent",
                        "mc_remaining_time", "layer_num", "total_layer_num", "stg_cur", "print_error", "spd_lvl",
                        "gcode_state", "subtask_name", "cooling_fan_speed", "big_fan1_speed", "big_fan2_speed"})
    filter["print"][k] = true;  // keep in step with onReport
  filter["print"]["vt_tray"]["tray_type"] = true;
  net.setInsecure();
  mqtt.setServer(PRINTER_IP, 8883);
  mqtt.setBufferSize(48 * 1024);  // a full "pushall" report is ~20-30 KB
  // PubSubClient waits for a packet's bytes in a loop that never lets the idle task run; its default 15 s would trip
  // the 5 s task watchdog if the printer stalled mid-report.
  mqtt.setSocketTimeout(3);
  mqtt.setCallback(onReport);
  bool wasUp = false;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (paused) {  // the radar is open: give its TLS connection this one's RAM; a pushall catches up afterwards
      if (wasUp) {
        mqtt.disconnect();
        net.stop();  // frees the TLS buffers, whatever state the connection was in
        Serial.println("printer: paused while the radar is open");
      }
      wasUp = up = false;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!mqtt.connected()) {
      if (wasUp) Serial.println("printer: disconnected");
      wasUp = up = false;
      if (!mqtt.connect(id, "bblp", PRINTER_CODE)) {
        Serial.printf("printer: MQTT connect failed (%d)\n", mqtt.state());
        vTaskDelay(pdMS_TO_TICKS(10000));
        continue;
      }
      Serial.printf("printer: connected to " PRINTER_IP " (RAM free %u KB, lowest %u KB)\n", ESP.getFreeHeap() / 1024,
                    ESP.getMinFreeHeap() / 1024);
      wasUp = up = true;
      mqtt.subscribe("device/" PRINTER_SN "/report");
      mqtt.publish("device/" PRINTER_SN "/request", R"({"pushing":{"sequence_id":"0","command":"pushall"}})");
    }
    mqtt.loop();
    vTaskDelay(pdMS_TO_TICKS(50));  // reports come about once a second
  }
}

// ---------- camera ----------
// Frames are decoded here, on core 0, so loop() only copies and scales the finished picture.
static constexpr size_t JPG_MAX = 256 * 1024;  // frames run 45-65 KB
static LGFX_Sprite camPic;                      // the latest frame, decoded at half size; guarded by camLock
static bool camFresh = false;
static SemaphoreHandle_t camLock;
static volatile uint32_t camWantMs = 0, camLastMs = 0;
static volatile bool camOpen = false;  // the TLS connection is open (set by camTask only)
static WiFiClientSecure cam;

static void camClose() { cam.stop(); camOpen = false; }

static bool readN(uint8_t* p, size_t n) {
  uint32_t t = millis();
  while (n) {
    int r = cam.read(p, n);
    if (r > 0) { p += r; n -= r; t = millis(); }
    else if (!cam.connected() || millis() - t > 5000) return false;
    else vTaskDelay(pdMS_TO_TICKS(5));
  }
  return true;
}

static void camTask(void*) {
  uint8_t* jpg = (uint8_t*)ps_malloc(JPG_MAX);
  cam.setInsecure();
  for (;;) {
    bool want = camWantMs && millis() - camWantMs < 30000 && WiFi.status() == WL_CONNECTED;
    if (!want) {
      if (camOpen) { camClose(); Serial.println("printer: camera stopped"); }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!cam.connected()) {
      if (!cam.connect(PRINTER_IP, 6000)) {
        Serial.println("printer: camera connect failed (is LAN Only Liveview on?)");
        vTaskDelay(pdMS_TO_TICKS(10000));
        continue;
      }
      uint8_t login[80] = {0};
      login[0] = 0x40; login[5] = 0x30;  // payload size 0x40, type 0x3000 (little-endian)
      memcpy(login + 16, "bblp", 4);
      memcpy(login + 48, PRINTER_CODE, std::min(strlen(PRINTER_CODE), size_t(32)));
      cam.write(login, sizeof login);
      camOpen = true;
      Serial.printf("printer: camera streaming (RAM free %u KB, lowest %u KB)\n", ESP.getFreeHeap() / 1024,
                    ESP.getMinFreeHeap() / 1024);
    }
    uint8_t hdr[16];
    if (!readN(hdr, 16)) { camClose(); continue; }
    size_t len = hdr[0] | hdr[1] << 8 | hdr[2] << 16;
    if (len < 4 || len > JPG_MAX || !readN(jpg, len)) { camClose(); continue; }
    if (jpg[0] != 0xFF || jpg[1] != 0xD8) continue;  // not a JPEG
    camLastMs = millis();
    // Another frame already waiting means we're behind (frames queue up during a Wi-Fi hiccup, then arrive in a
    // burst): skip this one and decode only the newest. Decoding a burst back to back starved the idle task on core
    // 0 for 5 s and tripped the task watchdog.
    if (cam.available() >= 16) continue;
    xSemaphoreTake(camLock, portMAX_DELAY);  // held while decoding; loop()'s pump only tries it, so it never waits
    if (uiCamDecode(camPic, jpg, len)) camFresh = true;
    xSemaphoreGive(camLock);
    vTaskDelay(pdMS_TO_TICKS(10));  // a breather for the rest of core 0 after each decode
  }
}

void printerCamWant(bool on) { if (on) camWantMs = millis(); }
void printerCamStop() { camWantMs = 0; }
bool printerCamOpen() { return camOpen; }
bool printerCamUp() { return camLastMs && int32_t(millis() - camLastMs) < 5000; }  // signed: a frame may land mid-call

bool printerCamPump() {
  if (!camLock || xSemaphoreTake(camLock, 0) != pdTRUE) return false;
  bool redraw = camFresh && uiCamFrame(camPic);
  camFresh = false;
  xSemaphoreGive(camLock);
  return redraw;
}

void printerBegin() {
  if (!*PRINTER_IP) return;
  camLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "printer", 16384, nullptr, 1, nullptr, 0);  // TLS handshakes need a deep stack
  // Idle priority, like the radar task: JPEG decoding is long CPU work, and this way the idle task (which the 5 s task
  // watchdog checks) always gets its turn, while the data, MQTT and network tasks come first.
  xTaskCreatePinnedToCore(camTask, "printer-cam", 16384, nullptr, tskIDLE_PRIORITY, nullptr, 0);
}

void printerRead(Printer& p) { portENTER_CRITICAL(&mux); p = shared; portEXIT_CRITICAL(&mux); }
bool printerUp() { return up && lastMs; }
void printerPause(bool on) { paused = on; }
uint32_t printerLastMs() { return lastMs; }
