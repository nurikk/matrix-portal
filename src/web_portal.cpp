#include "web_portal.h"
#include "weather_state.h"

#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3) && !defined(MATRIX_BENCHMARK)

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiProv.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>   // brings in AsyncWebSocket
#include <ArduinoJson.h>
#include <GeoIP.h>
#include <time.h>
#include <timezonedb_lookup.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "life_settings.h"

extern LifeSettings gLive;
extern LifeSettings gSaved;
extern LifeSettings gDefaults;
extern volatile bool gReqReseed;
extern volatile bool gReqForget;
extern volatile int8_t gReqPause;
extern volatile bool gReqClear;
extern volatile uint8_t gReqClockAnimation;
extern volatile bool gPaused;
extern volatile uint16_t gStatRenderFps;
extern volatile uint16_t gStatLifeUps;
extern uint16_t liveCells;
extern uint32_t generation;
extern uint8_t panelWidth;
extern uint8_t panelHeight;
extern int gGeoBitDepth;
extern int gGeoTile;

// Defined in life_render.h (the core-1 Life translation unit). Packs the active
// panel's RGB565 image into dst[panelWidth*panelHeight]. Reading it from core 0 is a
// documented benign cross-core race (see the comment on the definition).
void copyDrawnFrame(uint16_t *dst);
size_t measureDrawnFrameRlePayload();
size_t copyDrawnFrameRle(uint8_t *dst, size_t capacity);

// Defined in life_sim.h (core 1). Injects browser-drawn cells from a tight row-major
// bitmask into the current generation. Called ONLY from webPortalTick() on core 1 (which
// owns the cell arrays); core 0 merely stages the bitmask and sets gReqDraw.
void applyDrawnCells(const uint8_t *mask, uint8_t w, uint8_t h);

#include "web_ui.h"

void webServerStart();  // defined below; called from onWiFiEvent

// --- connectivity state (read by main.cpp for the one-time IP scroll) ---
volatile bool gShowIpScroll = false;
char gIpText[32] = {0};

namespace {
constexpr uint16_t kSettingsVersion = 4;   // bumped: removed retired wave/knock-origin tuning (struct layout changed)
constexpr const char *kNvsNamespace = "matrixlife";
constexpr const char *kKeyVersion = "ver";
constexpr const char *kKeyBlob = "settings";
constexpr const char *kKeyClockTimezone = "clockTz";
constexpr const char *kKeyClockAuto = "clockAuto";
constexpr const char *kKeyWeatherEnabled = "wxEn";
constexpr const char *kKeyWeatherAuto = "wxAuto";
constexpr const char *kKeyWeatherUnitsF = "wxUnitsF";
constexpr const char *kKeyWeatherLat = "wxLat";
constexpr const char *kKeyWeatherLon = "wxLon";
constexpr const char *kProvServiceName = "PROV_MatrixLife";
constexpr const char *kProvPop = "matrixlife";   // proof-of-possession entered in the app
constexpr const char *kMdnsHost = "matrixportal";
constexpr size_t kTimezoneNameBytes = 40;
constexpr const char *kDefaultTimezone = "UTC";
constexpr const char *kNtpServer1 = "pool.ntp.org";
constexpr const char *kNtpServer2 = "time.nist.gov";
constexpr const char *kNtpServer3 = "time.google.com";
constexpr uint32_t kWeatherCacheMs = 20UL * 60UL * 1000UL;
constexpr uint32_t kWeatherHttpTimeoutMs = 7000;
constexpr const char *kWeatherProvider = "Open-Meteo";

struct ClockSettings {
  char timezone[kTimezoneNameBytes];
  uint8_t autoTimezone;
};

WeatherSettings defaultWeatherSettings() {
  WeatherSettings s = {};
  s.enabled = 1;
  s.autoLocation = 1;
  s.unitsF = 0;
  s.latitudeE6 = kWeatherCoordUnset;
  s.longitudeE6 = kWeatherCoordUnset;
  return s;
}

ClockSettings defaultClockSettings() {
  ClockSettings s = {};
  snprintf(s.timezone, sizeof(s.timezone), "%s", kDefaultTimezone);
  s.autoTimezone = 1;
  return s;
}

portMUX_TYPE gSettingsMux = portMUX_INITIALIZER_UNLOCKED;
LifeSettings gPending;
volatile bool gPendingDirty = false;
ClockSettings gClockDefaults = defaultClockSettings();
ClockSettings gClockSaved = defaultClockSettings();
ClockSettings gClockLive = defaultClockSettings();
ClockSettings gClockPending = defaultClockSettings();
volatile bool gClockPendingDirty = false;
WeatherSettings gWeatherDefaults = defaultWeatherSettings();
WeatherSettings gWeatherSaved = defaultWeatherSettings();
WeatherSettings gWeatherLive = defaultWeatherSettings();
WeatherSettings gWeatherPending = defaultWeatherSettings();
volatile bool gWeatherPendingDirty = false;
char gDetectedTimezone[kTimezoneNameBytes] = "";
char gClockStatus[48] = "clock not synced";
char gClockPosix[64] = "UTC0";
WeatherSnapshot gWeatherSnapshot = {true, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "weather not fetched"};
uint32_t gWeatherLastFetchAt = 0;
int32_t gWeatherLastLatE6 = kWeatherCoordUnset;
int32_t gWeatherLastLonE6 = kWeatherCoordUnset;
TaskHandle_t gLocationDetectTask = nullptr;
TaskHandle_t gWeatherFetchTask = nullptr;
bool gNtpStarted = false;
volatile bool gReqWeatherRefresh = false;
uint32_t gWeatherRefreshDueAt = 0;

void broadcastSchema();
void broadcastClock(const ClockSettings &s, const char *source);
void broadcastWeather(const WeatherSettings &s, const char *source);
void locationRequestDetect(bool saveDetected, bool updateClock, bool updateWeather);
void weatherRequestDetect(bool saveDetected);
void scheduleWeatherRefresh(uint32_t delayMs);

void copyString(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  snprintf(dst, cap, "%s", src ? src : "");
}

bool isUtf8Continuation(uint8_t c) {
  return (c & 0xC0) == 0x80;
}

size_t utf8SequenceBytes(const char *src, size_t remaining) {
  if (remaining == 0) return 0;
  uint8_t c0 = (uint8_t)src[0];
  if (c0 < 0x80) return 1;
  if (c0 >= 0xC2 && c0 <= 0xDF) {
    return remaining >= 2 && isUtf8Continuation((uint8_t)src[1]) ? 2 : 0;
  }
  if (remaining < 3) return 0;
  uint8_t c1 = (uint8_t)src[1];
  uint8_t c2 = (uint8_t)src[2];
  if (c0 == 0xE0) {
    return c1 >= 0xA0 && c1 <= 0xBF && isUtf8Continuation(c2) ? 3 : 0;
  }
  if (c0 >= 0xE1 && c0 <= 0xEC) {
    return isUtf8Continuation(c1) && isUtf8Continuation(c2) ? 3 : 0;
  }
  if (c0 == 0xED) {
    return c1 >= 0x80 && c1 <= 0x9F && isUtf8Continuation(c2) ? 3 : 0;
  }
  if (c0 >= 0xEE && c0 <= 0xEF) {
    return isUtf8Continuation(c1) && isUtf8Continuation(c2) ? 3 : 0;
  }
  if (remaining < 4) return 0;
  uint8_t c3 = (uint8_t)src[3];
  if (c0 == 0xF0) {
    return c1 >= 0x90 && c1 <= 0xBF && isUtf8Continuation(c2) && isUtf8Continuation(c3) ? 4 : 0;
  }
  if (c0 >= 0xF1 && c0 <= 0xF3) {
    return isUtf8Continuation(c1) && isUtf8Continuation(c2) && isUtf8Continuation(c3) ? 4 : 0;
  }
  if (c0 == 0xF4) {
    return c1 >= 0x80 && c1 <= 0x8F && isUtf8Continuation(c2) && isUtf8Continuation(c3) ? 4 : 0;
  }
  return 0;
}

void copyUtf8String(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  if (!src) { dst[0] = '\0'; return; }

  size_t inLen = strlen(src);
  size_t in = 0;
  size_t out = 0;
  while (in < inLen && out + 1 < cap) {
    size_t n = utf8SequenceBytes(src + in, inLen - in);
    if (n == 0) {
      dst[out++] = '?';
      in++;
      continue;
    }
    if (out + n >= cap) break;  // never split a multibyte codepoint
    memcpy(dst + out, src + in, n);
    out += n;
    in += n;
  }
  dst[out] = '\0';
}

void writeWeatherCity(JsonObject obj, const char *key, const char *city) {
  char safe[kWeatherCityBytes];
  copyUtf8String(safe, sizeof(safe), city);
  obj[key] = safe;
}

void writeWeatherStatus(JsonObject obj, const char *key, const char *status) {
  char safe[kWeatherStatusBytes];
  copyUtf8String(safe, sizeof(safe), status);
  obj[key] = safe;
}

bool weatherHasLocation(const WeatherSettings &s) {
  return s.latitudeE6 != kWeatherCoordUnset && s.longitudeE6 != kWeatherCoordUnset;
}

bool normalizeWeatherCoord(float lat, float lon, int32_t &latE6, int32_t &lonE6) {
  if (!isfinite(lat) || !isfinite(lon)) return false;
  if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) return false;
  latE6 = static_cast<int32_t>(lat * 1000000.0f + (lat >= 0.0f ? 0.5f : -0.5f));
  lonE6 = static_cast<int32_t>(lon * 1000000.0f + (lon >= 0.0f ? 0.5f : -0.5f));
  return true;
}

int16_t weatherTenths(float v) {
  return static_cast<int16_t>(v * 10.0f + (v >= 0.0f ? 0.5f : -0.5f));
}

uint8_t weatherPercent(long v) {
  if (v < 0) return 0;
  if (v > 100) return 100;
  return static_cast<uint8_t>(v);
}

uint16_t weatherSpeedTenths(float v) {
  if (!isfinite(v) || v < 0.0f) return 0;
  if (v > 999.9f) return 9999;
  return static_cast<uint16_t>(v * 10.0f + 0.5f);
}

bool parseFloatText(const char *text, float &out) {
  if (!text || !*text) return false;
  char *end = nullptr;
  out = strtof(text, &end);
  while (end && *end && isspace((unsigned char)*end)) end++;
  return end && *end == '\0' && isfinite(out);
}

void formatWeatherCoord(int32_t e6, char *out, size_t cap) {
  if (!out || cap == 0) return;
  bool neg = e6 < 0;
  int32_t absValue = neg ? -e6 : e6;
  snprintf(out, cap, "%s%ld.%06ld", neg ? "-" : "",
           static_cast<long>(absValue / 1000000L), static_cast<long>(absValue % 1000000L));
}

void setWeatherStatus(const char *status) {
  taskENTER_CRITICAL(&gSettingsMux);
  copyString(gWeatherSnapshot.status, sizeof(gWeatherSnapshot.status), status);
  taskEXIT_CRITICAL(&gSettingsMux);
}

void setWeatherInvalidStatus(const char *status) {
  taskENTER_CRITICAL(&gSettingsMux);
  gWeatherSnapshot.valid = false;
  copyString(gWeatherSnapshot.status, sizeof(gWeatherSnapshot.status), status);
  taskEXIT_CRITICAL(&gSettingsMux);
}

void weatherSyncSnapshotSettings(const WeatherSettings &s) {
  taskENTER_CRITICAL(&gSettingsMux);
  gWeatherSnapshot.enabled = s.enabled != 0;
  gWeatherSnapshot.unitsF = s.unitsF != 0;
  if (!gWeatherSnapshot.enabled) {
    gWeatherSnapshot.valid = false;
    copyString(gWeatherSnapshot.status, sizeof(gWeatherSnapshot.status), "weather disabled");
  } else if (!weatherHasLocation(s)) {
    gWeatherSnapshot.valid = false;
    copyString(gWeatherSnapshot.status, sizeof(gWeatherSnapshot.status), "weather location needed");
  }
  taskEXIT_CRITICAL(&gSettingsMux);
}

void publishWeatherPending(const WeatherSettings &s) {
  taskENTER_CRITICAL(&gSettingsMux);
  gWeatherPending = s;
  gWeatherPendingDirty = true;
  taskEXIT_CRITICAL(&gSettingsMux);
}

bool lookupClockPosix(const char *timezone, const char **posix) {
  if (!timezone || !*timezone) return false;
  const char *found = lookup_posix_timezone_tz(timezone);
  if (!found) {
    char lookup[kTimezoneNameBytes];
    copyString(lookup, sizeof(lookup), timezone);
    for (size_t i = 0; lookup[i]; i++) {
      if (lookup[i] == '_') lookup[i] = ' ';
    }
    found = lookup_posix_timezone_tz(lookup);
  }
  if (!found) return false;
  if (posix) *posix = found;
  return true;
}

bool normalizeClockTimezone(const char *in, char *out, size_t cap) {
  if (!in || !out || cap == 0) return false;
  while (*in && isspace((unsigned char)*in)) in++;
  size_t len = strlen(in);
  while (len > 0 && isspace((unsigned char)in[len - 1])) len--;
  if (len == 0 || len >= cap) return false;

  for (size_t i = 0; i < len; i++) {
    char c = in[i];
    if (c == ' ') c = '_';
    if (!(isalnum((unsigned char)c) || c == '/' || c == '_' || c == '-' || c == '+' || c == '.')) return false;
    out[i] = c;
  }
  out[len] = '\0';
  return lookupClockPosix(out, nullptr);
}

void setClockStatus(const char *status) {
  taskENTER_CRITICAL(&gSettingsMux);
  copyString(gClockStatus, sizeof(gClockStatus), status);
  taskEXIT_CRITICAL(&gSettingsMux);
}

void clockApplyTimezone(const char *timezone) {
  const char *posix = nullptr;
  if (!lookupClockPosix(timezone, &posix)) {
    timezone = kDefaultTimezone;
    posix = "UTC0";
    setClockStatus("unknown timezone; using UTC");
  } else {
    setClockStatus(gNtpStarted ? "clock syncing" : "timezone ready");
  }

  setenv("TZ", posix, 1);
  tzset();
  taskENTER_CRITICAL(&gSettingsMux);
  copyString(gClockPosix, sizeof(gClockPosix), posix);
  taskEXIT_CRITICAL(&gSettingsMux);
}

void clockStartNtp() {
  configTime(0, 0, kNtpServer1, kNtpServer2, kNtpServer3);
  gNtpStarted = true;
  setClockStatus("clock syncing");
}

bool clockReadLocal(char *out, size_t cap) {
  time_t now = time(nullptr);
  if (now < 1609459200) return false;   // before 2021 means SNTP has not set wall time yet
  struct tm local;
  localtime_r(&now, &local);
  return strftime(out, cap, "%Y-%m-%d %H:%M:%S", &local) > 0;
}

void clockSaveToNvs(const ClockSettings &s) {
  Preferences prefs;
  if (prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    prefs.putString(kKeyClockTimezone, s.timezone);
    prefs.putBool(kKeyClockAuto, s.autoTimezone != 0);
    prefs.end();
    taskENTER_CRITICAL(&gSettingsMux);
    gClockSaved = s;
    taskEXIT_CRITICAL(&gSettingsMux);
  }
}

void weatherSaveToNvs(const WeatherSettings &s) {
  Preferences prefs;
  if (prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    prefs.putBool(kKeyWeatherEnabled, s.enabled != 0);
    prefs.putBool(kKeyWeatherAuto, s.autoLocation != 0);
    prefs.putBool(kKeyWeatherUnitsF, s.unitsF != 0);
    prefs.putInt(kKeyWeatherLat, s.latitudeE6);
    prefs.putInt(kKeyWeatherLon, s.longitudeE6);
    prefs.end();
    taskENTER_CRITICAL(&gSettingsMux);
    gWeatherSaved = s;
    taskEXIT_CRITICAL(&gSettingsMux);
  }
}

void publishClockPending(const ClockSettings &s) {
  taskENTER_CRITICAL(&gSettingsMux);
  gClockPending = s;
  gClockPendingDirty = true;
  taskEXIT_CRITICAL(&gSettingsMux);
}

void onWiFiEvent(arduino_event_t *e) {
  switch (e->event_id) {
    case ARDUINO_EVENT_PROV_START:
      Serial.println("[prov] BLE provisioning started — open the ESP BLE Provisioning app");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      IPAddress ip = WiFi.localIP();
      snprintf(gIpText, sizeof(gIpText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      Serial.printf("[wifi] connected: http://%s/  (http://%s.local/)\n", gIpText, kMdnsHost);
      if (MDNS.begin(kMdnsHost)) {
        MDNS.addService("http", "tcp", 80);
      }
      gShowIpScroll = true;       // main loop scrolls it once
      webServerStart();           // forward-declared above; defined below
      clockStartNtp();
      if (gClockSaved.autoTimezone || gWeatherSaved.autoLocation) {
        locationRequestDetect(/*saveDetected=*/true, gClockSaved.autoTimezone != 0,
                              gWeatherSaved.autoLocation != 0);
      } else {
        scheduleWeatherRefresh(10000);
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      WiFi.reconnect();
      break;
    default:
      break;
  }
}
}  // namespace

void settingsLoadFromNvs() {
  gSaved = defaultLifeSettings();
  gClockSaved = gClockDefaults;
  gWeatherSaved = gWeatherDefaults;

  Preferences prefs;
  if (prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
    LifeSettings tmp;
    uint16_t ver = prefs.getUShort(kKeyVersion, 0);
    size_t got = prefs.getBytesLength(kKeyBlob);
    if (ver == kSettingsVersion && got == sizeof(LifeSettings)) {
      prefs.getBytes(kKeyBlob, &tmp, sizeof(LifeSettings));
      clampSettings(tmp);
      gSaved = tmp;
    }

    String tz = prefs.getString(kKeyClockTimezone, "");
    char normalized[kTimezoneNameBytes];
    if (normalizeClockTimezone(tz.c_str(), normalized, sizeof(normalized))) {
      copyString(gClockSaved.timezone, sizeof(gClockSaved.timezone), normalized);
    }
    gClockSaved.autoTimezone = prefs.getBool(kKeyClockAuto, true) ? 1 : 0;
    gWeatherSaved.enabled = prefs.getBool(kKeyWeatherEnabled, true) ? 1 : 0;
    gWeatherSaved.autoLocation = prefs.getBool(kKeyWeatherAuto, true) ? 1 : 0;
    gWeatherSaved.unitsF = prefs.getBool(kKeyWeatherUnitsF, false) ? 1 : 0;
    gWeatherSaved.latitudeE6 = prefs.getInt(kKeyWeatherLat, kWeatherCoordUnset);
    gWeatherSaved.longitudeE6 = prefs.getInt(kKeyWeatherLon, kWeatherCoordUnset);
    if (gWeatherSaved.latitudeE6 < -90000000L || gWeatherSaved.latitudeE6 > 90000000L ||
        gWeatherSaved.longitudeE6 < -180000000L || gWeatherSaved.longitudeE6 > 180000000L) {
      gWeatherSaved.latitudeE6 = kWeatherCoordUnset;
      gWeatherSaved.longitudeE6 = kWeatherCoordUnset;
    }
    prefs.end();
  }
  gLive = gSaved;
  gClockLive = gClockSaved;
  gClockPending = gClockLive;
  gClockPendingDirty = false;
  gWeatherLive = gWeatherSaved;
  gWeatherPending = gWeatherLive;
  gWeatherPendingDirty = false;
  clockApplyTimezone(gClockLive.timezone);
  weatherSyncSnapshotSettings(gWeatherLive);
}

void settingsSaveToNvs(const LifeSettings &s) {
  Preferences prefs;
  if (prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    prefs.putUShort(kKeyVersion, kSettingsVersion);
    prefs.putBytes(kKeyBlob, &s, sizeof(LifeSettings));
    prefs.end();
    gSaved = s;
  }
}

void settingsClearNvs() {
  Preferences prefs;
  if (prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    prefs.clear();
    prefs.end();
  }
}

void webPortalBegin() {
  settingsLoadFromNvs();
  WiFi.onEvent(onWiFiEvent);
  // WIFI_PROV_SCHEME_HANDLER_FREE_BTDM frees the BT controller memory once
  // provisioning ends. If already provisioned, this connects with stored creds
  // and never starts BLE.
  WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                          WIFI_PROV_SECURITY_1, kProvPop, kProvServiceName);
  WiFiProv.printQR(kProvServiceName, kProvPop, "ble");  // QR + payload to Serial
}

namespace {
AsyncWebServer gServer(80);
AsyncWebSocket gWs("/ws");
TaskHandle_t gPushTask = nullptr;

constexpr size_t kMaxWsFrameBytes = 512;   // control frames are small; reject larger pre-parse
constexpr uint32_t kStatsPushMs = 500;

// --- Live board mirror (binary frames) ---
// The board is streamed to browsers as binary WS frames (JSON stays on text frames, so
// the two never collide). Layout, all little-endian:
//   [0]    magic 0x4C ('L')   [1] version   [2..3] width   [4..5] height
//   [6..]  width*height RGB565 pixels, row-major (same bit layout as Adafruit GFX 565).
// RLE frames use magic 'R', the same header, then repeated uint16 runLength + uint16 RGB565
// color pairs. A frame is sent as RLE only when it is smaller than the raw RGB565 payload.
constexpr uint32_t kFramePushMs = 100;     // ~10 fps board mirror — smooth, LAN-cheap
constexpr uint8_t kFrameMagic = 0x4C;      // 'L'
constexpr uint8_t kRleFrameMagic = 0x52;   // 'R'
constexpr uint8_t kFrameVersion = 1;
constexpr size_t kFrameHeaderBytes = 6;

uint32_t gLastFrameBytes = 0;
uint32_t gRawFrameBytes = 0;
bool gLastFrameWasRle = false;

// --- Browser-drawn cells (binary frames, browser → firmware) ---
// Same 6-byte header layout as the download frame, but magic 'D'. The payload is a tight
// row-major bitmask packed by width (bitIndex = y*width + x, LSB-first); a set bit paints
// cell (x,y) alive. The buffer is sized for the firmware's documented max panel (128×128,
// enforced by the #error guard in life_state.h), so it is geometry-independent.
constexpr uint8_t kDrawFrameMagic = 0x44;   // 'D'
constexpr uint8_t kDrawFrameVersion = 1;
constexpr size_t kDrawMaskBytes = (128u * 128u + 7u) / 8u;   // 2048; covers any supported panel
constexpr size_t kMaxWsDrawBytes = kFrameHeaderBytes + kDrawMaskBytes;

// Single-producer (core 0 WS callback) / single-consumer (core 1 webPortalTick) hand-off
// for a browser-drawn frame. The critical sections guard only gReqDraw — they are the
// memory barrier. Core 0 fills gDrawBitmask only while !gReqDraw, and core 1 clears
// gReqDraw only AFTER it finishes reading, so the buffer is never written and read at once.
portMUX_TYPE gDrawMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t gDrawBitmask[kDrawMaskBytes];
volatile bool gReqDraw = false;

void writeWeatherCoord(JsonObject obj, const char *key, int32_t e6) {
  if (e6 == kWeatherCoordUnset) {
    obj[key] = "";
    return;
  }
  char text[18];
  formatWeatherCoord(e6, text, sizeof(text));
  obj[key] = text;
}

void writeWeatherObject(JsonObject weather, const WeatherSettings &pending,
                        const WeatherSettings &saved, const WeatherSnapshot &snapshot) {
  weather["provider"] = kWeatherProvider;
  weather["enabled"] = pending.enabled != 0;
  weather["autoLocation"] = pending.autoLocation != 0;
  weather["unitsF"] = pending.unitsF != 0;
  weather["savedEnabled"] = saved.enabled != 0;
  weather["savedAutoLocation"] = saved.autoLocation != 0;
  weather["savedUnitsF"] = saved.unitsF != 0;
  weather["defaultEnabled"] = gWeatherDefaults.enabled != 0;
  weather["defaultAutoLocation"] = gWeatherDefaults.autoLocation != 0;
  weather["defaultUnitsF"] = gWeatherDefaults.unitsF != 0;
  weather["hasLocation"] = weatherHasLocation(pending);
  writeWeatherCoord(weather, "latitude", pending.latitudeE6);
  writeWeatherCoord(weather, "longitude", pending.longitudeE6);
  writeWeatherCoord(weather, "savedLatitude", saved.latitudeE6);
  writeWeatherCoord(weather, "savedLongitude", saved.longitudeE6);
  writeWeatherCoord(weather, "defaultLatitude", gWeatherDefaults.latitudeE6);
  writeWeatherCoord(weather, "defaultLongitude", gWeatherDefaults.longitudeE6);
  weather["valid"] = snapshot.valid;
  weather["weatherCode"] = snapshot.weatherCode;
  weather["precipitationProbability"] = snapshot.precipitationProbability;
  weather["temperatureTenths"] = snapshot.temperatureTenths;
  weather["apparentTemperatureTenths"] = snapshot.apparentTemperatureTenths;
  weather["highTemperatureTenths"] = snapshot.highTemperatureTenths;
  weather["lowTemperatureTenths"] = snapshot.lowTemperatureTenths;
  weather["windSpeedTenths"] = snapshot.windSpeedTenths;
  weather["updatedEpoch"] = (unsigned long)snapshot.updatedEpoch;
  writeWeatherCity(weather, "city", snapshot.city);
  writeWeatherStatus(weather, "status", snapshot.status);
}

// Build the full settings schema. "live" = gPending (the desired copy, mutex-guarded) —
// matches the old sendSettingsJson and avoids reading core-1-owned gLive from core 0.
// Caller must NOT hold gSettingsMux.
void buildSchemaDoc(JsonDocument &doc) {
  LifeSettings pend;
  ClockSettings clockPend;
  ClockSettings clockSaved;
  WeatherSettings weatherPend;
  WeatherSettings weatherSaved;
  WeatherSnapshot weatherSnapshot;
  char detected[kTimezoneNameBytes];
  char status[sizeof(gClockStatus)];
  char posix[sizeof(gClockPosix)];
  taskENTER_CRITICAL(&gSettingsMux);
  pend = gPending;
  clockPend = gClockPending;
  clockSaved = gClockSaved;
  weatherPend = gWeatherPending;
  weatherSaved = gWeatherSaved;
  weatherSnapshot = gWeatherSnapshot;
  copyString(detected, sizeof(detected), gDetectedTimezone);
  copyString(status, sizeof(status), gClockStatus);
  copyString(posix, sizeof(posix), gClockPosix);
  taskEXIT_CRITICAL(&gSettingsMux);

  doc["type"] = "schema";
  JsonObject geo = doc["geometry"].to<JsonObject>();
  geo["width"] = panelWidth;
  geo["height"] = panelHeight;
  geo["bitDepth"] = gGeoBitDepth;
  geo["tile"] = gGeoTile;

  JsonArray arr = doc["fields"].to<JsonArray>();
  for (size_t i = 0; i < kLifeFieldCount; i++) {
    JsonObject f = arr.add<JsonObject>();
    f["key"] = kLifeFieldMeta[i].key;
    f["label"] = kLifeFieldMeta[i].label;
    f["group"] = kLifeFieldMeta[i].group;
    f["min"] = kLifeFieldMeta[i].min;
    f["max"] = kLifeFieldMeta[i].max;
    f["step"] = kLifeFieldMeta[i].step;
    f["live"] = getLifeSettingByIndex(pend, i);
    f["saved"] = getLifeSettingByIndex(gSaved, i);
    f["default"] = getLifeSettingByIndex(gDefaults, i);
    f["desc"] = kLifeFieldMeta[i].desc;
  }

  char localTime[32];
  bool synced = clockReadLocal(localTime, sizeof(localTime));
  JsonObject clock = doc["clock"].to<JsonObject>();
  clock["timezone"] = clockPend.timezone;
  clock["autoTimezone"] = clockPend.autoTimezone != 0;
  clock["savedTimezone"] = clockSaved.timezone;
  clock["savedAutoTimezone"] = clockSaved.autoTimezone != 0;
  clock["defaultTimezone"] = gClockDefaults.timezone;
  clock["defaultAutoTimezone"] = gClockDefaults.autoTimezone != 0;
  clock["detectedTimezone"] = detected;
  clock["status"] = synced ? "synced" : status;
  clock["posix"] = posix;
  clock["synced"] = synced;
  if (synced) clock["localTime"] = localTime;

  JsonObject weather = doc["weather"].to<JsonObject>();
  writeWeatherObject(weather, weatherPend, weatherSaved, weatherSnapshot);
}

// Stats are read cross-core (core 1 writes, this runs on core 0) WITHOUT a lock. This is a
// deliberate benign data race: liveCells/generation are intentionally non-volatile (read
// per-cell in the render hot loop), aligned loads are atomic on Xtensa, and a torn/stale
// number in a cosmetic readout is acceptable. Do not "fix" this with volatile.
void buildStatsDoc(JsonDocument &doc) {
  doc["type"] = "stats";
  doc["paused"] = (bool)gPaused;   // authoritative sim state so the client's Pause/Resume toggle can't drift
  doc["renderFps"] = (unsigned)gStatRenderFps;
  doc["lifeUps"] = (unsigned)gStatLifeUps;
  doc["live"] = (unsigned)liveCells;
  doc["generation"] = (unsigned long)generation;
  doc["uptimeMs"] = (unsigned long)millis();
  doc["frameBytes"] = (unsigned long)gLastFrameBytes;
  doc["rawFrameBytes"] = (unsigned long)gRawFrameBytes;
  doc["frameEncoding"] = gLastFrameWasRle ? "rle" : "raw";

  ClockSettings clockPend;
  WeatherSettings weatherPend;
  WeatherSettings weatherSaved;
  WeatherSnapshot weatherSnapshot;
  char status[sizeof(gClockStatus)];
  taskENTER_CRITICAL(&gSettingsMux);
  clockPend = gClockPending;
  weatherPend = gWeatherPending;
  weatherSaved = gWeatherSaved;
  weatherSnapshot = gWeatherSnapshot;
  copyString(status, sizeof(status), gClockStatus);
  taskEXIT_CRITICAL(&gSettingsMux);

  char localTime[32];
  bool synced = clockReadLocal(localTime, sizeof(localTime));
  JsonObject clock = doc["clock"].to<JsonObject>();
  clock["timezone"] = clockPend.timezone;
  clock["autoTimezone"] = clockPend.autoTimezone != 0;
  clock["synced"] = synced;
  clock["status"] = synced ? "synced" : status;
  if (synced) clock["localTime"] = localTime;

  JsonObject weather = doc["weather"].to<JsonObject>();
  writeWeatherObject(weather, weatherPend, weatherSaved, weatherSnapshot);
}

void publishPending(const LifeSettings &s) {
  taskENTER_CRITICAL(&gSettingsMux);
  gPending = s;
  gPendingDirty = true;
  taskEXIT_CRITICAL(&gSettingsMux);
}

// Broadcast the full schema to all clients (after a baseline change: save/revert/reset).
void broadcastSchema() {
  JsonDocument doc;
  buildSchemaDoc(doc);
  String out;
  serializeJson(doc, out);
  gWs.textAll(out);
}

void writeClockMessage(JsonDocument &doc, const ClockSettings &s, const char *source) {
  doc["type"] = "clock";
  doc["timezone"] = s.timezone;
  doc["autoTimezone"] = s.autoTimezone != 0;
  if (source) doc["source"] = source;
}

void broadcastClock(const ClockSettings &s, const char *source) {
  JsonDocument doc;
  writeClockMessage(doc, s, source);
  String out;
  serializeJson(doc, out);
  gWs.textAll(out);
}

void writeWeatherMessage(JsonDocument &doc, const WeatherSettings &s, const char *source) {
  WeatherSnapshot snapshot;
  taskENTER_CRITICAL(&gSettingsMux);
  snapshot = gWeatherSnapshot;
  taskEXIT_CRITICAL(&gSettingsMux);

  doc["type"] = "weather";
  doc["enabled"] = s.enabled != 0;
  doc["autoLocation"] = s.autoLocation != 0;
  doc["unitsF"] = s.unitsF != 0;
  doc["hasLocation"] = weatherHasLocation(s);
  writeWeatherCoord(doc.as<JsonObject>(), "latitude", s.latitudeE6);
  writeWeatherCoord(doc.as<JsonObject>(), "longitude", s.longitudeE6);
  doc["valid"] = snapshot.valid;
  doc["weatherCode"] = snapshot.weatherCode;
  doc["precipitationProbability"] = snapshot.precipitationProbability;
  doc["temperatureTenths"] = snapshot.temperatureTenths;
  doc["apparentTemperatureTenths"] = snapshot.apparentTemperatureTenths;
  doc["highTemperatureTenths"] = snapshot.highTemperatureTenths;
  doc["lowTemperatureTenths"] = snapshot.lowTemperatureTenths;
  doc["windSpeedTenths"] = snapshot.windSpeedTenths;
  doc["updatedEpoch"] = (unsigned long)snapshot.updatedEpoch;
  writeWeatherCity(doc.as<JsonObject>(), "city", snapshot.city);
  writeWeatherStatus(doc.as<JsonObject>(), "status", snapshot.status);
  if (source) doc["source"] = source;
}

void broadcastWeather(const WeatherSettings &s, const char *source) {
  JsonDocument doc;
  writeWeatherMessage(doc, s, source);
  String out;
  serializeJson(doc, out);
  gWs.textAll(out);
}

enum LocationDetectFlags : uintptr_t {
  kDetectSave = 1,
  kDetectClock = 2,
  kDetectWeather = 4,
};

void locationDetectTask(void *param) {
  uintptr_t flags = (uintptr_t)param;
  bool saveDetected = (flags & kDetectSave) != 0;
  bool updateClock = (flags & kDetectClock) != 0;
  bool updateWeather = (flags & kDetectWeather) != 0;
  GeoIP geoip;
  location_t loc = geoip.getGeoFromWiFi(false);
  bool schemaDirty = saveDetected;

  if (updateClock) {
    ClockSettings work;
    ClockSettings saved;
    char detected[kTimezoneNameBytes];
    bool ok = loc.status && normalizeClockTimezone(loc.timezone, detected, sizeof(detected));

    if (ok) {
      taskENTER_CRITICAL(&gSettingsMux);
      copyString(gDetectedTimezone, sizeof(gDetectedTimezone), detected);
      work = gClockPending;
      saved = gClockSaved;
      copyString(work.timezone, sizeof(work.timezone), detected);
      work.autoTimezone = 1;
      gClockPending = work;
      gClockPendingDirty = true;
      taskEXIT_CRITICAL(&gSettingsMux);

      Serial.printf("[clock] timezone detected: %s\n", detected);
      if (saveDetected) {
        if (strcmp(saved.timezone, work.timezone) != 0 || saved.autoTimezone != work.autoTimezone) {
          clockSaveToNvs(work);
        }
      } else {
        broadcastClock(work, "detect");
      }
    } else {
      Serial.println("[clock] timezone detection failed");
      setClockStatus("timezone detect failed");
      schemaDirty = true;
    }
  }

  if (updateWeather) {
    int32_t latE6;
    int32_t lonE6;
    bool ok = loc.status && normalizeWeatherCoord(loc.latitude, loc.longitude, latE6, lonE6);
    if (ok) {
      WeatherSettings work;
      WeatherSettings saved;
      const char *place = loc.city[0] ? loc.city : (loc.region[0] ? loc.region : loc.country);
      taskENTER_CRITICAL(&gSettingsMux);
      work = gWeatherPending;
      saved = gWeatherSaved;
      work.latitudeE6 = latE6;
      work.longitudeE6 = lonE6;
      work.autoLocation = 1;
      gWeatherPending = work;
      gWeatherPendingDirty = true;
      copyString(gWeatherSnapshot.city, sizeof(gWeatherSnapshot.city), place);
      copyString(gWeatherSnapshot.status, sizeof(gWeatherSnapshot.status), "weather location detected");
      taskEXIT_CRITICAL(&gSettingsMux);

      char latText[18];
      char lonText[18];
      formatWeatherCoord(latE6, latText, sizeof(latText));
      formatWeatherCoord(lonE6, lonText, sizeof(lonText));
      Serial.printf("[weather] location detected: %s,%s %s\n", latText, lonText, place);
      if (saveDetected) {
        if (saved.enabled != work.enabled || saved.autoLocation != work.autoLocation ||
            saved.unitsF != work.unitsF || saved.latitudeE6 != work.latitudeE6 ||
            saved.longitudeE6 != work.longitudeE6) {
          weatherSaveToNvs(work);
        }
      } else {
        broadcastWeather(work, "detect");
      }
    } else {
      Serial.println("[weather] location detection failed");
      setWeatherStatus("weather location failed");
      schemaDirty = true;
    }
  }

  if (schemaDirty) {
    broadcastSchema();
  }

  gLocationDetectTask = nullptr;
  vTaskDelete(nullptr);
}

void locationRequestDetect(bool saveDetected, bool updateClock, bool updateWeather) {
  if (gLocationDetectTask || (!updateClock && !updateWeather)) return;
  uintptr_t flags = (saveDetected ? kDetectSave : 0) |
                    (updateClock ? kDetectClock : 0) |
                    (updateWeather ? kDetectWeather : 0);
  BaseType_t created = xTaskCreatePinnedToCore(locationDetectTask, "locdetect", 8192,
                                               (void *)flags, 1, &gLocationDetectTask, 0);
  if (created != pdPASS) {
    gLocationDetectTask = nullptr;
    if (updateClock) setClockStatus("timezone detect unavailable");
    if (updateWeather) setWeatherStatus("weather detect unavailable");
  }
}

void weatherRequestDetect(bool saveDetected) {
  locationRequestDetect(saveDetected, false, true);
}

void scheduleWeatherRefresh(uint32_t delayMs) {
  uint32_t dueAt = millis() + delayMs;
  taskENTER_CRITICAL(&gSettingsMux);
  if (!gReqWeatherRefresh || delayMs == 0 || (int32_t)(dueAt - gWeatherRefreshDueAt) < 0) {
    gWeatherRefreshDueAt = dueAt;
  }
  gReqWeatherRefresh = true;
  taskEXIT_CRITICAL(&gSettingsMux);
}

void weatherFetchTask(void *) {
  WeatherSettings settings;
  taskENTER_CRITICAL(&gSettingsMux);
  settings = gWeatherLive;
  taskEXIT_CRITICAL(&gSettingsMux);
  weatherSyncSnapshotSettings(settings);

  if (!settings.enabled) {
    gWeatherFetchTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  if (!weatherHasLocation(settings)) {
    gWeatherFetchTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setWeatherInvalidStatus("weather wifi unavailable");
    scheduleWeatherRefresh(10000);
    gWeatherFetchTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  char lat[18];
  char lon[18];
  formatWeatherCoord(settings.latitudeE6, lat, sizeof(lat));
  formatWeatherCoord(settings.longitudeE6, lon, sizeof(lon));
  char url[384];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
           "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
           "&hourly=precipitation_probability"
           "&daily=temperature_2m_max,temperature_2m_min"
           "&temperature_unit=%s&wind_speed_unit=%s&timezone=auto&forecast_days=1&forecast_hours=1",
           lat, lon, settings.unitsF ? "fahrenheit" : "celsius",
           settings.unitsF ? "mph" : "kmh");

  setWeatherStatus("weather fetching");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kWeatherHttpTimeoutMs);
  bool ok = false;
  int16_t tempTenths = 0;
  int16_t apparentTenths = 0;
  int16_t highTenths = 0;
  int16_t lowTenths = 0;
  uint16_t windSpeedTenths = 0;
  uint8_t precipitationProbability = 0;
  uint8_t weatherCode = 0;
  if (http.begin(client, url)) {
    int status = http.GET();
    if (status == HTTP_CODE_OK) {
      String body = http.getString();
      JsonDocument doc;
      if (!deserializeJson(doc, body)) {
        JsonObject current = doc["current"].as<JsonObject>();
        JsonObject hourly = doc["hourly"].as<JsonObject>();
        JsonObject daily = doc["daily"].as<JsonObject>();
        if (!current["temperature_2m"].isNull() && !current["apparent_temperature"].isNull() &&
            !current["weather_code"].isNull() && !current["wind_speed_10m"].isNull() &&
            !hourly["precipitation_probability"][0].isNull() &&
            !daily["temperature_2m_max"][0].isNull() && !daily["temperature_2m_min"][0].isNull()) {
          float temp = current["temperature_2m"] | 0.0f;
          float apparent = current["apparent_temperature"] | temp;
          float wind = current["wind_speed_10m"] | 0.0f;
          float high = daily["temperature_2m_max"][0] | temp;
          float low = daily["temperature_2m_min"][0] | temp;
          long precip = hourly["precipitation_probability"][0] | 0L;
          long code = current["weather_code"] | 0L;
          tempTenths = weatherTenths(temp);
          apparentTenths = weatherTenths(apparent);
          highTenths = weatherTenths(high);
          lowTenths = weatherTenths(low);
          windSpeedTenths = weatherSpeedTenths(wind);
          precipitationProbability = weatherPercent(precip);
          if (code < 0) code = 0;
          if (code > 255) code = 255;
          weatherCode = static_cast<uint8_t>(code);
          ok = true;
        }
      }
    } else {
      Serial.printf("[weather] fetch failed: HTTP %d\n", status);
    }
    http.end();
  }

  if (ok) {
    time_t now = time(nullptr);
    taskENTER_CRITICAL(&gSettingsMux);
    gWeatherSnapshot.enabled = true;
    gWeatherSnapshot.valid = true;
    gWeatherSnapshot.unitsF = settings.unitsF != 0;
    gWeatherSnapshot.weatherCode = weatherCode;
    gWeatherSnapshot.precipitationProbability = precipitationProbability;
    gWeatherSnapshot.temperatureTenths = tempTenths;
    gWeatherSnapshot.apparentTemperatureTenths = apparentTenths;
    gWeatherSnapshot.highTemperatureTenths = highTenths;
    gWeatherSnapshot.lowTemperatureTenths = lowTenths;
    gWeatherSnapshot.windSpeedTenths = windSpeedTenths;
    gWeatherSnapshot.updatedEpoch = now >= 1609459200 ? static_cast<uint32_t>(now) : 0;
    gWeatherLastFetchAt = millis();
    gWeatherLastLatE6 = settings.latitudeE6;
    gWeatherLastLonE6 = settings.longitudeE6;
    copyString(gWeatherSnapshot.status, sizeof(gWeatherSnapshot.status), "weather ready");
    taskEXIT_CRITICAL(&gSettingsMux);
    Serial.printf("[weather] %ld.%ld %c feels %ld.%ld rain %u%% wind %u.%u code %u\n",
                  (long)(tempTenths / 10), labs(tempTenths % 10), settings.unitsF ? 'F' : 'c',
                  (long)(apparentTenths / 10), labs(apparentTenths % 10), precipitationProbability,
                  windSpeedTenths / 10, windSpeedTenths % 10, weatherCode);
  } else {
    setWeatherInvalidStatus("weather fetch failed");
  }

  broadcastSchema();
  gWeatherFetchTask = nullptr;
  vTaskDelete(nullptr);
}

// === Inbound message dispatcher — parse one JSON text frame and route it. ===
// Runs in the AsyncTCP task (core 0). Returns true if understood. Reference implementation
// below; this is the learning-mode contribution point (see plan note).
bool dispatchWsMessage(AsyncWebSocketClient *client, const uint8_t *data, size_t len) {
  JsonDocument doc;
  // cast: ArduinoJson's sized overload takes const char*, not uint8_t*.
  if (deserializeJson(doc, reinterpret_cast<const char *>(data), len)) return false;  // parse error → ignore
  const char *type = doc["type"] | "";

  if (strcmp(type, "set") == 0) {
    const char *key = doc["key"] | "";
    long value = doc["value"] | 0L;
    LifeSettings work;
    taskENTER_CRITICAL(&gSettingsMux);
    work = gPending;
    taskEXIT_CRITICAL(&gSettingsMux);
    if (!applyLifeSettingField(work, key, value)) return false;   // unknown key
    taskENTER_CRITICAL(&gSettingsMux);
    gPending = work;
    gPendingDirty = true;
    taskEXIT_CRITICAL(&gSettingsMux);

    // Echo the clamped value to OTHER clients for multi-tab sync.
    long applied = value;
    getLifeSettingByKey(work, key, &applied);
    JsonDocument echo;
    echo["type"] = "set";
    echo["key"] = key;
    echo["value"] = applied;
    echo["from"] = client->id();
    String out;
    serializeJson(echo, out);
    gWs.textAll(out);
    return true;
  }

  if (strcmp(type, "clock") == 0) {
    ClockSettings work;
    taskENTER_CRITICAL(&gSettingsMux);
    work = gClockPending;
    taskEXIT_CRITICAL(&gSettingsMux);

    if (!doc["timezone"].isNull()) {
      char normalized[kTimezoneNameBytes];
      const char *timezone = doc["timezone"] | "";
      if (!normalizeClockTimezone(timezone, normalized, sizeof(normalized))) return false;
      copyString(work.timezone, sizeof(work.timezone), normalized);
    }
    if (!doc["autoTimezone"].isNull()) {
      work.autoTimezone = (doc["autoTimezone"] | false) ? 1 : 0;
    }

    publishClockPending(work);
    JsonDocument echo;
    writeClockMessage(echo, work, "client");
    echo["from"] = client->id();
    String out;
    serializeJson(echo, out);
    gWs.textAll(out);
    return true;
  }

  if (strcmp(type, "weather") == 0) {
    WeatherSettings work;
    taskENTER_CRITICAL(&gSettingsMux);
    work = gWeatherPending;
    taskEXIT_CRITICAL(&gSettingsMux);

    if (!doc["enabled"].isNull()) {
      work.enabled = (doc["enabled"] | false) ? 1 : 0;
    }
    if (!doc["autoLocation"].isNull()) {
      work.autoLocation = (doc["autoLocation"] | false) ? 1 : 0;
    }
    if (!doc["unitsF"].isNull()) {
      work.unitsF = (doc["unitsF"] | false) ? 1 : 0;
    }
    if (!doc["latitude"].isNull() || !doc["longitude"].isNull()) {
      const char *latText = doc["latitude"] | "";
      const char *lonText = doc["longitude"] | "";
      if (!*latText && !*lonText) {
        work.latitudeE6 = kWeatherCoordUnset;
        work.longitudeE6 = kWeatherCoordUnset;
      } else {
        float lat;
        float lon;
        int32_t latE6;
        int32_t lonE6;
        if (!parseFloatText(latText, lat) || !parseFloatText(lonText, lon) ||
            !normalizeWeatherCoord(lat, lon, latE6, lonE6)) return false;
        work.latitudeE6 = latE6;
        work.longitudeE6 = lonE6;
      }
    }

    publishWeatherPending(work);
    JsonDocument echo;
    writeWeatherMessage(echo, work, "client");
    echo["from"] = client->id();
    String out;
    serializeJson(echo, out);
    gWs.textAll(out);
    return true;
  }

  if (strcmp(type, "action") == 0) {
    const char *action = doc["action"] | "";
    if (strcmp(action, "reseed") == 0) { gReqReseed = true; return true; }
    if (strcmp(action, "stop") == 0)   { gReqPause = 1;     return true; }
    if (strcmp(action, "resume") == 0) { gReqPause = -1;    return true; }
    if (strcmp(action, "clear") == 0)  { gReqClear = true;  return true; }
    if (strcmp(action, "forget") == 0) { gReqForget = true; return true; }
    if (strcmp(action, "clockMinute") == 0) { gReqClockAnimation = kClockAnimationRequestMinute; return true; }
    if (strcmp(action, "clockHour") == 0)   { gReqClockAnimation = kClockAnimationRequestHour; return true; }
    if (strcmp(action, "detectTimezone") == 0) { locationRequestDetect(/*saveDetected=*/false, true, false); return true; }
    if (strcmp(action, "detectWeather") == 0) { weatherRequestDetect(/*saveDetected=*/false); return true; }
    if (strcmp(action, "refreshWeather") == 0) { weatherRequestRefresh(); return true; }
    if (strcmp(action, "save") == 0) {
      LifeSettings cur;
      ClockSettings clock;
      WeatherSettings weather;
      taskENTER_CRITICAL(&gSettingsMux);
      cur = gPending;
      clock = gClockPending;
      weather = gWeatherPending;
      taskEXIT_CRITICAL(&gSettingsMux);
      settingsSaveToNvs(cur);
      clockSaveToNvs(clock);
      weatherSaveToNvs(weather);
      broadcastSchema();
      return true;
    }
    if (strcmp(action, "revert") == 0) {
      ClockSettings clockSaved;
      WeatherSettings weatherSaved;
      taskENTER_CRITICAL(&gSettingsMux);
      clockSaved = gClockSaved;
      weatherSaved = gWeatherSaved;
      taskEXIT_CRITICAL(&gSettingsMux);
      publishPending(gSaved);
      publishClockPending(clockSaved);
      publishWeatherPending(weatherSaved);
      broadcastSchema();
      return true;
    }
    if (strcmp(action, "reset") == 0)  { publishPending(gDefaults); publishClockPending(gClockDefaults); publishWeatherPending(gWeatherDefaults); broadcastSchema(); return true; }
  }
  return false;
}

// Validate one complete binary draw frame and stage its bitmask for core 1. Runs in the
// AsyncTCP task (core 0). Returns true if the frame was understood (even if dropped due to
// backpressure). Never touches the cell arrays — that is webPortalTick()'s job on core 1.
bool dispatchDrawFrame(const uint8_t *data, size_t len) {
  if (len < kFrameHeaderBytes) return false;
  if (data[0] != kDrawFrameMagic || data[1] != kDrawFrameVersion) return false;
  uint16_t w = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
  uint16_t h = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
  if (w != panelWidth || h != panelHeight) return false;   // geometry must match exactly
  size_t maskBytes = ((size_t)w * h + 7) / 8;
  if (maskBytes > kDrawMaskBytes) return false;
  if (len != kFrameHeaderBytes + maskBytes) return false;

  // SPSC hand-off: fill the buffer only while core 1 has drained the previous draw, then
  // publish via the barrier. The critical sections guard only the flag.
  bool pending;
  taskENTER_CRITICAL(&gDrawMux); pending = gReqDraw; taskEXIT_CRITICAL(&gDrawMux);
  if (pending) return true;                       // core 1 hasn't drained the last draw — drop this one
  memcpy(gDrawBitmask, data + kFrameHeaderBytes, maskBytes);   // fill buffer BEFORE publishing
  taskENTER_CRITICAL(&gDrawMux); gReqDraw = true; taskEXIT_CRITICAL(&gDrawMux);   // publish (barrier)
  return true;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  (void)server;
  switch (type) {
    case WS_EVT_CONNECT: {
      JsonDocument doc;
      buildSchemaDoc(doc);
      doc["clientId"] = client->id();   // tell the client its own id so it can filter its echoes
      String out;
      serializeJson(doc, out);
      client->text(out);
      break;
    }
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      // Complete single frames only. Text → JSON control (≤512 B cap); binary → drawn cells
      // (≤max-panel cap). Reject oversized/fragmented frames before parsing.
      if (info->final && info->index == 0 && info->len == len) {
        if (info->opcode == WS_TEXT && len <= kMaxWsFrameBytes) {
          dispatchWsMessage(client, data, len);
        } else if (info->opcode == WS_BINARY && len <= kMaxWsDrawBytes) {
          dispatchDrawFrame(data, len);
        }
      }
      break;
    }
    default:
      break;
  }
}

// Broadcast one binary board frame (header + RGB565 pixels) to all clients via a single
// shared, ref-counted buffer. Caller has already checked count()>0 and availableForWriteAll().
// Frames are ephemeral: if the heap can't hand us a contiguous block we just skip this one.
void pushBoardFrame() {
  const size_t cells = (size_t)panelWidth * panelHeight;
  const size_t rawPayload = cells * 2;
  const size_t rawTotal = kFrameHeaderBytes + rawPayload;
  gRawFrameBytes = rawTotal;

  size_t rlePayload = measureDrawnFrameRlePayload();
  if (rlePayload > 0 && kFrameHeaderBytes + rlePayload < rawTotal) {
    const size_t rleTotal = kFrameHeaderBytes + rlePayload;
    if (ESP.getMaxAllocHeap() >= rleTotal + 512) {
      AsyncWebSocketMessageBuffer *buf = gWs.makeBuffer(rleTotal);
      if (buf && buf->length() == rleTotal) {
        uint8_t *p = buf->get();
        p[0] = kRleFrameMagic;
        p[1] = kFrameVersion;
        p[2] = (uint8_t)(panelWidth & 0xFF);
        p[3] = (uint8_t)(panelWidth >> 8);
        p[4] = (uint8_t)(panelHeight & 0xFF);
        p[5] = (uint8_t)(panelHeight >> 8);
        size_t actualPayload = copyDrawnFrameRle(p + kFrameHeaderBytes, rlePayload);
        if (actualPayload == rlePayload) {
          gLastFrameBytes = rleTotal;
          gLastFrameWasRle = true;
          gWs.binaryAll(buf);
          return;
        }
      }
      if (buf) delete buf;   // drawnColor changed between measure/encode; fall back to raw
    }
  }

  // Avoid the OOM-abort path: only allocate if a big-enough contiguous block exists.
  if (ESP.getMaxAllocHeap() < rawTotal + 512) return;
  AsyncWebSocketMessageBuffer *buf = gWs.makeBuffer(rawTotal);
  if (!buf) return;
  if (buf->length() != rawTotal) { delete buf; return; }

  uint8_t *p = buf->get();
  p[0] = kFrameMagic;
  p[1] = kFrameVersion;
  p[2] = (uint8_t)(panelWidth & 0xFF);
  p[3] = (uint8_t)(panelWidth >> 8);
  p[4] = (uint8_t)(panelHeight & 0xFF);
  p[5] = (uint8_t)(panelHeight >> 8);
  // p+6 is 2-byte aligned (vector data is over-aligned, header is even), so the uint16
  // writes inside copyDrawnFrame are well-aligned for Xtensa. Memory is little-endian, so
  // each RGB565 lands low-byte-first on the wire — matching the browser's Uint16Array read.
  copyDrawnFrame(reinterpret_cast<uint16_t *>(p + kFrameHeaderBytes));
  gLastFrameBytes = rawTotal;
  gLastFrameWasRle = false;
  gWs.binaryAll(buf);   // moves the shared buffer into each client's queue, then deletes buf
}

// Core-0 task: stream the board (~kFramePushMs) and ephemeral stats (~kStatsPushMs), then
// reap dead clients. Backpressure: gate the whole tick on availableForWriteAll() and skip
// it if any client is backed up — both payloads are ephemeral, the next tick supersedes
// them. We use all-or-nothing availableForWriteAll() rather than per-client canSend(),
// because per-client gating needs gWs.getClients() iteration, which would race cleanupClients().
void wsPushTask(void *) {
  uint32_t lastStatsAt = 0;
  for (;;) {
    if (gWs.count() > 0 && gWs.availableForWriteAll()) {
      pushBoardFrame();
      uint32_t nowMs = millis();
      if (nowMs - lastStatsAt >= kStatsPushMs) {
        lastStatsAt = nowMs;
        JsonDocument doc;
        buildStatsDoc(doc);
        String out;
        serializeJson(doc, out);
        gWs.textAll(out);
      }
    }
    gWs.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(kFramePushMs));
  }
}
}  // namespace

void startWeatherRefresh();

void webServerStart() {
  if (gPushTask) {
    return;  // already started; idempotent across WiFi reconnects (preserves gPending)
  }
  taskENTER_CRITICAL(&gSettingsMux);
  gPending = gLive;        // first connect only: desired == current
  gPendingDirty = false;
  gClockPending = gClockLive;
  gClockPendingDirty = false;
  gWeatherPending = gWeatherLive;
  gWeatherPendingDirty = false;
  taskEXIT_CRITICAL(&gSettingsMux);

  gServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", kIndexHtml);
  });
  gWs.onEvent(onWsEvent);
  gServer.addHandler(&gWs);
  gServer.begin();

  xTaskCreatePinnedToCore(wsPushTask, "wspush", 4096, nullptr, 1, &gPushTask, 0);  // core 0
}

void webPortalTick() {
  if (gPendingDirty) {
    LifeSettings next;
    taskENTER_CRITICAL(&gSettingsMux);
    next = gPending;
    gPendingDirty = false;
    taskEXIT_CRITICAL(&gSettingsMux);
    gLive = next;          // the ONLY writer of gLive (core 1)
  }

  if (gClockPendingDirty) {
    ClockSettings next;
    taskENTER_CRITICAL(&gSettingsMux);
    next = gClockPending;
    gClockPendingDirty = false;
    taskEXIT_CRITICAL(&gSettingsMux);
    gClockLive = next;
    clockApplyTimezone(gClockLive.timezone);
  }

  if (gWeatherPendingDirty) {
    WeatherSettings next;
    taskENTER_CRITICAL(&gSettingsMux);
    next = gWeatherPending;
    gWeatherPendingDirty = false;
    taskEXIT_CRITICAL(&gSettingsMux);
    gWeatherLive = next;
    weatherSyncSnapshotSettings(gWeatherLive);
    weatherRequestRefresh();
  }

  bool doWeatherRefresh = false;
  uint32_t nowMs = millis();
  taskENTER_CRITICAL(&gSettingsMux);
  if (gReqWeatherRefresh && (int32_t)(nowMs - gWeatherRefreshDueAt) >= 0) {
    gReqWeatherRefresh = false;
    doWeatherRefresh = true;
  }
  taskEXIT_CRITICAL(&gSettingsMux);
  if (doWeatherRefresh) {
    startWeatherRefresh();
  }

  // Drain a staged browser-drawn frame: apply FIRST (read the buffer), then release it by
  // clearing the flag — so core 0 only refills gDrawBitmask once we are done reading.
  bool doDraw;
  taskENTER_CRITICAL(&gDrawMux); doDraw = gReqDraw; taskEXIT_CRITICAL(&gDrawMux);
  if (doDraw) {
    applyDrawnCells(gDrawBitmask, panelWidth, panelHeight);   // read buffer (core 1 owns cell arrays)
    taskENTER_CRITICAL(&gDrawMux); gReqDraw = false; taskEXIT_CRITICAL(&gDrawMux);   // release buffer
  }
}

bool weatherCopySnapshot(WeatherSnapshot &out) {
  taskENTER_CRITICAL(&gSettingsMux);
  out = gWeatherSnapshot;
  taskEXIT_CRITICAL(&gSettingsMux);
  return out.enabled && out.valid;
}

void startWeatherRefresh() {
  WeatherSettings settings;
  WeatherSnapshot snapshot;
  uint32_t lastFetchAt;
  int32_t lastLatE6;
  int32_t lastLonE6;
  taskENTER_CRITICAL(&gSettingsMux);
  settings = gWeatherLive;
  snapshot = gWeatherSnapshot;
  lastFetchAt = gWeatherLastFetchAt;
  lastLatE6 = gWeatherLastLatE6;
  lastLonE6 = gWeatherLastLonE6;
  taskEXIT_CRITICAL(&gSettingsMux);

  weatherSyncSnapshotSettings(settings);
  if (!settings.enabled || !weatherHasLocation(settings)) return;
  uint32_t now = millis();
  if (snapshot.valid && lastFetchAt != 0 && now - lastFetchAt < kWeatherCacheMs &&
      snapshot.unitsF == (settings.unitsF != 0) && lastLatE6 == settings.latitudeE6 &&
      lastLonE6 == settings.longitudeE6) {
    return;
  }
  if (gWeatherFetchTask) return;
  BaseType_t created = xTaskCreatePinnedToCore(weatherFetchTask, "weather", 8192,
                                               nullptr, 1, &gWeatherFetchTask, 0);
  if (created != pdPASS) {
    gWeatherFetchTask = nullptr;
    setWeatherInvalidStatus("weather fetch unavailable");
  }
}

void weatherRequestRefresh() {
  scheduleWeatherRefresh(0);
}

#else  // not S3 / benchmark build: portal compiles to nothing.
// LifeSettings is available via web_portal.h (included above).
void webPortalBegin() {}
void settingsLoadFromNvs() {}
void settingsSaveToNvs(const LifeSettings &) {}
void settingsClearNvs() {}
void webPortalTick() {}
bool weatherCopySnapshot(WeatherSnapshot &out) { out = {}; return false; }
void weatherRequestRefresh() {}
#endif
