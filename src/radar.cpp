// Weather radar app: NOAA's MRMS base reflectivity mosaic over Esri's World Dark Gray map (OpenStreetMap data).
// The radar comes from NOAA's OGC WMS, which renders any area at any size as a PNG that's transparent where there's
// no echo, so one request covers the whole map and lines up with the tiles pixel for pixel (both Web Mercator).
// Runs in its own task while the radar app is on screen: builds the base map once per place and zoom (kept, so
// switching zoom back is instant), fetches the past hour every 5 min, and plays it as a loop, composing each frame
// here so loop() only copies the finished picture. It also looks up ZIP codes for the location sheet
// (api.zippopotam.us), whether or not the map is showing.
#include "radar.h"
#include "config.h"
#include "ui.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "psram_json.h"

static constexpr int FRAMES = 10, STEP_MIN = 6;  // the past hour, a frame every 6 minutes
static constexpr uint32_t REFRESH_MS = 5 * 60000, FRAME_MS = 1000, HOLD_MS = 3000;  // the latest frame holds longer

// What loop() asks for.
struct View {
  float lat, lon;
  int z;
  bool operator==(const View& o) const { return lat == o.lat && lon == o.lon && z == o.z; }
};
static portMUX_TYPE viewMux = portMUX_INITIALIZER_UNLOCKED;
static View want = {0, 0, -1};
static volatile uint32_t wantMs = 0, okMs = 0;  // okMs: the last time the loop was fetched, for radarUp()
static volatile bool active = false;  // downloading or connected (set by the task only)

// Handed to loop() under `lock`: the latest composed frame, and a ZIP lookup's answer.
static SemaphoreHandle_t lock;
static LGFX_Sprite pic;
static bool picFresh = false;
static time_t picT = 0;
static int picFrame = 0, picFrames = 0;
static char zipAsk[6];
static volatile bool zipAsked = false;
static struct { bool ready, ok; char zip[6], place[28]; float lat, lon; } answer;

// Esri's map tiles and NOAA's radar are HTTPS-only. They share one TLS connection (a second would cost ~35 KB of
// internal RAM), reconnecting when the host changes; the ZIP lookup is plain HTTP.
// ponytail: TLS without a cert check (public imagery, worst case a wrong picture); pin their CAs if that matters.
static WiFiClient zipNet;
static WiFiClientSecure tlsNet;
static HTTPClient tlsHttp, zipHttp;
static String tlsHost;

// GET url into a PSRAM buffer, grown as needed. Retries once if a kept connection had gone stale.
static bool fetch(HTTPClient& http, WiFiClient& net, const String& url, uint8_t*& buf, size_t& cap, size_t& len) {
  for (int attempt = 0; attempt < 2; attempt++) {
    bool reused = net.connected();
    http.setReuse(true);
    http.setConnectTimeout(3000);
    http.setTimeout(4000);  // under the 5 s task watchdog, in case the task ever runs above idle priority
    http.setUserAgent("ServerPanel/1.0 (ESP32-P4 home dashboard)");
    if (!http.begin(net, url)) return false;
    int code = http.GET();
    if (code <= 0 && reused) { http.end(); net.stop(); continue; }
    if (code != 200) {
      Serial.printf("radar: %s -> HTTP %d\n", url.substring(0, 90).c_str(), code);
      http.end();
      return false;
    }
    String body = http.getString();
    http.end();
    len = body.length();
    if (len > cap) {
      free(buf);
      cap = len + 16 * 1024;
      buf = (uint8_t*)ps_malloc(cap);
      if (!buf) { cap = len = 0; return false; }
    }
    memcpy(buf, body.c_str(), len);
    return len > 0;
  }
  return false;
}

// HTTPS GET over the shared connection. HTTPClient reuses whatever connection is open, so a new host closes it first.
static bool fetchTls(const String& url, uint8_t*& buf, size_t& cap, size_t& len) {
  if (!wantMs) return false;  // radarStop(): give the RAM back now, not after the rest of a map or a loop
  String host = url.substring(8, url.indexOf('/', 8));
  if (host != tlsHost) { tlsHttp.end(); tlsNet.stop(); tlsHost = host; }
  return fetch(tlsHttp, tlsNet, url, buf, cap, len);
}

// Esri's World Dark Gray canvas: a JPEG base layer, and a transparent PNG layer of place names drawn over the radar.
static String esriTile(const char* layer, int z, int x, int y) {
  char url[160];
  snprintf(url, sizeof url, "https://services.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_%s/MapServer/tile/%d/%d/%d",
           layer, z, y, x);
  return url;
}

// uiRadarLayer's tile sources: one tile into a shared buffer (each is drawn before the next is fetched).
static bool layerTile(const char* layer, int z, int x, int y, const uint8_t*& img, size_t& len) {
  static uint8_t* buf = nullptr;
  static size_t cap = 0;
  if (!fetchTls(esriTile(layer, z, x, y), buf, cap, len)) return false;
  img = buf;
  return true;
}
static bool baseTile(int z, int x, int y, const uint8_t*& img, size_t& len) { return layerTile("Base", z, x, y, img, len); }
static bool labelTile(int z, int x, int y, const uint8_t*& img, size_t& len) {
  return layerTile("Reference", z, x, y, img, len);
}

static void lookupZip(const char* zip) {
  static uint8_t* buf = nullptr;
  static size_t cap = 0;
  size_t len = 0;
  bool ok = false;
  float lat = 0, lon = 0;
  char place[28] = "";
  if (fetch(zipHttp, zipNet, String("http://api.zippopotam.us/us/") + zip, buf, cap, len)) {
    JsonDocument doc;
    if (!deserializeJson(doc, buf, len)) {
      JsonVariantConst p = doc["places"][0];
      const char *name = p["place name"], *st = p["state abbreviation"], *la = p["latitude"], *lo = p["longitude"];
      if (name && la && lo) {
        snprintf(place, sizeof place, "%s%s%s", name, st ? ", " : "", st ? st : "");
        lat = atof(la);
        lon = atof(lo);
        ok = true;
      }
    }
  }  // a 404 is the service's "no such ZIP"
  Serial.printf("radar: ZIP %s -> %s\n", zip, ok ? place : "not found");
  xSemaphoreTake(lock, portMAX_DELAY);
  answer = {true, ok, {}, {}, lat, lon};
  snprintf(answer.zip, sizeof answer.zip, "%s", zip);
  snprintf(answer.place, sizeof answer.place, "%s", place);
  xSemaphoreGive(lock);
}

// ---------- local weather: api.weather.gov (NWS), over the same shared TLS connection ----------
// /points/<lat>,<lon> gives the place's forecast URL and nearby stations (looked up once per place); then current
// conditions from the nearest station and the forecast every 10 min, and active alerts every 2 min.
static Weather *wxWork, *wxShared;  // built here; handed to loop() under `lock` (heap: ~5 KB each, so PSRAM)
static bool wxFresh = false;
static float wxLat = NAN, wxLon = NAN;  // the place the URLs below are for
static String wxForecastUrl, wxStation;
static uint32_t wxCondAt = 0, wxAlertAt = 0;  // when each is next due

static bool getWx(const String& url, JsonDocument& doc, const JsonDocument& filter) {
  static uint8_t* buf = nullptr;
  static size_t cap = 0;
  size_t len;
  if (!fetchTls(url, buf, cap, len)) return false;
  DeserializationError e = deserializeJson(doc, buf, len, DeserializationOption::Filter(filter));
  if (e) Serial.printf("radar: weather JSON: %s\n", e.c_str());
  return !e;
}

// ISO 8601 with an offset ("2026-10-24T21:45:00-05:00") to UTC; 0 if absent.
static time_t isoTime(const char* s) {
  int y, mo, d, h, mi, sec = 0, oh = 0, om = 0;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) return 0;
  const char* tz = s + 19;
  while (*tz && *tz != '+' && *tz != '-' && *tz != 'Z') tz++;  // past any fraction of a second
  int sign = *tz == '-' ? -1 : 1;
  if (*tz == '+' || *tz == '-') sscanf(tz + 1, "%d:%d", &oh, &om);
  y -= mo <= 2;  // days since 1970 from a civil date (Howard Hinnant's algorithm)
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400), doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const long days = era * 146097 + long(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
  return time_t(days * 86400 + h * 3600 + mi * 60 + sec - sign * (oh * 3600 + om * 60));
}

// NWS text is hard-wrapped at ~70 columns with blank lines between paragraphs: rejoin lines, keep paragraphs.
static size_t appendText(char* dst, size_t at, size_t cap, const char* s) {
  if (!s) return at;
  if (at && at < cap - 1) dst[at++] = '\n';
  for (; *s && at < cap - 1; s++) {
    if (*s == '\r') continue;
    if (*s == '\n') {
      if (s[1] == '\n') { if (at && dst[at - 1] != '\n') dst[at++] = '\n'; while (s[1] == '\n') s++; }
      else if (at && dst[at - 1] != ' ' && dst[at - 1] != '\n') dst[at++] = ' ';
      continue;
    }
    if (*s == ' ' && at && (dst[at - 1] == ' ' || dst[at - 1] == '\n')) continue;
    dst[at++] = *s;
  }
  dst[at] = 0;
  return at;
}

static void publishWx() {
  xSemaphoreTake(lock, portMAX_DELAY);
  *wxShared = *wxWork;
  wxFresh = true;
  xSemaphoreGive(lock);
}

static void updateWeather(const View& v) {
  if (!(wxLat == v.lat && wxLon == v.lon)) {  // a new place: start over
    wxLat = v.lat;
    wxLon = v.lon;
    wxForecastUrl = wxStation = "";
    *wxWork = Weather();
    wxCondAt = wxAlertAt = millis();
    publishWx();
  }
  char url[128];
  if (!wxStation.length() && int32_t(millis() - wxCondAt) >= 0) {  // the place's NWS grid and nearest station
    JsonDocument filter, doc(&psram);
    filter["properties"]["forecast"] = true;
    filter["properties"]["observationStations"] = true;
    snprintf(url, sizeof url, "https://api.weather.gov/points/%.4f,%.4f", v.lat, v.lon);
    if (getWx(url, doc, filter)) {
      wxForecastUrl = doc["properties"]["forecast"] | "";
      String stations = doc["properties"]["observationStations"] | "";
      JsonDocument f2, d2(&psram);
      f2["features"][0]["properties"]["stationIdentifier"] = true;
      if (stations.length() && getWx(stations + "?limit=1", d2, f2))
        wxStation = d2["features"][0]["properties"]["stationIdentifier"] | "";
    }
    if (!wxStation.length()) { wxCondAt = millis() + 60000; return; }  // try again in a minute
  }
  if (wxStation.length() && int32_t(millis() - wxCondAt) >= 0) {  // conditions now, and the forecast
    JsonDocument filter, doc(&psram);
    for (const char* k : {"temperature", "windSpeed", "windDirection", "relativeHumidity"})
      filter["properties"][k]["value"] = true;
    filter["properties"]["textDescription"] = true;
    bool ok = getWx("https://api.weather.gov/stations/" + wxStation + "/observations/latest", doc, filter);
    if (ok) {
      JsonVariantConst p = doc["properties"];
      auto val = [&](const char* k) { JsonVariantConst x = p[k]["value"]; return x.isNull() ? NAN : x.as<float>(); };
      float c = val("temperature"), kmh = val("windSpeed"), deg = val("windDirection");
      wxWork->tempF = isnan(c) ? NAN : c * 9 / 5 + 32;
      wxWork->windMph = isnan(kmh) ? NAN : kmh * 0.621371f;
      wxWork->humidity = val("relativeHumidity");
      static const char* const dirs[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                         "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
      snprintf(wxWork->windDir, sizeof wxWork->windDir, "%s", isnan(deg) ? "" : dirs[int(deg / 22.5f + 0.5f) % 16]);
      snprintf(wxWork->sky, sizeof wxWork->sky, "%s", (const char*)(p["textDescription"] | ""));
    }
    JsonDocument f2, d2(&psram);
    for (const char* k : {"name", "temperature", "shortForecast"}) f2["properties"]["periods"][0][k] = true;
    f2["properties"]["periods"][0]["probabilityOfPrecipitation"]["value"] = true;
    if (wxForecastUrl.length() && getWx(wxForecastUrl, d2, f2)) {
      int n = 0;
      for (JsonVariantConst p : d2["properties"]["periods"].as<JsonArrayConst>()) {
        if (n == 2) break;
        auto& q = wxWork->periods[n++];
        snprintf(q.name, sizeof q.name, "%s", (const char*)(p["name"] | ""));
        snprintf(q.brief, sizeof q.brief, "%s", (const char*)(p["shortForecast"] | ""));
        q.temp = p["temperature"] | 0;
        q.pop = p["probabilityOfPrecipitation"]["value"] | -1;
      }
      wxWork->nPeriods = n;
    }
    wxWork->have = ok || wxWork->nPeriods;
    wxCondAt = millis() + (ok ? 10 * 60000 : 60000);
    publishWx();
  }
  if (int32_t(millis() - wxAlertAt) >= 0) {  // active alerts for this point, most severe first
    JsonDocument filter, doc(&psram);
    for (const char* k : {"event", "severity", "ends", "expires", "headline", "description", "instruction"})
      filter["features"][0]["properties"][k] = true;
    snprintf(url, sizeof url, "https://api.weather.gov/alerts/active?point=%.4f,%.4f", v.lat, v.lon);
    if (getWx(url, doc, filter)) {
      static const char* const order[] = {"Extreme", "Severe", "Moderate", "Minor", "Unknown"};
      int n = 0;
      for (const char* sev : order)
        for (JsonVariantConst f : doc["features"].as<JsonArrayConst>()) {
          JsonVariantConst p = f["properties"];
          const char* s = p["severity"] | "Unknown";
          bool known = false;
          for (const char* o : order) known |= !strcmp(s, o);
          if (n == 3 || strcmp(known ? s : "Unknown", sev)) continue;
          WxAlert& a = wxWork->alerts[n++];
          snprintf(a.event, sizeof a.event, "%s", (const char*)(p["event"] | "Weather alert"));
          snprintf(a.severity, sizeof a.severity, "%s", s);
          a.ends = isoTime((p["ends"].isNull() ? p["expires"] : p["ends"]).as<const char*>());
          size_t at = appendText(a.text, 0, sizeof a.text, p["headline"].as<const char*>());
          at = appendText(a.text, at, sizeof a.text, p["description"].as<const char*>());
          appendText(a.text, at, sizeof a.text, p["instruction"].as<const char*>());
        }
      wxWork->nAlerts = n;
      wxWork->alertsKnown = true;
      if (n) Serial.printf("radar: %d NWS alert(s), first: %s\n", n, wxWork->alerts[0].event);
      publishWx();
      wxAlertAt = millis() + 2 * 60000;
    } else {
      wxAlertAt = millis() + 60000;
    }
  }
}

static void task(void*) {
  // Map layers per zoom level (base, and place names on black), kept for the current place; radar frames as PNGs.
  static LGFX_Sprite base[N_RADAR_ZOOMS], labels[N_RADAR_ZOOMS];
  View baseOf[N_RADAR_ZOOMS] = {};
  struct Frame { uint8_t* png = nullptr; size_t cap = 0, len = 0; time_t t = 0; } frames[FRAMES];
  int nFrames = 0, shown = 0;
  View loaded = {0, 0, -1};
  uint32_t nextFrameMs = 0, fetchedMs = 0, retryAt = 0, baseRetryAt = 0;
  bool timeIt = false;  // log how long the next composed frame takes (once per fetch)

  for (;;) {
    if (zipAsked && WiFi.status() == WL_CONNECTED) {
      char zip[6];
      memcpy(zip, zipAsk, sizeof zip);
      zipAsked = false;
      lookupZip(zip);
    }
    View v;
    portENTER_CRITICAL(&viewMux); v = want; portEXIT_CRITICAL(&viewMux);
    bool on = wantMs && millis() - wantMs < 30000 && WiFi.status() == WL_CONNECTED;
    if (!on || v.z < 0) {
      // Free its TLS memory until shown again. Not `if (connected())`: once the server drops an idle keep-alive
      // connection that's false, but the TLS buffers stay allocated until stop().
      if (tlsHost.length()) { tlsHttp.end(); tlsNet.stop(); tlsHost = ""; }
      active = false;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    active = true;
    int zi = 0;
    while (zi < N_RADAR_ZOOMS - 1 && RADAR_ZOOMS[zi] != v.z) zi++;

    // A new place or zoom: a fresh loop, on the base map for it (kept per zoom, so switching back is instant).
    if (!(loaded == v)) {
      loaded = v;
      nFrames = shown = 0;
      fetchedMs = 0;
      retryAt = baseRetryAt = millis();
    }
    if (!(baseOf[zi] == v) && int32_t(millis() - baseRetryAt) >= 0) {
      Serial.printf("radar: base map for %.4f, %.4f at zoom %d\n", v.lat, v.lon, v.z);
      if (uiRadarLayer(base[zi], v.lat, v.lon, v.z, baseTile, 0x242424u) &&  // the map's land colour under gaps
          uiRadarLayer(labels[zi], v.lat, v.lon, v.z, labelTile, RADAR_CLEAR))
        baseOf[zi] = v;
      else { baseRetryAt = millis() + 30000; Serial.println("radar: some base map tiles failed; retrying in 30 s"); }
    }

    // Every 5 min: the past hour, oldest first. Needs the clock for the times; without it, just the latest.
    if ((!fetchedMs || millis() - fetchedMs > REFRESH_MS) && int32_t(millis() - retryAt) >= 0) {
      double minX, minY, maxX, maxY;
      uiRadarBBox(v.lat, v.lon, v.z, minX, minY, maxX, maxY);
      time_t now = time(nullptr);
      bool clockOk = now > 1700000000;
      int count = clockOk ? FRAMES : 1, got = 0;
      for (int k = 0; k < count; k++) {
        time_t t = now - (count - 1 - k) * STEP_MIN * 60;
        char when[40] = "";
        if (clockOk) strftime(when, sizeof when, "&TIME=%Y-%m-%dT%H:%M:00Z", gmtime(&t));
        char url[400];
        snprintf(url, sizeof url,
                 "https://opengeo.ncep.noaa.gov/geoserver/conus/conus_bref_qcd/ows?SERVICE=WMS&VERSION=1.1.1"
                 "&REQUEST=GetMap&LAYERS=conus_bref_qcd&STYLES=&SRS=EPSG:3857&BBOX=%.1f,%.1f,%.1f,%.1f"
                 "&WIDTH=%d&HEIGHT=%d&FORMAT=image/png&TRANSPARENT=true%s",
                 minX, minY, maxX, maxY, RADAR_W, RADAR_H, when);
        Frame& f = frames[got];
        if (fetchTls(url, f.png, f.cap, f.len)) { f.t = clockOk ? t : 0; got++; }
      }
      if (got) {
        nFrames = got;
        shown = nFrames - 2;  // the next frame shown is the latest; the loop carries on from the oldest
        fetchedMs = okMs = millis();
        if (!clockOk) fetchedMs -= REFRESH_MS - 30000;  // just the latest frame: get the hour once the clock is set
        nextFrameMs = 0;
        timeIt = true;
        Serial.printf("radar: %d frames (RAM free %u KB, lowest %u KB)\n", got, ESP.getFreeHeap() / 1024,
                      ESP.getMinFreeHeap() / 1024);
      } else {
        retryAt = millis() + 30000;  // nothing came: try again in 30 s
      }
    }
    updateWeather(v);

    // Play the loop: compose the next frame for loop() to pick up.
    if (nFrames && baseOf[zi] == v && int32_t(millis() - nextFrameMs) >= 0) {
      shown = (shown + 1 + nFrames) % nFrames;
      const Frame& f = frames[shown];
      xSemaphoreTake(lock, portMAX_DELAY);
      uint32_t t0 = millis();
      if (uiRadarCompose(pic, base[zi], f.png, f.len, &labels[zi])) {
        picFresh = true;
        picT = f.t;
        picFrame = shown;
        picFrames = nFrames;
      }
      xSemaphoreGive(lock);
      if (timeIt) { timeIt = false; Serial.printf("radar: a frame composes in %u ms\n", unsigned(millis() - t0)); }
      nextFrameMs = millis() + (shown == nFrames - 1 ? HOLD_MS : FRAME_MS);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void radarBegin() {
  lock = xSemaphoreCreateMutex();
  wxWork = new Weather;
  wxShared = new Weather;
  tlsNet.setInsecure();
  // Idle priority: HTTPClient's reads (Stream::timedRead) busy-wait for slow data without yielding, and at priority 1
  // a server that stalls for 5 s starved the idle task and tripped the task watchdog. At 0 the idle task shares the
  // core, and every other task (data, printer, camera) comes first. Deep stack: TLS handshakes.
  xTaskCreatePinnedToCore(task, "radar", 16384, nullptr, tskIDLE_PRIORITY, nullptr, 0);
}

void radarWant(bool on, float lat, float lon, int z) {
  if (!on) return;
  portENTER_CRITICAL(&viewMux);
  want = {lat, lon, z};
  portEXIT_CRITICAL(&viewMux);
  wantMs = millis();
}

void radarStop() { wantMs = 0; }
bool radarOpen() { return active; }

void radarLookup(const char* zip) {
  snprintf(zipAsk, sizeof zipAsk, "%s", zip);
  zipAsked = true;
}

bool radarPump() {
  if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) return false;  // only tries: loop() never waits on a compose
  bool redraw = false;
  if (picFresh) { redraw = uiRadarFrame(pic, picT, picFrame, picFrames); picFresh = false; }
  if (wxFresh) { uiRadarWeather(*wxShared); wxFresh = false; redraw = true; }
  if (answer.ready) {
    uiRadarPlace(answer.ok, answer.zip, answer.lat, answer.lon, answer.place);
    answer.ready = false;
    redraw = true;
  }
  xSemaphoreGive(lock);
  return redraw;
}

bool radarUp() { return okMs && millis() - okMs < 15 * 60000; }
