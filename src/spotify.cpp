// Spotify app: what's playing on the user's account, and its controls, over the Spotify Web API (HTTPS with a
// bearer token, refreshed hourly from the refresh token in config.h; tools/spotify_auth.py gets that one).
// Polls GET /me/player every 2 s (5 s while nothing plays), sends the controls tapped, checks whether each new track
// is in Liked Songs, and fetches the album art when it changes. The art comes from Spotify's image CDN over plain
// HTTP, so the only TLS connection is the API's (~35 KB of internal RAM; the token endpoint shares it, reconnecting
// about once an hour). The task only exists while the app is on screen (plus a 30 s linger), so panels that never
// open it don't spend its stack.
#include "spotify.h"
#include "config.h"
#include "ui.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <algorithm>
#include "psram_json.h"

// The latest state, with the controls tapped since applied at once; loop() takes it in spotifyPump().
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Spotify state;
static uint32_t stateMs = 0;  // millis() at which state.progressMs held
static uint32_t cmdGen = 0;   // bumped per control: a poll that started before one doesn't overwrite it
static bool fresh = false;
static volatile uint32_t wantMs = 0;
static volatile bool running = false, active = false, up = false;
struct Cmd { uint8_t cmd; int arg; };
static QueueHandle_t cmds;

static SemaphoreHandle_t artLock;  // guards art and artFresh; held while decoding, loop() only tries it
static LGFX_Sprite art;
static uint32_t artTint = 0;  // its colour, for the background
static bool artFresh = false;

// ponytail: TLS without a cert check, like the other HTTPS sources; pin Spotify's CA if that matters.
static WiFiClientSecure tlsNet;
static HTTPClient tlsHttp;
static String tlsHost;
static WiFiClient artNet;
static HTTPClient artHttp;
static String token;
static uint32_t tokenUntil = 0, waitUntil = 0;  // waitUntil: a 429's Retry-After, or a failed token refresh

// method + url [+ form body]: the HTTP status (<= 0: no answer); the reply's body to *reply. range: a Range header.
static int request(const char* method, const String& url, const String& body, String* reply, const char* range = nullptr) {
  const bool tls = url.startsWith("https://");
  HTTPClient& http = tls ? tlsHttp : artHttp;
  WiFiClient& net = tls ? tlsNet : artNet;
  if (tls) {  // one TLS connection for both Spotify hosts: a new host closes it first
    String host = url.substring(8, url.indexOf('/', 8));
    if (host != tlsHost) { tlsHttp.end(); tlsNet.stop(); tlsHost = host; }
  }
  for (int attempt = 0; attempt < 2; attempt++) {
    bool reused = net.connected();
    http.setReuse(true);
    http.setConnectTimeout(3000);
    http.setTimeout(4000);  // under the 5 s task watchdog, in case the task ever runs above idle priority
    if (!http.begin(net, url)) return -1;
    if (url.indexOf("//accounts.") > 0) {
      http.addHeader("Authorization", "Basic " + base64::encode(SPOTIFY_CLIENT_ID ":" SPOTIFY_CLIENT_SECRET));
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    } else if (tls) {
      http.addHeader("Authorization", "Bearer " + token);
      if (body.length()) http.addHeader("Content-Type", "application/json");
    }
    if (range) http.addHeader("Range", range);
    // HTTPClient leaves Content-Length out of an empty body, and Spotify answers a PUT or POST without it with 411.
    if (!body.length() && strcmp(method, "GET")) http.addHeader("Content-Length", "0");
    int code = http.sendRequest(method, body);
    if (code <= 0 && reused) { http.end(); net.stop(); continue; }  // the kept connection had gone stale
    if (code == 429) waitUntil = millis() + 1000 * std::max(1L, http.header("Retry-After").toInt());
    // No body to read on a 204 (volume, seek...), and HTTPClient doesn't know that: without a Content-Length it read
    // on until Spotify closed the kept connection, which hung this task after the first volume change.
    String r = code > 0 && code != 204 ? http.getString() : String();
    http.end();
    if (reply) *reply = r;
    return code;
  }
  return -1;
}

static void setError(const char* msg) {
  Serial.printf("spotify: %s\n", msg);
  up = false;
  portENTER_CRITICAL(&mux);
  snprintf(state.error, sizeof state.error, "%s", msg);
  fresh = true;
  portEXIT_CRITICAL(&mux);
}

// A failed API call: Spotify's own message when it gave one ({"error": {"status": 403, "message": "..."}}).
static void apiError(const char* what, int code, const String& reply) {
  char msg[72];
  if (code <= 0) { setError("OFFLINE  /  no answer from Spotify"); return; }
  JsonDocument doc(&psram);
  const char* m = deserializeJson(doc, reply) ? "" : doc["error"]["message"] | "";
  snprintf(msg, sizeof msg, "%s: HTTP %d %s", what, code, m);
  setError(msg);
}

static bool refreshToken() {
  String reply;
  int code = request("POST", "https://accounts.spotify.com/api/token",
                     "grant_type=refresh_token&refresh_token=" SPOTIFY_REFRESH, &reply);
  JsonDocument doc(&psram);
  if (code == 200 && !deserializeJson(doc, reply) && doc["access_token"].is<const char*>()) {
    token = doc["access_token"].as<const char*>();
    tokenUntil = millis() + (doc["expires_in"] | 3600) * 1000UL - 60000;  // a minute early
    return true;
  }
  const char* e = code > 0 && !deserializeJson(doc, reply) ? doc["error"] | "" : "";
  setError(!strcmp(e, "invalid_grant")    ? "Spotify login expired: run tools/spotify_auth.py again"
           : !strcmp(e, "invalid_client") ? "Spotify rejected SPOTIFY_CLIENT_ID / SECRET"
           : code > 0                     ? "Spotify login failed"
                                          : "OFFLINE  /  no answer from Spotify");
  return false;
}

// Liked Songs, per item: asked once per new track, and set by the like control.
static char likedUri[64] = "";  // URL-encoded, e.g. spotify%3Atrack%3A<id>; "" = nothing to like
static bool liked = false;
static String artUrl;           // the album art handed to the screen

// An image from Spotify's CDN, over plain HTTP, in 16 KB pieces (the CDN takes Range requests). Fetched whole, the
// 64 KB TCP window filled internal RAM with the packets queued for this idle-priority task faster than it read them:
// down to 40 KB free, near the Wi-Fi wedge.
static bool fetchImage(String url, String& jpg) {
  url.replace("https://", "http://");
  jpg = "";
  bool got = url.length() > 0;
  if (got) jpg.reserve(160 * 1024);
  for (size_t at = 0; got;) {
    char range[40];
    snprintf(range, sizeof range, "bytes=%u-%u", unsigned(at), unsigned(at + 16383));
    String part;
    int code = request("GET", url, "", &part, range);
    if (code == 416 && at) break;  // it ended exactly on a piece
    got = (code == 206 || code == 200) && part.length();
    if (!got) break;
    if (code == 200) { jpg = part; break; }  // the server sent it whole after all
    jpg += part;
    at += part.length();
    if (part.length() < 16384) break;  // the last piece
  }
  return got && jpg.length();
}

static void setArt(const String& url) {
  if (url == artUrl) return;
  artUrl = url;
  String jpg;
  bool got = fetchImage(url, jpg);
  if (got) Serial.printf("spotify: album art %u KB (RAM free %u KB, lowest %u KB)\n", unsigned(jpg.length() / 1024),
                         ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024);
  xSemaphoreTake(artLock, portMAX_DELAY);
  if (!got || !uiSpotifyDecode(art, (const uint8_t*)jpg.c_str(), jpg.length(), artTint)) art.deleteSprite();  // none: a placeholder
  artFresh = true;
  xSemaphoreGive(artLock);
  if (url.length() && !got) artUrl = "";  // try again next poll
}

// ---------- the library and devices sheets ----------
// Recently played contexts (album, playlist, artist, Liked Songs) and the user's playlists, fetched when the library
// sheet opens (again after 5 min); covers then come one per loop round. Devices are polled every 5 s while their
// sheet shows. Spotify's recommendations, featured playlists and "Made for You" mixes aren't open to new apps (since
// November 2024), so this is what a homepage can be made of.
static volatile bool libWanted = false, devWanted = false;
static SpotifyLibrary lib;                    // this task's copy
static String libUri[SPOTIFY_ITEMS], libCover[SPOTIFY_ITEMS];
static uint32_t libAt = 0, devAt = 0;
static int nextCover = SPOTIFY_ITEMS;         // the next cover to fetch; SPOTIFY_ITEMS = none
static SpotifyDevices devs;
static String devId[8];
// Handed to loop() under artLock.
static SpotifyLibrary libShared;
static SpotifyDevices devShared;
static bool libFresh = false, devFresh = false;
static LGFX_Sprite cover;
static int coverIdx = -1;                     // cover holds this item's cover until loop() takes it

static char lastError[72];  // getJson's last failure, for the library sheet
static bool getJson(const String& url, JsonDocument& doc, const JsonDocument& filter) {
  String r;
  int code = request("GET", url, "", &r);
  if (code == 401) tokenUntil = millis();  // refresh before the next request
  if (code != 200) {
    JsonDocument e(&psram);
    const char* m = code > 0 && !deserializeJson(e, r) ? e["error"]["message"] | "" : "";
    snprintf(lastError, sizeof lastError, code > 0 ? "Spotify: HTTP %d %s" : "No answer from Spotify", code, m);
    Serial.printf("spotify: GET %s -> HTTP %d %s\n", url.c_str() + 27, code, m);
    return false;
  }
  return !deserializeJson(doc, r, DeserializationOption::Filter(filter));
}

// The smallest image at least a cover wide (lists come largest first), else the first.
static String coverUrl(JsonArrayConst imgs) {
  String best;
  for (JsonVariantConst i : imgs) {
    const int w = i["width"] | 0;
    if (!best.length() || w >= SPOTIFY_COVER) best = i["url"] | "";
  }
  return best;
}

static void fetchLibrary() {
  lib = SpotifyLibrary();
  JsonDocument filter, doc(&psram);
  filter["items"][0]["context"]["type"] = true;
  filter["items"][0]["context"]["uri"] = true;
  for (const char* k : {"uri", "name"}) filter["items"][0]["track"]["album"][k] = true;
  filter["items"][0]["track"]["album"]["images"][0]["url"] = true;
  filter["items"][0]["track"]["album"]["images"][0]["width"] = true;
  filter["items"][0]["track"]["artists"][0]["name"] = true;
  int n = 0, nSkip = 0;
  String skip[8];
  const bool recentOk = getJson("https://api.spotify.com/v1/me/player/recently-played?limit=50", doc, filter);
  if (!recentOk) snprintf(lib.error[0], sizeof lib.error[0], "%s", lastError);
  if (recentOk) {
    for (JsonVariantConst it : doc["items"].as<JsonArrayConst>()) {
      if (n == SPOTIFY_RECENT) break;
      JsonVariantConst ctx = it["context"], album = it["track"]["album"];
      String uri = ctx.isNull() ? String(album["uri"] | "") : String(ctx["uri"] | "");
      const char* type = ctx.isNull() ? "album" : ctx["type"] | "";
      bool seen = !uri.length();
      for (int k = 0; k < n; k++) seen |= libUri[k] == uri;
      for (int k = 0; k < nSkip; k++) seen |= skip[k] == uri;
      if (seen) continue;
      SpotifyItem& item = lib.item[n];
      String img;
      if (!strcmp(type, "album")) {
        snprintf(item.name, sizeof item.name, "%s", (const char*)(album["name"] | ""));
        snprintf(item.sub, sizeof item.sub, "%s", (const char*)(it["track"]["artists"][0]["name"] | "Album"));
        img = coverUrl(album["images"]);
      } else if (!strcmp(type, "collection")) {
        snprintf(item.name, sizeof item.name, "Liked Songs");
        snprintf(item.sub, sizeof item.sub, "Playlist");
      } else if (!strcmp(type, "playlist") || !strcmp(type, "artist")) {  // look the name and picture up
        const String id = uri.substring(uri.lastIndexOf(':') + 1);
        JsonDocument f2, d2(&psram);
        f2["name"] = true;
        f2["images"][0]["url"] = true;
        f2["images"][0]["width"] = true;
        f2["owner"]["display_name"] = true;
        const bool pl = !strcmp(type, "playlist");
        if (!getJson("https://api.spotify.com/v1/" + String(pl ? "playlists/" : "artists/") + id +
                         (pl ? "?fields=name,images,owner(display_name)" : ""), d2, f2)) {
          if (nSkip < 8) skip[nSkip++] = uri;  // Spotify's own playlists answer 404 to apps: ask once per fetch
          continue;
        }
        snprintf(item.name, sizeof item.name, "%s", (const char*)(d2["name"] | ""));
        snprintf(item.sub, sizeof item.sub, "%s", pl ? (const char*)(d2["owner"]["display_name"] | "Playlist") : "Artist");
        img = coverUrl(d2["images"]);
      } else {
        continue;  // shows, audiobooks: not on this screen
      }
      libUri[n] = uri;
      libCover[n] = img;
      n++;
    }
  }
  lib.nRecent = n;
  JsonDocument f3, d3(&psram);
  for (const char* k : {"uri", "name"}) f3["items"][0][k] = true;
  f3["items"][0]["owner"]["display_name"] = true;
  f3["items"][0]["images"][0]["url"] = true;
  f3["items"][0]["images"][0]["width"] = true;
  int m = 0;
  const bool listsOk = getJson("https://api.spotify.com/v1/me/playlists?limit=" + String(SPOTIFY_LISTS), d3, f3);
  if (!listsOk) snprintf(lib.error[1], sizeof lib.error[1], "%s", lastError);
  if (listsOk)
    for (JsonVariantConst p : d3["items"].as<JsonArrayConst>()) {
      if (m == SPOTIFY_LISTS) break;
      SpotifyItem& item = lib.item[SPOTIFY_RECENT + m];
      snprintf(item.name, sizeof item.name, "%s", (const char*)(p["name"] | ""));
      snprintf(item.sub, sizeof item.sub, "%s", (const char*)(p["owner"]["display_name"] | "Playlist"));
      libUri[SPOTIFY_RECENT + m] = p["uri"] | "";
      libCover[SPOTIFY_RECENT + m] = coverUrl(p["images"]);
      m++;
    }
  lib.nLists = m;
  lib.known = true;
  Serial.printf("spotify: library: %d recent, %d playlists\n", n, m);
  xSemaphoreTake(artLock, portMAX_DELAY);
  libShared = lib;
  libFresh = true;
  xSemaphoreGive(artLock);
  nextCover = 0;
}

// One cover per call, once loop() has taken the previous one.
static void fetchCover() {
  while (nextCover < SPOTIFY_ITEMS && !libCover[nextCover].length()) nextCover++;
  if (nextCover >= SPOTIFY_ITEMS || coverIdx >= 0) return;
  const int i = nextCover++;
  String jpg;
  if (!fetchImage(libCover[i], jpg)) return;
  xSemaphoreTake(artLock, portMAX_DELAY);
  if (uiSpotifyCoverDecode(cover, (const uint8_t*)jpg.c_str(), jpg.length())) coverIdx = i;
  xSemaphoreGive(artLock);
}

static void fetchDevices() {
  JsonDocument filter, doc(&psram);
  for (const char* k : {"id", "name", "type", "is_active", "volume_percent"}) filter["devices"][0][k] = true;
  if (!getJson("https://api.spotify.com/v1/me/player/devices", doc, filter)) return;
  devs = SpotifyDevices();
  for (JsonVariantConst d : doc["devices"].as<JsonArrayConst>()) {
    if (devs.n == 8) break;
    SpotifyDevice& x = devs.d[devs.n];
    snprintf(x.name, sizeof x.name, "%s", (const char*)(d["name"] | ""));
    snprintf(x.type, sizeof x.type, "%s", (const char*)(d["type"] | ""));
    x.active = d["is_active"] | false;
    x.volume = d["volume_percent"] | -1;
    devId[devs.n++] = d["id"] | "";
  }
  devs.known = true;
  xSemaphoreTake(artLock, portMAX_DELAY);
  devShared = devs;
  devFresh = true;
  xSemaphoreGive(artLock);
}

// Play a library item. With nothing active Spotify answers 404: then play it on the first device it knows of,
// a computer if there is one.
static void playContext(int i) {
  if (i < 0 || i >= SPOTIFY_ITEMS || !libUri[i].length()) return;
  const String body = "{\"context_uri\":\"" + libUri[i] + "\"}";
  String reply;
  int code = request("PUT", "https://api.spotify.com/v1/me/player/play", body, &reply);
  if (code == 404) {
    fetchDevices();
    int pick = -1;
    for (int k = 0; k < devs.n; k++)
      if (pick < 0 || (!strcmp(devs.d[k].type, "Computer") && strcmp(devs.d[pick].type, "Computer"))) pick = k;
    if (pick >= 0) code = request("PUT", "https://api.spotify.com/v1/me/player/play?device_id=" + devId[pick], body, &reply);
    else setError("No device: open Spotify on a phone, PC or speaker");
  }
  Serial.printf("spotify: play %s -> HTTP %d\n", libUri[i].c_str(), code);
}

static void moveTo(int i) {
  if (i < 0 || i >= devs.n) return;
  String reply;
  int code = request("PUT", "https://api.spotify.com/v1/me/player", "{\"device_ids\":[\"" + devId[i] + "\"],\"play\":true}", &reply);
  Serial.printf("spotify: play on %s -> HTTP %d\n", devs.d[i].name, code);
  devAt = millis() + 1000;  // show the move soon
}

// GET /me/player into the state. Returns whether something is playing (for the poll interval).
static bool poll() {
  static JsonDocument filter;
  if (filter.isNull()) {
    for (const char* k : {"is_playing", "progress_ms", "shuffle_state", "repeat_state", "currently_playing_type"})
      filter[k] = true;
    for (const char* k : {"name", "volume_percent", "supports_volume"}) filter["device"][k] = true;
    for (const char* k : {"id", "type", "name", "duration_ms"}) filter["item"][k] = true;
    filter["item"]["artists"][0]["name"] = true;
    filter["item"]["album"]["name"] = true;
    filter["item"]["album"]["images"][0]["url"] = true;
    filter["item"]["show"]["name"] = true;  // podcast episodes: the show, and the episode's own images
    filter["item"]["images"][0]["url"] = true;
  }
  const uint32_t gen = cmdGen;
  String reply;
  int code = request("GET", "https://api.spotify.com/v1/me/player?additional_types=episode", "", &reply);
  if (code == 401) { tokenUntil = millis(); return false; }  // the token expired early: refresh, then poll again
  if (code != 200 && code != 204) { apiError("Spotify", code, reply); return false; }
  Spotify s;
  String img;
  if (code == 200) {  // 204: no device playing or paused
    JsonDocument doc(&psram);
    if (DeserializationError e = deserializeJson(doc, reply, DeserializationOption::Filter(filter))) {
      Serial.printf("spotify: JSON: %s\n", e.c_str());
      return false;
    }
    JsonVariantConst dev = doc["device"], item = doc["item"];
    s.active = true;
    s.playing = doc["is_playing"] | false;
    s.shuffle = doc["shuffle_state"] | false;
    const char* rep = doc["repeat_state"] | "off";
    s.repeat = !strcmp(rep, "track") ? 2 : !strcmp(rep, "context") ? 1 : 0;
    s.progressMs = doc["progress_ms"] | 0;
    s.volume = dev["volume_percent"] | -1;
    s.canVolume = dev["supports_volume"] | false;
    snprintf(s.device, sizeof s.device, "%s", (const char*)(dev["name"] | "a device"));
    static char logged[48] = "";  // log the device once, with whether its volume can be set
    char now[48];
    snprintf(now, sizeof now, "%s/%d/%d", s.device, s.canVolume, s.volume >= 0);
    if (strcmp(now, logged)) {
      strcpy(logged, now);
      Serial.printf("spotify: playing on %s, volume %s\n", s.device,
                    s.canVolume && s.volume >= 0 ? "settable" : s.canVolume ? "not reported" : "fixed by the device");
    }
    if (item.isNull()) {  // an ad, or something the API doesn't describe
      const char* type = doc["currently_playing_type"] | "";
      snprintf(s.title, sizeof s.title, "%s", !strcmp(type, "ad") ? "Advertisement" : "Playing");
    } else {
      snprintf(s.id, sizeof s.id, "%s", (const char*)(item["id"] | ""));
      snprintf(s.title, sizeof s.title, "%s", (const char*)(item["name"] | ""));
      s.durationMs = item["duration_ms"] | 0;
      const bool episode = !strcmp(item["type"] | "", "episode");
      size_t n = 0;
      if (episode) n = snprintf(s.artist, sizeof s.artist, "%s", (const char*)(item["show"]["name"] | ""));
      for (JsonVariantConst a : item["artists"].as<JsonArrayConst>())
        if (n < sizeof s.artist) n += snprintf(s.artist + n, sizeof s.artist - n, "%s%s", n ? ", " : "", (const char*)(a["name"] | ""));
      snprintf(s.album, sizeof s.album, "%s", episode ? "Podcast" : (const char*)(item["album"]["name"] | ""));
      img = (episode ? item["images"][0]["url"] : item["album"]["images"][0]["url"]) | "";  // the largest, 640 px
      img.replace("https://", "http://");
      char uri[64];
      snprintf(uri, sizeof uri, "spotify%%3A%s%%3A%s", episode ? "episode" : "track", s.id);
      if (*s.id && strcmp(uri, likedUri)) {  // a new track: is it in Liked Songs?
        strcpy(likedUri, uri);
        liked = false;
        String r;
        if (request("GET", "https://api.spotify.com/v1/me/library/contains?uris=" + String(uri), "", &r) == 200)
          liked = r.indexOf("true") >= 0;
      }
    }
  }
  if (!*s.id) likedUri[0] = 0;
  s.liked = liked;
  setArt(img);
  up = true;
  portENTER_CRITICAL(&mux);
  if (gen == cmdGen) {  // otherwise a control was tapped meanwhile: keep its effect until the next poll
    state = s;
    stateMs = millis();
    fresh = true;
  }
  portEXIT_CRITICAL(&mux);
  return s.playing;
}

static void send(const Cmd& c) {
  if (c.cmd == SP_CONTEXT) { playContext(c.arg); return; }
  if (c.cmd == SP_DEVICE) { moveTo(c.arg); return; }
  static const char* const repeatL[] = {"off", "context", "track"};
  const String api = "https://api.spotify.com/v1/me/";
  const char* method = "PUT";
  String url;
  switch (c.cmd) {
    case SP_PLAY: url = api + "player/play"; break;
    case SP_PAUSE: url = api + "player/pause"; break;
    case SP_NEXT: method = "POST"; url = api + "player/next"; break;
    case SP_PREV: method = "POST"; url = api + "player/previous"; break;
    case SP_SEEK: url = api + "player/seek?position_ms=" + c.arg; break;
    case SP_VOLUME: url = api + "player/volume?volume_percent=" + c.arg; break;
    case SP_SHUFFLE: url = api + "player/shuffle?state=" + (c.arg ? "true" : "false"); break;
    case SP_REPEAT: url = api + "player/repeat?state=" + repeatL[c.arg % 3]; break;
    case SP_LIKE:
      if (!*likedUri) return;
      method = c.arg ? "PUT" : "DELETE";
      url = api + "library?uris=" + likedUri;
      break;
    default: return;
  }
  String reply;
  int code = request(method, url, "", &reply);
  if (code == 401 && refreshToken()) code = request(method, url, "", &reply);
  if (code >= 200 && code < 300 && c.cmd == SP_LIKE) liked = c.arg;
  // Errors: mostly 404 (no active device) or 403 (the device or track doesn't allow it). The next poll undoes the tap.
  JsonDocument doc(&psram);
  Serial.printf("spotify: %s %s -> HTTP %d %s\n", method, url.substring(api.length()).c_str(), code,
                deserializeJson(doc, reply) ? "" : (const char*)(doc["error"]["message"] | ""));
}

static void closeAll() {
  tlsHttp.end();
  tlsNet.stop();  // frees the TLS buffers, whatever state the connection was in
  tlsHost = "";
  artHttp.end();
  artNet.stop();
}

static void task(void*) {
  static const char* keep[] = {"Retry-After"};
  tlsHttp.collectHeaders(keep, 1);
  uint32_t pollAt = 0;
  for (;;) {
    if (!(wantMs && millis() - wantMs < 30000)) break;  // off screen for 30 s, or spotifyStop()
    if (WiFi.status() != WL_CONNECTED || int32_t(millis() - waitUntil) < 0) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
    active = true;
    if (!token.length() || int32_t(millis() - tokenUntil) >= 0) {
      if (!refreshToken()) { waitUntil = millis() + 30000; continue; }
    }
    Cmd c;
    while (xQueueReceive(cmds, &c, 0) == pdTRUE) { send(c); pollAt = millis() + 700; }  // Spotify takes a moment
    if (int32_t(millis() - pollAt) >= 0) pollAt = millis() + (poll() ? 2000 : 5000);
    static bool libShown = false;  // fetch each time the sheet opens, and every 5 min while it shows
    if (libWanted && (!libShown || millis() - libAt > 5 * 60000)) { fetchLibrary(); libAt = millis(); }
    libShown = libWanted;
    if (devWanted && int32_t(millis() - devAt) >= 0) { fetchDevices(); devAt = millis() + 5000; }
    fetchCover();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  closeAll();
  Cmd c;
  while (xQueueReceive(cmds, &c, 0) == pdTRUE) {}  // taps on a screen that's gone
  Serial.printf("spotify: stopped (RAM free %u KB, lowest %u KB)\n", ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024);
  active = false;
  running = false;
  vTaskDelete(nullptr);
}

void spotifyBegin() {
  if (!*SPOTIFY_REFRESH) return;
  cmds = xQueueCreate(4, sizeof(Cmd));
  artLock = xSemaphoreCreateMutex();
  tlsNet.setInsecure();
}

void spotifyWant(bool on) {
  if (!on || !cmds) return;
  wantMs = millis();
  if (running) return;
  running = true;
  // Idle priority, like the radar task: HTTPClient busy-waits for slow data, and the 5 s task watchdog checks the
  // idle task. Deep stack: TLS handshakes.
  xTaskCreatePinnedToCore(task, "spotify", 16384, nullptr, tskIDLE_PRIORITY, nullptr, 0);
}

void spotifyStop() { wantMs = 0; }
void spotifyBrowse(bool library, bool devices) {
  libWanted = library;
  if (devices && !devWanted) devAt = 0;  // just opened: ask now
  devWanted = devices;
}
bool spotifyOpen() { return active; }
bool spotifyUp() { return up; }

void spotifyCommand(uint8_t cmd, int arg) {
  if (!cmds) return;
  portENTER_CRITICAL(&mux);
  Spotify& s = state;
  uint32_t now = millis();
  if (s.playing) s.progressMs = std::min(s.durationMs, s.progressMs + (now - stateMs));
  stateMs = now;
  switch (cmd) {
    case SP_PLAY: s.playing = true; break;
    case SP_PAUSE: s.playing = false; break;
    case SP_SEEK: s.progressMs = arg; break;
    case SP_VOLUME: s.volume = arg; break;
    case SP_SHUFFLE: s.shuffle = arg; break;
    case SP_REPEAT: s.repeat = arg; break;
    case SP_LIKE: s.liked = arg; break;
  }
  cmdGen++;
  fresh = true;
  portEXIT_CRITICAL(&mux);
  Cmd c{cmd, arg};
  xQueueSend(cmds, &c, 0);
}

bool spotifyPump() {
  if (!cmds) return false;
  bool redraw = false;
  if (xSemaphoreTake(artLock, 0) == pdTRUE) {
    if (artFresh) { uiSpotifyArt(art, artTint); artFresh = false; redraw = true; }
    if (libFresh) { uiSpotifyLibrary(libShared); libFresh = false; redraw = true; }
    if (devFresh) { uiSpotifyDevices(devShared); devFresh = false; redraw = true; }
    if (coverIdx >= 0) { uiSpotifyCover(coverIdx, cover); coverIdx = -1; redraw = true; }
    xSemaphoreGive(artLock);
  }
  // Hand the state over when it changed, and once a second while playing so the progress bar moves.
  static Spotify s;  // static: ~400 bytes, off the loop task's stack
  static uint32_t handedMs = 0;
  uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  bool take = fresh || (state.playing && now - handedMs >= 1000);
  if (take) {
    s = state;
    fresh = false;
    if (s.playing) s.progressMs = std::min(s.durationMs, s.progressMs + (now - stateMs));
  }
  portEXIT_CRITICAL(&mux);
  if (take) { uiSpotify(s); handedMs = now; redraw = true; }
  return redraw && uiSpotifyWanted();
}
