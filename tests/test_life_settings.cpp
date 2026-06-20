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

  // applyLifeSettingField clamps to range and reports unknown keys.
  LifeSettings s = d;
  CHECK(applyLifeSettingField(s, "lifeStepMs", 5) && s.lifeStepMs == 10, "low clamp");
  CHECK(applyLifeSettingField(s, "lifeStepMs", 999999) && s.lifeStepMs == 1000, "high clamp");
  CHECK(applyLifeSettingField(s, "lifeStepMs", 250) && s.lifeStepMs == 250, "in range");
  CHECK(!applyLifeSettingField(s, "nope", 1), "unknown key rejected");

  // Serialized JSON mentions a known key and its default value.
  char buf[8192];
  size_t n = serializeSettingsJson(d, d, d, buf, sizeof(buf));
  CHECK(n > 0 && n < sizeof(buf), "serialize fits buffer");
  CHECK(std::strstr(buf, "\"key\":\"lifeStepMs\"") != nullptr, "json has lifeStepMs");
  CHECK(std::strstr(buf, "\"default\":100") != nullptr, "json has default 100");

  if (g_failures == 0) std::printf("ALL SETTINGS TESTS PASSED\n");
  return g_failures ? 1 : 0;
}
