#pragma once
#include <stdint.h>
#include "life_settings.h"

constexpr uint8_t kClockAnimationRequestMinute = 1;
constexpr uint8_t kClockAnimationRequestFiveMinute = 2;
constexpr uint8_t kClockAnimationRequestHour = 3;

// ESP32-S3-only WiFi provisioning + web control panel. No-ops elsewhere.
void webPortalBegin();
void settingsLoadFromNvs();
void settingsSaveToNvs(const LifeSettings &s);
void settingsClearNvs();
void webPortalTick();
