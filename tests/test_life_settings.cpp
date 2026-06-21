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
  CHECK(d.shakeDelta == 10000, "default shakeDelta");
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

  if (g_failures == 0) std::printf("ALL SETTINGS TESTS PASSED\n");
  return g_failures ? 1 : 0;
}
