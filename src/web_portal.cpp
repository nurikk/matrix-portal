#include "web_portal.h"

#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3) && !defined(MATRIX_BENCHMARK)

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <ESPmDNS.h>
#include "life_settings.h"

extern LifeSettings gLive;
extern LifeSettings gSaved;

void webServerStart();  // forward declaration; real impl in Task 5 (temp stub at file end until then)

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
      webServerStart();           // defined in Task 5; forward-declared above
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

void webPortalTick() {}

void webServerStart() {}  // TEMP — replaced in Task 5

#else  // not S3 / benchmark build: portal compiles to nothing.
// LifeSettings is available via web_portal.h (included above).
void webPortalBegin() {}
void settingsLoadFromNvs() {}
void settingsSaveToNvs(const LifeSettings &) {}
void settingsClearNvs() {}
void webPortalTick() {}
#endif
