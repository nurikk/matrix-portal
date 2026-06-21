#include "web_portal.h"

#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3) && !defined(MATRIX_BENCHMARK)

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>   // brings in AsyncWebSocket
#include <ArduinoJson.h>
#include "life_settings.h"

extern LifeSettings gLive;
extern LifeSettings gSaved;
extern LifeSettings gDefaults;
extern volatile bool gReqReseed;
extern volatile bool gReqBurn;
extern volatile bool gReqForget;
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

#include "web_ui.h"

void webServerStart();  // defined below; called from onWiFiEvent

// --- connectivity state (read by main.cpp for the one-time IP scroll) ---
volatile bool gShowIpScroll = false;
char gIpText[32] = {0};

namespace {
constexpr uint16_t kSettingsVersion = 1;
constexpr const char *kNvsNamespace = "matrixlife";
constexpr const char *kKeyVersion = "ver";
constexpr const char *kKeyBlob = "settings";
constexpr const char *kProvServiceName = "PROV_MatrixLife";
constexpr const char *kProvPop = "matrixlife";   // proof-of-possession entered in the app
constexpr const char *kMdnsHost = "matrixportal";
Preferences gPrefs;

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
  if (gPrefs.begin(kNvsNamespace, /*readOnly=*/true)) {
    LifeSettings tmp;
    uint16_t ver = gPrefs.getUShort(kKeyVersion, 0);
    size_t got = gPrefs.getBytesLength(kKeyBlob);
    if (ver == kSettingsVersion && got == sizeof(LifeSettings)) {
      gPrefs.getBytes(kKeyBlob, &tmp, sizeof(LifeSettings));
      clampSettings(tmp);
      gSaved = tmp;
    }
    gPrefs.end();
  }
  gLive = gSaved;
}

void settingsSaveToNvs(const LifeSettings &s) {
  if (gPrefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    gPrefs.putUShort(kKeyVersion, kSettingsVersion);
    gPrefs.putBytes(kKeyBlob, &s, sizeof(LifeSettings));
    gPrefs.end();
    gSaved = s;
  }
}

void settingsClearNvs() {
  if (gPrefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    gPrefs.clear();
    gPrefs.end();
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
portMUX_TYPE gSettingsMux = portMUX_INITIALIZER_UNLOCKED;
LifeSettings gPending;
volatile bool gPendingDirty = false;

AsyncWebServer gServer(80);
AsyncWebSocket gWs("/ws");
TaskHandle_t gPushTask = nullptr;

constexpr size_t kMaxWsFrameBytes = 256;   // control frames are tiny; reject larger pre-parse
constexpr uint32_t kStatsPushMs = 500;

// --- Live board mirror (binary frames) ---
// The board is streamed to browsers as binary WS frames (JSON stays on text frames, so
// the two never collide). Layout, all little-endian:
//   [0]    magic 0x4C ('L')   [1] version   [2..3] width   [4..5] height
//   [6..]  width*height RGB565 pixels, row-major (same bit layout as Adafruit GFX 565).
constexpr uint32_t kFramePushMs = 100;     // ~10 fps board mirror — smooth, LAN-cheap
constexpr uint8_t kFrameMagic = 0x4C;      // 'L'
constexpr uint8_t kFrameVersion = 1;
constexpr size_t kFrameHeaderBytes = 6;

// Build the full settings schema. "live" = gPending (the desired copy, mutex-guarded) —
// matches the old sendSettingsJson and avoids reading core-1-owned gLive from core 0.
// Caller must NOT hold gSettingsMux.
void buildSchemaDoc(JsonDocument &doc) {
  LifeSettings pend;
  taskENTER_CRITICAL(&gSettingsMux);
  pend = gPending;
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
}

// Stats are read cross-core (core 1 writes, this runs on core 0) WITHOUT a lock. This is a
// deliberate benign data race: liveCells/generation are intentionally non-volatile (read
// per-cell in the render hot loop), aligned loads are atomic on Xtensa, and a torn/stale
// number in a cosmetic readout is acceptable. Do not "fix" this with volatile.
void buildStatsDoc(JsonDocument &doc) {
  doc["type"] = "stats";
  doc["renderFps"] = (unsigned)gStatRenderFps;
  doc["lifeUps"] = (unsigned)gStatLifeUps;
  doc["live"] = (unsigned)liveCells;
  doc["generation"] = (unsigned long)generation;
  doc["uptimeMs"] = (unsigned long)millis();
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

  if (strcmp(type, "action") == 0) {
    const char *action = doc["action"] | "";
    if (strcmp(action, "reseed") == 0) { gReqReseed = true; return true; }
    if (strcmp(action, "burn") == 0)   { gReqBurn = true;   return true; }
    if (strcmp(action, "forget") == 0) { gReqForget = true; return true; }
    if (strcmp(action, "save") == 0) {
      LifeSettings cur;
      taskENTER_CRITICAL(&gSettingsMux);
      cur = gPending;
      taskEXIT_CRITICAL(&gSettingsMux);
      settingsSaveToNvs(cur);
      broadcastSchema();
      return true;
    }
    if (strcmp(action, "revert") == 0) { publishPending(gSaved);    broadcastSchema(); return true; }
    if (strcmp(action, "reset") == 0)  { publishPending(gDefaults); broadcastSchema(); return true; }
  }
  return false;
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
      // Complete single-frame text only; reject oversized frames before deserializing.
      if (info->final && info->index == 0 && info->len == len &&
          info->opcode == WS_TEXT && len <= kMaxWsFrameBytes) {
        dispatchWsMessage(client, data, len);
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
  const size_t total = kFrameHeaderBytes + cells * 2;
  // Avoid the OOM-abort path: only allocate if a big-enough contiguous block exists.
  if (ESP.getMaxAllocHeap() < total + 512) return;
  AsyncWebSocketMessageBuffer *buf = gWs.makeBuffer(total);
  if (!buf) return;
  if (buf->length() != total) { delete buf; return; }

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

void webServerStart() {
  if (gPushTask) {
    return;  // already started; idempotent across WiFi reconnects (preserves gPending)
  }
  taskENTER_CRITICAL(&gSettingsMux);
  gPending = gLive;        // first connect only: desired == current
  gPendingDirty = false;
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
}

#else  // not S3 / benchmark build: portal compiles to nothing.
// LifeSettings is available via web_portal.h (included above).
void webPortalBegin() {}
void settingsLoadFromNvs() {}
void settingsSaveToNvs(const LifeSettings &) {}
void settingsClearNvs() {}
void webPortalTick() {}
#endif
