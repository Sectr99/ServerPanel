// Stream Deck surface for Bitfocus Companion, over its Satellite API: one TCP connection, one message per line
// (https://companion.free/for-developers/Satellite-API). The panel registers as a DECK_COLS x DECK_ROWS surface;
// Companion sends each key's image (KEY-STATE) and the panel sends taps back (KEY-PRESS).
// The connection runs in a task of its own so a slow or absent Companion never stalls the screen. Images reach
// loop() through one slot per key (ui.cpp draws on loop()'s task and LovyanGFX isn't thread-safe), and taps come
// back through a queue, so only this task ever touches the socket.
#include "companion.h"
#include "config.h"
#include "ui.h"
#include <WiFi.h>
#include <mbedtls/base64.h>

static constexpr size_t LINE_CAP = 256 * 1024;              // a raw RGB key is ~100 KB of base64; PNGs are far smaller
static constexpr size_t IMG_MAX = DECK_PX * DECK_PX * 3;  // decoded: raw RGB888, or a PNG (never bigger in practice)

struct Slot { uint8_t* buf; size_t len; bool png, fresh; };
static Slot slots[DECK_KEYS];
static SemaphoreHandle_t lock;
static QueueHandle_t presses;
static volatile bool added = false;  // Companion accepted ADD-DEVICE on the current connection
static WiFiClient sock;
static char deviceId[20];

// Finds ` NAME=value` in a message; value runs to the next space, or sits in quotes. Base64 never contains a space,
// so a match can't start inside a bitmap.
static bool param(const char* line, const char* name, const char*& v, size_t& n) {
  char pat[24];
  snprintf(pat, sizeof pat, " %s=", name);
  const char* p = strstr(line, pat);
  if (!p) return false;
  p += strlen(pat);
  const char* e;
  if (*p == '"') { p++; e = strchr(p, '"'); if (!e) return false; }
  else for (e = p; *e && *e != ' '; e++) {}
  v = p; n = e - p;
  return true;
}

static void addDevice(bool png) {
  sock.printf("ADD-DEVICE DEVICEID=%s PRODUCT_NAME=\"ServerPanel\" SERIAL=serverpanel:%s KEYS_TOTAL=%d KEYS_PER_ROW=%d "
              "BITMAPS=%d COLORS=false TEXT=false BRIGHTNESS=false%s\n",
              deviceId, deviceId, DECK_KEYS, DECK_COLS, DECK_PX, png ? " BITMAP_FORMAT=png" : "");
  Serial.printf("companion: adding surface (%s keys)\n", png ? "PNG" : "raw RGB");
}

// One complete message from Companion. BEGIN starts the handshake; CAPS (or a timeout in task()) answers it.
static void handle(char* line, uint32_t& beganAt, bool& sentAdd) {
  const char *v;
  size_t n;
  if (!strncmp(line, "PING", 4)) { sock.printf("PONG%s\n", line + 4); return; }
  if (!strncmp(line, "BEGIN", 5)) { beganAt = millis(); sentAdd = false; Serial.printf("companion: %s\n", line); return; }
  if (!strncmp(line, "CAPS", 4)) {  // API 1.10+: sent right after BEGIN. PNG keys need BITMAP_FORMATS to list png
    bool png = param(line, "BITMAP_FORMATS", v, n) && memmem(v, n, "png", 3);
    if (!sentAdd) { addDevice(png); sentAdd = true; }
    return;
  }
  if (!strncmp(line, "ADD-DEVICE", 10)) {
    added = !strncmp(line, "ADD-DEVICE OK", 13);
    Serial.printf("companion: %s\n", line);
    return;
  }
  if (!strncmp(line, "KEYS-CLEAR", 10)) {
    xSemaphoreTake(lock, portMAX_DELAY);
    for (Slot& s : slots) { s.len = 0; s.fresh = true; }
    xSemaphoreGive(lock);
    return;
  }
  if (!strncmp(line, "KEY-STATE", 9)) {
    if (!param(line, "KEY", v, n)) return;
    int key = atoi(v);
    if (key < 0 || key >= DECK_KEYS) return;
    if (!param(line, "BITMAP", v, n)) return;  // e.g. a state change without a new image
    bool png = n > 5 && !memcmp(v, "data:", 5);
    if (png) {  // data:image/png;base64,....
      const char* comma = (const char*)memchr(v, ',', n);
      if (!comma) return;
      n -= comma + 1 - v;
      v = comma + 1;
    }
    Slot& s = slots[key];
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t olen = 0;
    if (mbedtls_base64_decode(s.buf, IMG_MAX, &olen, (const unsigned char*)v, n) == 0) {
      s.len = olen; s.png = png; s.fresh = true;
    }
    xSemaphoreGive(lock);
    return;
  }
  if (strstr(line, "ERROR")) Serial.printf("companion: %.200s\n", line);
}

static void task(void*) {
  char* line = (char*)ps_malloc(LINE_CAP);
  for (Slot& s : slots) s.buf = (uint8_t*)ps_malloc(IMG_MAX);
  uint64_t mac = ESP.getEfuseMac();  // stable per board, so Companion keeps each panel's pages apart
  snprintf(deviceId, sizeof deviceId, "%012llX", (unsigned long long)mac);
  size_t len = 0;
  bool skipping = false, sentAdd = false, wasUp = false;
  uint32_t beganAt = 0, lastPing = 0, lastRx = 0;
  uint8_t chunk[2048];
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) { added = false; vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (!sock.connected()) {
      if (wasUp) Serial.println("companion: disconnected");
      added = wasUp = false;
      sock.stop();
      if (!sock.connect(COMPANION_HOST, COMPANION_PORT, 2000)) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
      sock.setNoDelay(true);
      Serial.println("companion: connected to " COMPANION_HOST);
      wasUp = true;
      len = 0; skipping = sentAdd = false; beganAt = 0;
      lastPing = lastRx = millis();
    }

    int avail;
    bool busy = false;  // data arrived this pass: check again soon, more may follow
    while ((avail = sock.available()) > 0) {
      int got = sock.read(chunk, std::min<int>(avail, sizeof chunk));
      if (got <= 0) break;
      lastRx = millis();
      busy = true;
      for (int i = 0; i < got; i++) {
        char ch = chunk[i];
        if (ch == '\n') {
          if (!skipping) {
            if (len && line[len - 1] == '\r') len--;
            line[len] = 0;
            handle(line, beganAt, sentAdd);
          }
          len = 0; skipping = false;
        } else if (!skipping) {
          if (len < LINE_CAP - 1) line[len++] = ch;
          else skipping = true;  // too long to be anything we asked for: drop it
        }
      }
    }

    // Pre-1.10 servers send no CAPS: register with raw RGB keys once it has had its chance.
    if (beganAt && !sentAdd && millis() - beganAt > 1000) { addDevice(false); sentAdd = true; }
    int8_t key;
    while (xQueueReceive(presses, &key, 0) == pdTRUE) {  // a tap = press and release (no hold actions yet)
      sock.printf("KEY-PRESS DEVICEID=%s KEY=%d PRESSED=1\n", deviceId, key);
      sock.printf("KEY-PRESS DEVICEID=%s KEY=%d PRESSED=0\n", deviceId, key);
    }
    if (millis() - lastPing > 2000) { sock.print("PING panel\n"); lastPing = millis(); }  // the API's keepalive
    if (millis() - lastRx > 10000) { Serial.println("companion: no reply in 10 s"); sock.stop(); }
    // Wait for the next pass on the tap queue itself, so a tap still goes out at once while idle passes are slower.
    xQueuePeek(presses, &key, pdMS_TO_TICKS(busy ? 10 : 30));
  }
}

void companionBegin() {
  if (!*COMPANION_HOST) return;
  lock = xSemaphoreCreateMutex();
  presses = xQueueCreate(8, sizeof(int8_t));
  xTaskCreatePinnedToCore(task, "companion", 8192, nullptr, 1, nullptr, 0);
}

bool companionPump() {
  if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) return false;
  bool any = false;
  for (int i = 0; i < DECK_KEYS; i++)
    if (slots[i].fresh) { uiDeckKey(i, slots[i].buf, slots[i].len, slots[i].png); slots[i].fresh = false; any = true; }
  xSemaphoreGive(lock);
  return any;
}

void companionPress(int key) {
  int8_t k = key;
  if (presses) xQueueSend(presses, &k, 0);
}

bool companionUp() { return added; }
