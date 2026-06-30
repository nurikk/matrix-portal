// Host trace for the hourly (weather) clock animation. Renders the full hour
// animation -- gather + weather face -- for every weather icon and temperature
// branch and fails if any pixel reads as green/cyan/yellow. White, blue,
// magenta, pink and red are allowed; only an actual green tint fails.
#include <array>
#include <cstdint>
#include <cstdio>

#define MATRIX_WIDTH 64
#define MATRIX_BIT_DEPTH 5
#define MATRIX_RGB_CHAINS 1
#define MATRIX_TILE 1
#define WIFI_PORTAL_ENABLED 1

static uint32_t gNowMs = 0;
static uint32_t gNowMicros = 0;
uint32_t millis() { return gNowMs; }
uint32_t micros() { return gNowMicros; }

struct FakeSerial { template <typename T> void print(T) {} template <typename T> void println(T) {} };
struct FakeMatrix {
  std::array<uint16_t, MATRIX_WIDTH * 64> pixels = {};
  uint32_t frameCount = 0;
  void drawPixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < 64) pixels[y * MATRIX_WIDTH + x] = color;
  }
  void show() { frameCount++; }
  uint32_t getFrameCount() const { return frameCount; }
};
FakeSerial Serial;
FakeMatrix matrix;

#include "../src/life_bits.h"
#include "../src/life_settings.h"
#include "../src/weather_state.h"
#include "../src/web_portal.h"

// Configurable weather snapshot the harness hands to the clock code.
static WeatherSnapshot gTestWeather = {};
static bool gTestWeatherValid = false;
bool weatherCopySnapshot(WeatherSnapshot &out) { out = gTestWeather; return gTestWeatherValid; }
void weatherRequestRefresh() {}

#include "../src/life_state.h"
#include "../src/life_util.h"
#include "../src/life_profile.h"
#include "../src/life_color.h"
#include "../src/life_render.h"
#include "../src/life_clock.h"

static uint8_t red8(uint16_t c) { return ((c >> 11) & 0x1F) * 255 / 31; }
static uint8_t green8(uint16_t c) { return ((c >> 5) & 0x3F) * 255 / 63; }
static uint8_t blue8(uint16_t c) { return (c & 0x1F) * 255 / 31; }

// Green/cyan/yellow tint: green channel is prominent and clearly exceeds red or
// blue. White/gray (balanced channels), blue, magenta, pink and red all pass.
static bool isGreenish(uint16_t c) {
  int r = red8(c), g = green8(c), b = blue8(c);
  return g > 40 && (g > r + 24 || g > b + 24);
}

static void configureHarnessBounds() {
  panelWidth = MATRIX_WIDTH; panelHeight = 64; activeMask = activeMaskFor(panelWidth);
  for (uint8_t x = 0; x < panelWidth; x++) { bitForX[x] = RowBits(0, 0); rowBitSet(bitForX[x], x); }
}

static void seedLifeHarness() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    currentRows[y] = RowBits(0, 0);
    uint16_t base = y * kMaxWidth;
    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = base + x;
      if (((x * 7 + y * 11) % 5) != 0) {
        currentRows[y] |= bitForX[x];
        cellType[index] = 1;
        cellHue[index] = 96 + ((x + y) & 31);   // green/cyan source life, to stress the gather
        cellSat[index] = 230; cellAge[index] = 12;
        visualHue[index] = cellHue[index]; visualSat[index] = cellSat[index]; visualValue[index] = 220;
        drawnColor[index] = hsv565(cellHue[index], cellSat[index], visualValue[index]);
        matrix.drawPixel(x, y, drawnColor[index]);
      } else { drawnColor[index] = 0; }
    }
  }
}

struct Scenario {
  const char *name;
  bool valid;
  uint8_t code;
  int16_t tempTenths;
  uint8_t unitsF;
};

static int runScenario(const Scenario &s) {
  gTestWeatherValid = s.valid;
  gTestWeather = {};
  gTestWeather.enabled = true;
  gTestWeather.valid = s.valid;
  gTestWeather.unitsF = s.unitsF;
  gTestWeather.weatherCode = s.code;
  gTestWeather.precipitationProbability = 65;
  gTestWeather.temperatureTenths = s.tempTenths;
  gTestWeather.apparentTemperatureTenths = s.tempTenths - 15;
  gTestWeather.highTemperatureTenths = s.tempTenths + 40;
  gTestWeather.lowTemperatureTenths = s.tempTenths - 60;
  gTestWeather.windSpeedTenths = 123;

  configureHarnessBounds();
  seedLifeHarness();
  generation = 7; motionGlow = 0;
  gClockAnimation = {};   // clear any previous animation so begin is accepted

  if (!beginClockAnimation(kClockAnimationHour, 13, 0, 0x0BADC0DEUL ^ s.code, 0,
                           kClockHourAnimationMs)) {
    std::printf("FAIL[%s]: beginClockAnimation rejected hour animation\n", s.name);
    return 1;
  }

  // Snapshot the weather-face target pixels so we can confirm the face actually
  // lights up (de-greening to red/blue could re-trigger the approachColor565 freeze).
  std::array<bool, MATRIX_WIDTH * 64> isTarget = {};
  uint32_t targetCount = 0;
  for (uint8_t y = 0; y < panelHeight; y++)
    for (uint8_t x = 0; x < panelWidth; x++)
      if (nextRows[y] & bitForX[x]) { isTarget[y * MATRIX_WIDTH + x] = true; targetCount++; }

  uint32_t lateBrightSum = 0, lateSamples = 0;
  for (uint32_t now = 0; now <= kClockHourAnimationMs; now += 33) {
    gNowMs = now; gNowMicros = now * 1000;
    renderClockAnimationFrame(now);
    for (uint8_t y = 0; y < panelHeight; y++) {
      for (uint8_t x = 0; x < panelWidth; x++) {
        uint16_t c = matrix.pixels[y * MATRIX_WIDTH + x];
        if (isGreenish(c)) {
          std::printf("FAIL[%s]: green pixel t=%lums stage=%s x=%u y=%u rgb=(%u,%u,%u) color=0x%04x\n",
                      s.name, static_cast<unsigned long>(now),
                      now < kClockTransitionMoveMs ? "gather" : "weather",
                      x, y, red8(c), green8(c), blue8(c), c);
          return 1;
        }
        if (now >= 9000 && isTarget[y * MATRIX_WIDTH + x]) {
          uint8_t r = red8(c), g = green8(c), b = blue8(c);
          uint8_t m = r > g ? r : g; m = m > b ? m : b;
          lateBrightSum += m;
          lateSamples++;
        }
      }
    }
  }

  // After the gather + fade, the weather face must be lit, not frozen dim.
  uint32_t meanLate = lateSamples ? lateBrightSum / lateSamples : 0;
  if (targetCount > 0 && meanLate < 70) {
    std::printf("FAIL[%s]: weather face never lit (mean target brightness %lu late in animation) "
                "-- likely frozen by approachColor565\n",
                s.name, static_cast<unsigned long>(meanLate));
    return 1;
  }
  return 0;
}

int main() {
  const Scenario scenarios[] = {
      {"rain", true, 61, 150, 0},
      {"snow", true, 73, -20, 0},
      {"storm", true, 95, 210, 0},
      {"clear", true, 0, 350, 0},
      {"cloud", true, 3, 180, 0},
      {"hot-F", true, 1, 880, 1},
      {"cold-F", true, 1, 350, 1},
      {"mid-C", true, 2, 150, 0},
      {"invalid", false, 3, 0, 0},
  };
  for (const auto &s : scenarios) {
    if (runScenario(s) != 0) return 1;
  }
  std::printf("clock hour de-green: no green/cyan pixels across %zu weather scenarios\n",
              sizeof(scenarios) / sizeof(scenarios[0]));
  return 0;
}
