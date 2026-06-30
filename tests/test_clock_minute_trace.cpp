// Host trace for the 5-minute clock transition. Renders the minute animation
// frame by frame and asserts two things:
//   1. No pixel ever contains a visible green component (palette guarantee).
//   2. The clock face reaches full brightness *as the pixels finish gathering*,
//      not seconds later -- i.e. the animation "starts" when the move lands.
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#define MATRIX_WIDTH 64
#define MATRIX_BIT_DEPTH 5
#define MATRIX_RGB_CHAINS 1
#define MATRIX_TILE 1
#define WIFI_PORTAL_ENABLED 1

static uint32_t gNowMs = 0;
static uint32_t gNowMicros = 0;

uint32_t millis() { return gNowMs; }
uint32_t micros() { return gNowMicros; }

struct FakeSerial {
  template <typename T> void print(T) {}
  template <typename T> void println(T) {}
};

struct FakeMatrix {
  std::array<uint16_t, MATRIX_WIDTH * 64> pixels = {};
  uint32_t frameCount = 0;

  void drawPixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < 64) {
      pixels[y * MATRIX_WIDTH + x] = color;
    }
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

bool weatherCopySnapshot(WeatherSnapshot &out) {
  out = {};
  return false;
}

void weatherRequestRefresh() {}

#include "../src/life_state.h"
#include "../src/life_util.h"
#include "../src/life_profile.h"
#include "../src/life_color.h"
#include "../src/life_render.h"
#include "../src/life_clock.h"

static uint8_t red8(uint16_t color) { return ((color >> 11) & 0x1F) * 255 / 31; }
static uint8_t green8(uint16_t color) { return ((color >> 5) & 0x3F) * 255 / 63; }
static uint8_t blue8(uint16_t color) { return (color & 0x1F) * 255 / 31; }

static uint8_t bright8(uint16_t color) {
  uint8_t r = red8(color), g = green8(color), b = blue8(color);
  uint8_t m = r > g ? r : g;
  return m > b ? m : b;
}

static bool hasVisibleGreen(uint16_t color) {
  return green8(color) > 8;
}

static void configureHarnessBounds() {
  panelWidth = MATRIX_WIDTH;
  panelHeight = 64;
  activeMask = activeMaskFor(panelWidth);
  for (uint8_t x = 0; x < panelWidth; x++) {
    bitForX[x] = RowBits(0, 0);
    rowBitSet(bitForX[x], x);
  }
}

static void seedGreenHeavyLife() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    currentRows[y] = RowBits(0, 0);
    uint16_t base = y * kMaxWidth;
    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = base + x;
      if (((x * 7 + y * 11) % 5) != 0) {
        currentRows[y] |= bitForX[x];
        cellType[index] = 1;
        cellHue[index] = 96 + ((x + y) & 31);   // deliberately green/cyan source life
        cellSat[index] = 230;
        cellAge[index] = 12;
        visualHue[index] = cellHue[index];
        visualSat[index] = cellSat[index];
        visualValue[index] = 220;
        drawnColor[index] = hsv565(cellHue[index], cellSat[index], visualValue[index]);
        matrix.drawPixel(x, y, drawnColor[index]);
      } else {
        drawnColor[index] = 0;
      }
    }
  }
}

// Tolerance: once the pixels have gathered, the clock face must be essentially
// fully lit within this window. Larger gaps are the "pixels landed but the clock
// hasn't started yet" pause we are guarding against.
static constexpr uint32_t kMaxGatherToBrightMs = 500;

struct FrameSample {
  uint32_t t;
  uint32_t offTargetLit;   // lit pixels not on the digit shape (movers in transit)
  uint8_t meanPct;         // mean brightness over digit-shape pixels, 0..100
  uint8_t colonBright;     // mean brightness over the colon cells, 0..255
};

static int traceMinuteAnimation() {
  configureHarnessBounds();
  seedGreenHeavyLife();
  generation = 42;
  motionGlow = 0;

  if (!beginClockAnimation(kClockAnimationMinute, 12, 35, 0x12345678UL, 0,
                           kClockMinuteAnimationMs)) {
    std::printf("FAIL: beginClockAnimation rejected minute animation\n");
    return 1;
  }

  // The precomputed face (nextRows) is the set of digit-shape target pixels.
  std::array<bool, MATRIX_WIDTH * 64> isTarget = {};
  uint32_t targetCount = 0;
  for (uint8_t y = 0; y < panelHeight; y++) {
    for (uint8_t x = 0; x < panelWidth; x++) {
      if (nextRows[y] & bitForX[x]) {
        isTarget[y * MATRIX_WIDTH + x] = true;
        targetCount++;
      }
    }
  }
  if (targetCount == 0) {
    std::printf("FAIL: no target pixels in precomputed minute face\n");
    return 1;
  }

  // The colon cells must visibly blink; collect their locations.
  std::array<bool, MATRIX_WIDTH * 64> isColon = {};
  uint32_t colonCount = 0;
  for (uint8_t y = 0; y < panelHeight; y++) {
    for (uint8_t x = 0; x < panelWidth; x++) {
      uint8_t col, row;
      if (clockDigitalGrid(x, y, col, row) && clockDigitalColonCell(col, row)) {
        isColon[y * MATRIX_WIDTH + x] = true;
        colonCount++;
      }
    }
  }
  if (colonCount == 0) {
    std::printf("FAIL: no colon cells found in minute face\n");
    return 1;
  }

  std::vector<FrameSample> samples;
  for (uint32_t now = 0; now <= kClockMinuteAnimationMs; now += 33) {
    gNowMs = now;
    gNowMicros = now * 1000;
    renderClockAnimationFrame(now);

    uint32_t offTargetLit = 0;
    uint32_t targetBrightSum = 0;
    uint32_t colonBrightSum = 0;
    for (uint8_t y = 0; y < panelHeight; y++) {
      for (uint8_t x = 0; x < panelWidth; x++) {
        uint16_t color = matrix.pixels[y * MATRIX_WIDTH + x];
        if (hasVisibleGreen(color)) {
          std::printf("FAIL: green pixel frame_ms=%lu stage=%s x=%u y=%u rgb=(%u,%u,%u) color=0x%04x\n",
                      static_cast<unsigned long>(now),
                      now < kClockTransitionMoveMs ? "move" : "fade",
                      x, y, red8(color), green8(color), blue8(color), color);
          return 1;
        }
        uint8_t b = bright8(color);
        if (isColon[y * MATRIX_WIDTH + x]) colonBrightSum += b;
        if (isTarget[y * MATRIX_WIDTH + x]) {
          targetBrightSum += b;
        } else if (b > 24) {
          offTargetLit++;
        }
      }
    }
    uint8_t meanPct = static_cast<uint8_t>((targetBrightSum * 100UL) / (targetCount * 255UL));
    uint8_t colonBright = static_cast<uint8_t>(colonBrightSum / colonCount);
    samples.push_back({now, offTargetLit, meanPct, colonBright});
  }

  // Gather complete: movers in transit have collapsed onto the digit shape.
  // Use a small fraction of the initial straggler count so a few late movers
  // don't hide a finished gather.
  uint32_t initialOffTarget = samples.front().offTargetLit;
  uint32_t gatherThreshold = initialOffTarget / 20;   // 5%
  uint32_t gatherDoneMs = 0;
  bool gathered = false;
  for (const auto &s : samples) {
    if (s.offTargetLit <= gatherThreshold) {
      gatherDoneMs = s.t;
      gathered = true;
      break;
    }
  }
  if (!gathered) {
    std::printf("FAIL: movers never gathered (offTarget stayed > %u, started at %u)\n",
                gatherThreshold, initialOffTarget);
    return 1;
  }

  // Eventual full brightness of the gathered clock face.
  uint8_t postPeak = 0;
  for (const auto &s : samples) {
    if (s.t >= gatherDoneMs && s.meanPct > postPeak) {
      postPeak = s.meanPct;
    }
  }
  if (postPeak < 50) {
    std::printf("FAIL: gathered clock face too dim (peak mean brightness %u%%)\n", postPeak);
    return 1;
  }

  // When does the face actually reach ~full brightness (>=85%% of its peak)?
  uint8_t brightThreshold = static_cast<uint8_t>((postPeak * 85UL) / 100UL);
  uint32_t brightReachedMs = 0;
  bool reached = false;
  for (const auto &s : samples) {
    if (s.t >= gatherDoneMs && s.meanPct >= brightThreshold) {
      brightReachedMs = s.t;
      reached = true;
      break;
    }
  }
  if (!reached) {
    std::printf("FAIL: clock face never reached %u%% brightness after gather\n", brightThreshold);
    return 1;
  }

  uint32_t gap = brightReachedMs - gatherDoneMs;
  if (gap > kMaxGatherToBrightMs) {
    std::printf("FAIL: clock starts %lums after pixels gathered (gather=%lums, bright=%lums, "
                "peak=%u%%); pixels land then sit dim before the clock lights up\n",
                static_cast<unsigned long>(gap),
                static_cast<unsigned long>(gatherDoneMs),
                static_cast<unsigned long>(brightReachedMs), postPeak);
    return 1;
  }

  // The colon must actually BLINK -- oscillate bright then dim, repeatedly -- once
  // the clock is up and settled. A one-time bright->dim drop (a frozen face) is not
  // a blink, so count full low->high pulses rather than just the brightness range.
  const uint8_t kColonLow = 80;
  const uint8_t kColonHigh = 180;
  uint32_t blinkPulses = 0;
  bool colonHigh = false;
  uint8_t colonPeak = 0;
  for (const auto &s : samples) {
    if (s.t < gatherDoneMs + 500) continue;   // skip the gather bloom; measure the steady clock
    if (s.colonBright > colonPeak) colonPeak = s.colonBright;
    if (!colonHigh && s.colonBright >= kColonHigh) {
      colonHigh = true;
      blinkPulses++;
    } else if (colonHigh && s.colonBright <= kColonLow) {
      colonHigh = false;
    }
  }
  if (blinkPulses < 2) {
    std::printf("FAIL: colon does not blink (%lu bright pulse(s), peak=%u over displayed clock) "
                "-- the face is frozen\n",
                static_cast<unsigned long>(blinkPulses), colonPeak);
    return 1;
  }

  std::printf("clock minute trace: no green pixels across rendered frames\n");
  std::printf("clock minute timing: gathered=%lums, full-bright=%lums (gap=%lums <= %lums), peak=%u%%\n",
              static_cast<unsigned long>(gatherDoneMs),
              static_cast<unsigned long>(brightReachedMs),
              static_cast<unsigned long>(gap),
              static_cast<unsigned long>(kMaxGatherToBrightMs), postPeak);
  std::printf("clock minute colon: %lu blink pulses (peak=%u)\n",
              static_cast<unsigned long>(blinkPulses), colonPeak);
  return 0;
}

int main() {
  return traceMinuteAnimation();
}
