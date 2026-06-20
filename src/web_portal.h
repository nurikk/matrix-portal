#pragma once
#include "life_settings.h"
// ESP32-S3-only WiFi provisioning + web control panel. No-ops elsewhere.
void webPortalBegin();
void settingsLoadFromNvs();
void settingsSaveToNvs(const LifeSettings &s);
void settingsClearNvs();
void webPortalTick();
