// Host tests for src/life_settings.h (pure, no Arduino).
//   clang++ -std=c++17 -O2 -Wall -Wextra tests/test_life_settings.cpp -o /tmp/test_settings && /tmp/test_settings
#include "../src/life_settings.h"

#include <cstdio>
#include <cstring>

static int g_failures = 0;
#define CHECK(cond, msg)                                                        \
  do { if (!(cond)) { std::printf("FAIL: %s\n", (msg)); g_failures++; } } while (0)

int main() {
  LifeSettings d = defaultLifeSettings();

  // Defaults match the firmware's historical constexpr values.
  CHECK(d.lifeStepMs == 100, "default lifeStepMs");
  CHECK(d.burnStepMs == 29, "default burnStepMs");
  CHECK(d.renderFrameMs == 33, "default renderFrameMs");
  CHECK(d.minLiveCells == 8, "default minLiveCells");
  CHECK(d.hueStep == 3, "default hueStep");
  CHECK(d.knockImpulseFullScale == 18000, "default knockImpulseFullScale");

  // Defaults are already in-range: clamping is a no-op.
  LifeSettings c = d;
  clampSettings(c);
  CHECK(std::memcmp(&c, &d, sizeof(LifeSettings)) == 0, "defaults survive clamp");

  // Two independent default structs must be byte-identical (padding must be deterministic).
  LifeSettings d2 = defaultLifeSettings();
  CHECK(std::memcmp(&d, &d2, sizeof(LifeSettings)) == 0, "two defaults are byte-identical");

  // applyLifeSettingField clamps to range and reports unknown keys.
  LifeSettings s = d;
  CHECK(applyLifeSettingField(s, "lifeStepMs", 5) && s.lifeStepMs == 10, "low clamp");
  CHECK(applyLifeSettingField(s, "lifeStepMs", 999999) && s.lifeStepMs == 1000, "high clamp");
  CHECK(applyLifeSettingField(s, "lifeStepMs", 250) && s.lifeStepMs == 250, "in range");
  CHECK(!applyLifeSettingField(s, "nope", 1), "unknown key rejected");

  // --- field-metadata table (pure, ArduinoJson-free) ---
  CHECK(kLifeFieldCount > 0, "non-empty field table");

  bool foundLifeStep = false;
  for (size_t i = 0; i < kLifeFieldCount; i++) {
    if (std::strcmp(kLifeFieldMeta[i].key, "lifeStepMs") == 0) {
      foundLifeStep = true;
      CHECK(kLifeFieldMeta[i].min == 10 && kLifeFieldMeta[i].max == 1000 &&
            kLifeFieldMeta[i].step == 5, "lifeStepMs range/step");
      CHECK(kLifeFieldMeta[i].desc[0] != '\0', "lifeStepMs has desc");
      CHECK(getLifeSettingByIndex(d, i) == 100, "lifeStepMs default via index");
    }
  }
  CHECK(foundLifeStep, "metadata has lifeStepMs");

  long byKey = -1;
  CHECK(getLifeSettingByKey(d, "hueStep", &byKey) && byKey == 3, "by-key hueStep default");
  CHECK(!getLifeSettingByKey(d, "nope", &byKey), "by-key unknown rejected");

  // --- density-throttling birth policy ---
  // Cadence thresholds (defaults: medium=7, large=12, huge=18).
  CHECK(chunkBirthCadence(d, 0) == 1, "cadence: empty cluster -> 1");
  CHECK(chunkBirthCadence(d, 6) == 1, "cadence: below medium -> 1");
  CHECK(chunkBirthCadence(d, 7) == 2, "cadence: medium -> 2");
  CHECK(chunkBirthCadence(d, 11) == 2, "cadence: below large -> 2");
  CHECK(chunkBirthCadence(d, 12) == 3, "cadence: large -> 3");
  CHECK(chunkBirthCadence(d, 18) == 4, "cadence: huge -> 4");
  CHECK(chunkBirthCadence(d, 25) == 4, "cadence: max mass -> 4");

  // Organic mode (default): a dense cluster (mass 10 -> cadence 2) must suppress
  // some births and allow others across positions/generations.
  {
    bool anyAllowed = false, anySuppressed = false;
    for (uint32_t g = 0; g < 4; g++)
      for (uint8_t x = 0; x < 8; x++)
        for (uint8_t y = 0; y < 8; y++) {
          if (throttledBirthAllowed(d, g, x, y, 10)) anyAllowed = true;
          else anySuppressed = true;
        }
    CHECK(anySuppressed, "organic mode throttles some dense births");
    CHECK(anyAllowed, "organic mode still allows some dense births");
  }

  // Classic mode (disableReseed): a legal birth is NEVER throttled, for ANY
  // local mass, position, or generation. This is the pulsar fix.
  {
    LifeSettings classic = d;
    classic.disableReseed = 1;
    bool allAllowed = true;
    for (uint32_t g = 0; g < 4 && allAllowed; g++)
      for (uint8_t x = 0; x < 16 && allAllowed; x++)
        for (uint8_t y = 0; y < 16 && allAllowed; y++)
          for (int mass = 0; mass <= 25; mass++)
            if (!throttledBirthAllowed(classic, g, x, y, (uint8_t)mass)) { allAllowed = false; break; }
    CHECK(allAllowed, "classic mode never throttles a legal birth (pulsar fix)");
  }

  if (g_failures == 0) std::printf("ALL SETTINGS TESTS PASSED\n");
  return g_failures ? 1 : 0;
}
