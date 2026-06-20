#include "web_portal.h"

#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3) && !defined(MATRIX_BENCHMARK)

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <ESPmDNS.h>
#include <WebServer.h>
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

// Temporary placeholder — Task 6 deletes this and adds #include "web_ui.h"
static const char kIndexHtml[] = "<!doctype html><title>MatrixLife</title><p>panel coming soon</p>";

void webServerStart();  // defined below; called from onWiFiEvent

// --- connectivity state (read by main.cpp for the one-time IP scroll) ---
volatile bool gWifiConnected = false;
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
      gWifiConnected = true;
      gShowIpScroll = true;       // main loop scrolls it once
      webServerStart();           // forward-declared above; defined below
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      gWifiConnected = false;
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
WebServer gServer(80);
TaskHandle_t gWebTask = nullptr;

void sendSettingsJson() {
  static char buf[8192];
  size_t n = (size_t)snprintf(buf, sizeof(buf),
      "{\"geometry\":{\"width\":%d,\"height\":%d,\"bitDepth\":%d,\"tile\":%d},\"settings\":",
      (int)panelWidth, (int)panelHeight, gGeoBitDepth, gGeoTile);
  LifeSettings liveCopy;
  taskENTER_CRITICAL(&gSettingsMux);
  liveCopy = gPending;
  taskEXIT_CRITICAL(&gSettingsMux);
  n += serializeSettingsJson(liveCopy, gSaved, gDefaults, buf + n, sizeof(buf) - n);
  if (n < sizeof(buf) - 1) {
    n += (size_t)snprintf(buf + n, sizeof(buf) - n, "}");
  }
  gServer.send(200, "application/json", buf);
}

void handleGetSettings() { sendSettingsJson(); }

void handlePostSettings() {
  LifeSettings work;
  taskENTER_CRITICAL(&gSettingsMux);
  work = gPending;
  taskEXIT_CRITICAL(&gSettingsMux);
  for (int i = 0; i < gServer.args(); i++) {
    applyLifeSettingField(work, gServer.argName(i).c_str(), gServer.arg(i).toInt());
  }
  taskENTER_CRITICAL(&gSettingsMux);
  gPending = work;
  gPendingDirty = true;
  taskEXIT_CRITICAL(&gSettingsMux);
  sendSettingsJson();
}

void publish(const LifeSettings &s) {
  taskENTER_CRITICAL(&gSettingsMux);
  gPending = s;
  gPendingDirty = true;
  taskEXIT_CRITICAL(&gSettingsMux);
}

void handleSave() {
  LifeSettings cur;
  taskENTER_CRITICAL(&gSettingsMux);
  cur = gPending;
  taskEXIT_CRITICAL(&gSettingsMux);
  settingsSaveToNvs(cur);
  sendSettingsJson();
}

void handleRevert() { publish(gSaved);    sendSettingsJson(); }
void handleReset()  { publish(gDefaults); sendSettingsJson(); }

void handleAction() {
  String a = gServer.arg("do");
  if (a == "reseed") gReqReseed = true;
  else if (a == "burn") gReqBurn = true;
  gServer.send(200, "application/json", "{\"ok\":true}");
}

void handleStats() {
  char buf[160];
  snprintf(buf, sizeof(buf),
      "{\"renderFps\":%u,\"lifeUps\":%u,\"live\":%u,\"generation\":%lu,\"uptimeMs\":%lu}",
      (unsigned)gStatRenderFps, (unsigned)gStatLifeUps, (unsigned)liveCells,
      (unsigned long)generation, (unsigned long)millis());
  gServer.send(200, "application/json", buf);
}

void handleForget() {
  gServer.send(200, "application/json", "{\"ok\":true,\"msg\":\"rebooting to provisioning\"}");
  gReqForget = true;
}

void handleRoot()     { gServer.send_P(200, "text/html", kIndexHtml); }
void handleNotFound() { gServer.send(404, "text/plain", "not found"); }

void webTaskFn(void *) {
  gServer.on("/", HTTP_GET, handleRoot);
  gServer.on("/api/settings", HTTP_GET, handleGetSettings);
  gServer.on("/api/settings", HTTP_POST, handlePostSettings);
  gServer.on("/api/save", HTTP_POST, handleSave);
  gServer.on("/api/revert", HTTP_POST, handleRevert);
  gServer.on("/api/reset", HTTP_POST, handleReset);
  gServer.on("/api/action", HTTP_POST, handleAction);
  gServer.on("/api/stats", HTTP_GET, handleStats);
  gServer.on("/api/forget-wifi", HTTP_POST, handleForget);
  gServer.onNotFound(handleNotFound);
  gServer.begin();
  for (;;) {
    gServer.handleClient();
    vTaskDelay(1);
  }
}
}  // namespace

void webServerStart() {
  taskENTER_CRITICAL(&gSettingsMux);
  gPending = gLive;
  gPendingDirty = false;
  taskEXIT_CRITICAL(&gSettingsMux);
  if (!gWebTask) {
    xTaskCreatePinnedToCore(webTaskFn, "web", 8192, nullptr, 1, &gWebTask, 0);
  }
}

void webPortalTick() {
  if (gPendingDirty) {
    LifeSettings next;
    taskENTER_CRITICAL(&gSettingsMux);
    next = gPending;
    gPendingDirty = false;
    taskEXIT_CRITICAL(&gSettingsMux);
    gLive = next;
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
