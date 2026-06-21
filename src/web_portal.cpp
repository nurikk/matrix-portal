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
extern volatile int8_t gReqPause;
extern volatile bool gReqClear;
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
constexpr uint16_t kSettingsVersion = 3;   // bumped: removed tilt/shake knobs (struct layout changed)
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
    if (strcmp(action, "stop") == 0)   { gReqPause = 1;     return true; }
    if (strcmp(action, "resume") == 0) { gReqPause = -1;    return true; }
    if (strcmp(action, "clear") == 0)  { gReqClear = true;  return true; }
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
      // Complete single frames only. Text → JSON control (≤256 B cap); binary → drawn cells
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

  // Drain a staged browser-drawn frame: apply FIRST (read the buffer), then release it by
  // clearing the flag — so core 0 only refills gDrawBitmask once we are done reading.
  bool doDraw;
  taskENTER_CRITICAL(&gDrawMux); doDraw = gReqDraw; taskEXIT_CRITICAL(&gDrawMux);
  if (doDraw) {
    applyDrawnCells(gDrawBitmask, panelWidth, panelHeight);   // read buffer (core 1 owns cell arrays)
    taskENTER_CRITICAL(&gDrawMux); gReqDraw = false; taskEXIT_CRITICAL(&gDrawMux);   // release buffer
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
