#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr int32_t kWeatherCoordUnset = 2000000000L;
constexpr size_t kWeatherCityBytes = 36;
constexpr size_t kWeatherStatusBytes = 48;

struct WeatherSettings {
  uint8_t enabled;
  uint8_t autoLocation;
  uint8_t unitsF;
  int32_t latitudeE6;
  int32_t longitudeE6;
};

struct WeatherSnapshot {
  bool enabled;
  bool valid;
  uint8_t unitsF;
  uint8_t weatherCode;
  uint8_t precipitationProbability;
  int16_t temperatureTenths;
  int16_t apparentTemperatureTenths;
  int16_t highTemperatureTenths;
  int16_t lowTemperatureTenths;
  uint16_t windSpeedTenths;
  uint32_t updatedEpoch;
  char city[kWeatherCityBytes];
  char status[kWeatherStatusBytes];
};

bool weatherCopySnapshot(WeatherSnapshot &out);
void weatherRequestRefresh();
