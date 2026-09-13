// Host trace for the 5-minute clock transition. Covers spatial traversal,
// wrapped motion, empty-board reveal, palette, gather timing, animation, and
// framebuffer consistency across the supported panel geometries.
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef MATRIX_WIDTH
#define MATRIX_WIDTH 64
#endif
#define MATRIX_BIT_DEPTH 5
#define MATRIX_RGB_CHAINS 1
#ifndef MATRIX_TILE
#define MATRIX_TILE 1
#endif
#define WIFI_PORTAL_ENABLED 1

static constexpr uint16_t kTestHeight = 64 * MATRIX_TILE;
static constexpr uint32_t kTestCellCount = MATRIX_WIDTH * kTestHeight;

static uint32_t gNowMs = 0;
static uint32_t gNowMicros = 0;

uint32_t millis() { return gNowMs; }
uint32_t micros() { return gNowMicros; }

struct FakeSerial {
  template <typename T> void print(T) {}
  template <typename T> void println(T) {}
};

struct FakeMatrix {
  std::array<uint16_t, kTestCellCount> pixels = {};
  uint32_t frameCount = 0;

  void drawPixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < kTestHeight) {
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

static bool isCoolAurora(uint16_t color) {
  int r = red8(color), g = green8(color), b = blue8(color);
  return b > 48 && (b > r + 12 || g > r + 12);
}

static bool isWarmGold(uint16_t color) {
  int r = red8(color), g = green8(color), b = blue8(color);
  return r > 80 && g > 45 && r > b + 35 && g > b + 20;
}

static void configureHarnessBounds() {
  panelWidth = MATRIX_WIDTH;
  panelHeight = kTestHeight;
  activeMask = activeMaskFor(panelWidth);
  for (uint8_t x = 0; x < panelWidth; x++) {
    bitForX[x] = RowBits(0, 0);
    rowBitSet(bitForX[x], x);
  }
}


static int checkLifeAuroraPalette() {
  rngState = 0x43D12F5B;
  for (uint8_t type = 0; type < kTypeCount; type++) {
    for (uint16_t sample = 0; sample < 512; sample++) {
      uint8_t hue = relatedHue(type);
      uint8_t mutated = mutateHue(hue);
      if (hue < kAuroraHueMin || hue > kAuroraHueMax ||
          mutated < kAuroraHueMin || mutated > kAuroraHueMax) {
        std::printf("FAIL: generated Life hue escaped the Aurora range (%u -> %u)\n",
                    hue, mutated);
        return 1;
      }
    }
  }

  const uint16_t index = 0;
  generation = 0;
  motionGlow = 0;
  cellType[index] = 0;
  cellHue[index] = 120;
  cellSat[index] = 220;
  visualHue[index] = 132;
  visualSat[index] = 180;

  cellAge[index] = 10;
  Hsv mature = targetColorFor(index, 0, 0, true, 0);
  cellAge[index] = 0;
  Hsv newborn = targetColorFor(index, 0, 0, true, 0);
  cellAge[index] = 80;
  Hsv old = targetColorFor(index, 0, 0, true, 0);
  Hsv trail = targetColorFor(index, 0, 0, false, 0);

  if (newborn.h > 70 || newborn.s >= mature.s || newborn.v <= mature.v) {
    std::printf("FAIL: newborn target is not a warm ivory/gold flash\n");
    return 1;
  }
  if (old.v >= mature.v || old.h <= mature.h) {
    std::printf("FAIL: older target does not trend cooler and dimmer\n");
    return 1;
  }
  if (trail.h != visualHue[index] || trail.s != visualSat[index] || trail.v != 0) {
    std::printf("FAIL: dead-cell trail does not retain color while fading to black\n");
    return 1;
  }
  return 0;
}

static int checkColorPipeline() {
  for (uint8_t bits : {5, 6}) {
    uint8_t shift = bits == 5 ? 11 : 5;
    uint8_t maximum = (1 << bits) - 1;
    for (uint8_t from = 0; from <= maximum; from++) {
      for (uint8_t to = 0; to <= maximum; to++) {
        uint16_t current = from << shift, target = to << shift;
        if (approachColor565(current, target, 0) != current) return 1;
        for (uint8_t frame = 0; frame < maximum; frame++) {
          uint16_t next = approachColor565(current, target, 1);
          if ((current < target && (next <= current || next > target)) ||
              (current > target && (next >= current || next < target))) {
            std::printf("FAIL: RGB565 fade stalled or overshot\n");
            return 1;
          }
          current = next;
        }
        if (current != target) return 1;
      }
    }
  }
  if (calibrateColor565(0) != 0 || calibrateColor565(0xFFFF) != 0xCFF7 ||
      calibrateColor565(0x07E0) != 0x07E0) {
    std::printf("FAIL: RGB565 panel calibration gains changed\n");
    return 1;
  }

  for (uint16_t phase = 0; phase < 256; phase++) {
    if (absDiff16(smoothWave8(phase), smoothWave8(phase + 1)) > 4) {
      std::printf("FAIL: breathing wave has an abrupt edge\n");
      return 1;
    }
  }
  uint8_t fading = 255;
  for (uint16_t frame = 0; frame < 100; frame++) {
    uint8_t next = approachEased(fading, 0, 8);
    if ((fading > 0 && next >= fading) || fading - next > 8) return 1;
    fading = next;
  }
  if (fading != 0 || approachEased(4, 0, 8) != 3) return 1;

  cellAge[0] = 10;
  uint8_t dimmest = 255, brightest = 0;
  for (uint32_t now = 0; now < 8192; now += 32) {
    Hsv live = targetColorFor(0, 0, 0, true, now);
    if (live.v < dimmest) dimmest = live.v;
    if (live.v > brightest) brightest = live.v;
    if (targetColorFor(0, 0, 0, false, now).v != 0) return 1;
  }
  if (dimmest < 180 || brightest > 248 || brightest - dimmest < 48) {
    std::printf("FAIL: diffuser tuning lost live-cell contrast or breathing headroom\n");
    return 1;
  }

  cellAge[0] = 10;
  Hsv early = targetColorFor(0, 0, 0, true, 0);
  generation += 100;
  Hsv sameTime = targetColorFor(0, 0, 0, true, 0);
  Hsv later = targetColorFor(0, 0, 0, true, 4096);
  if (early.h != sameTime.h || early.v != sameTime.v || early.v == later.v) {
    std::printf("FAIL: Life shimmer is not wall-time driven\n");
    return 1;
  }

  for (uint8_t y = 0; y < panelHeight; y++) {
    for (uint8_t x = 0; x < panelWidth; x++) {
      uint8_t hue = clockMinutePaletteHue(x, y);
      if (hue < 112 || hue > 208 ||
          (x && absDiff16(hue, clockMinutePaletteHue(x - 1, y)) > 2)) {
        std::printf("FAIL: minute palette is not a continuous Aurora gradient\n");
        return 1;
      }
      Hsv face;
      uint8_t col, row;
      if (clockDigitalPixel(12, 35, x, y, face) &&
          clockDigitalGrid(x, y, col, row) && !clockDigitalColonCell(col, row)) {
        if (face.h != hue || face.s < 240) return 1;
        uint16_t index = static_cast<uint16_t>(y) * kMaxWidth + x;
        nextHue[index] = face.h;
        nextSat[index] = face.s;
        nextType[index] = face.v;
        for (uint32_t elapsed = 0; elapsed < 6144; elapsed += 192) {
          uint32_t now = gClockAnimation.startedAt + elapsed;
          uint16_t color = clockMinuteAnimatedColor(index, x, y, true, 255, 255, now);
          if (bright8(color) < 224 ||
              clockMinuteAnimatedColor(index, x, y, true, 0, 255, now) != 0) {
            std::printf("FAIL: diffuser clock digits dimmed too far or leaked at zero reveal\n");
            return 1;
          }
        }
      }
    }
  }
  uint8_t previous = 0;
  uint16_t litFrames = 0;
  for (uint32_t now = 0; now <= 8192; now += 16) {
    uint8_t twinkle = clockTwinkle(0, now);
    if (absDiff16(previous, twinkle) > 22) {
      std::printf("FAIL: clock twinkle flashes instead of easing\n");
      return 1;
    }
    if (twinkle) litFrames++;
    previous = twinkle;
  }
  if (litFrames < 20 || litFrames > 40) return 1;
  return 0;
}

static int checkRenderModes() {
  gLive = defaultLifeSettings();
  for (uint8_t y = 0; y < panelHeight; y++) currentRows[y] = RowBits(0, 0);
  for (uint16_t i = 0; i < kTestCellCount; i++) {
    visualValue[i] = 0;
    drawnColor[i] = 0;
    forceRedraw[i] = false;
  }
  matrix.pixels.fill(0);
  currentRows[0] = bitForX[0];
  visualHue[0] = cellHue[0] = 140;
  visualSat[0] = cellSat[0] = 220;
  visualValue[0] = 100;
  cellAge[0] = 10;
  forceRedraw[0] = true;
  fillScreenActive = true;
  gNowMs = 4096;
  renderFrame();
  if (drawnColor[0] != hsv565(140, 220, 100) || visualValue[0] != 100 || updatedPixels != 1) {
    std::printf("FAIL: fill color changed during breathing\n");
    return 1;
  }
  fillScreenActive = false;
  gLive.noFade = 1;
  renderFrame();
  Hsv target = targetColorFor(0, 0, 0, true, gNowMs);
  if (drawnColor[0] != hsv565(target.h, target.s, target.v)) return 1;
  currentRows[0] = RowBits(0, 0);
  renderFrame();
  if (drawnColor[0] != 0 || visualValue[0] != 0) return 1;
  renderFrame();
  if (updatedPixels != 0) return 1;
  gLive = defaultLifeSettings();
  return 0;
}

static int checkTransitionGeometry() {
  const uint8_t expected[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  for (uint16_t position = 0; position < 4; position++) {
    uint8_t x, y;
    clockMortonCoordinate(position, x, y);
    if (x != expected[position][0] || y != expected[position][1]) {
      std::printf("FAIL: Morton position %u decoded as (%u,%u), expected (%u,%u)\n",
                  position, x, y, expected[position][0], expected[position][1]);
      return 1;
    }
  }

  uint16_t gridSize = panelWidth > panelHeight ? panelWidth : panelHeight;
  uint16_t expectedPositions = gridSize * gridSize;
  if (clockTransitionPositionCount() != expectedPositions) {
    std::printf("FAIL: transition grid contains %u positions, expected %u\n",
                clockTransitionPositionCount(), expectedPositions);
    return 1;
  }

  if (clockLerpWrappedCoordinate(63, 1, 64, 0) != 63 ||
      clockLerpWrappedCoordinate(63, 1, 64, 127) != 0 ||
      clockLerpWrappedCoordinate(63, 1, 64, 255) != 1 ||
      clockLerpWrappedCoordinate(1, 63, 64, 127) != 0) {
    std::printf("FAIL: 64-wide transition interpolation did not take the shortest wrapped path\n");
    return 1;
  }
  if (gridSize == 128 &&
      (clockLerpWrappedCoordinate(127, 1, 128, 127) != 0 ||
       clockLerpWrappedCoordinate(1, 127, 128, 127) != 0)) {
    std::printf("FAIL: 128-wide transition interpolation did not take the shortest wrapped path\n");
    return 1;
  }

  uint32_t revealStartsAt = kClockTransitionMoveMs - kClockTransitionOverlapMs;
  if (clockTransitionOverlapProgress(revealStartsAt, kClockTransitionMoveMs) != 0 ||
      clockTransitionOverlapProgress(revealStartsAt + kClockTransitionOverlapMs / 2,
                                     kClockTransitionMoveMs) == 0) {
    std::printf("FAIL: target face reveal did not stay hidden until the arrival window\n");
    return 1;
  }
  return 0;
}

static void seedOffPaletteLife() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    currentRows[y] = RowBits(0, 0);
    uint16_t base = y * kMaxWidth;
    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = base + x;
      if (((x * 7 + y * 11) % 5) != 0) {
        currentRows[y] |= bitForX[x];
        cellType[index] = 1;
        cellHue[index] = 64 + ((x + y) & 63);   // broad source hues to stress palette gathering
        cellSat[index] = 230;
        cellAge[index] = 12;
        visualHue[index] = cellHue[index];
        visualSat[index] = cellSat[index];
        visualValue[index] = 220;
        drawnColor[index] = hsv565(cellHue[index], cellSat[index], visualValue[index]);
        matrix.drawPixel(x, y, calibrateColor565(drawnColor[index]));
      } else {
        visualHue[index] = 0;
        visualSat[index] = 0;
        visualValue[index] = 0;
        drawnColor[index] = 0;
        matrix.drawPixel(x, y, 0);
      }
    }
  }
}


static uint8_t meanTargetBrightness() {
  uint32_t total = 0;
  uint32_t count = 0;
  for (uint8_t y = 0; y < panelHeight; y++) {
    for (uint8_t x = 0; x < panelWidth; x++) {
      if (nextRows[y] & bitForX[x]) {
        total += bright8(matrix.pixels[y * MATRIX_WIDTH + x]);
        count++;
      }
    }
  }
  return count ? static_cast<uint8_t>(total / count) : 0;
}

static int checkEmptyTransition() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    currentRows[y] = RowBits(0, 0);
  }
  matrix.pixels.fill(0);
  for (uint16_t index = 0; index < kTestCellCount; index++) {
    drawnColor[index] = 0;
  }
  gClockAnimation = {};

  if (!beginClockAnimation(kClockAnimationMinute, 12, 35, 0x87654321UL, 0,
                           kClockMinuteAnimationMs) ||
      clockTransitionActive(0)) {
    std::printf("FAIL: empty board did not use the fallback clock reveal\n");
    return 1;
  }

  const uint32_t sampleTimes[] = {0, kClockMinuteAnimationMs / 2,
                                  kClockMinuteAnimationMs - 33,
                                  kClockMinuteAnimationMs + 33};
  uint8_t brightness[4] = {};
  for (uint8_t i = 0; i < 4; i++) {
    gNowMs = sampleTimes[i];
    gNowMicros = sampleTimes[i] * 1000;
    renderClockAnimationFrame(sampleTimes[i]);
    brightness[i] = meanTargetBrightness();
  }

  if (brightness[0] != 0 || brightness[1] == 0 || brightness[2] <= brightness[1] ||
      brightness[3] + 16 < brightness[2]) {
    std::printf("FAIL: empty transition brightness was not gradual (%u,%u,%u,%u)\n",
                brightness[0], brightness[1], brightness[2], brightness[3]);
    return 1;
  }
  gClockAnimation = {};
  return 0;
}

static int checkPausedClockRestoration() {
  seedOffPaletteLife();
  std::array<Hsv, kTestCellCount> originalVisual = {};
  std::array<RowBits, kTestHeight> originalRows = {};
  for (uint16_t i = 0; i < kTestCellCount; i++) {
    originalVisual[i] = {visualHue[i], visualSat[i], visualValue[i]};
  }
  for (uint8_t y = 0; y < panelHeight; y++) originalRows[y] = currentRows[y];
  gPaused = true;
  gClockAnimation = {};
  if (!beginClockAnimation(kClockAnimationMinute, 12, 35, 1234, 0, kClockMinuteAnimationMs)) return 1;
  renderClockAnimationFrame(kClockMinuteAnimationMs);
  if (finishClockAnimationAfterRender(kClockMinuteAnimationMs)) return 1;
  uint32_t releaseAt = kClockMinuteAnimationMs + kClockPostAnimationHoldMs;
  renderClockAnimationFrame(releaseAt);
  if (!finishClockAnimationAfterRender(releaseAt) || clockAnimationActive()) return 1;
  for (uint16_t i = 0; i < kTestCellCount; i++) {
    if (visualHue[i] != originalVisual[i].h || visualSat[i] != originalVisual[i].s ||
        visualValue[i] != originalVisual[i].v || !forceRedraw[i]) {
      std::printf("FAIL: paused clock overwrote Life visual state\n");
      return 1;
    }
  }
  for (uint8_t y = 0; y < panelHeight; y++) {
    if (currentRows[y].low != originalRows[y].low || currentRows[y].high != originalRows[y].high) return 1;
  }
  gPaused = false;
  return 0;
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
  if (checkLifeAuroraPalette() != 0) return 1;
  if (checkColorPipeline() != 0) return 1;
  if (checkRenderModes() != 0) return 1;
  if (checkTransitionGeometry() != 0) return 1;
  if (checkEmptyTransition() != 0) return 1;
  if (checkPausedClockRestoration() != 0) return 1;
  seedOffPaletteLife();
  generation = 42;
  motionGlow = 0;

  if (!beginClockAnimation(kClockAnimationMinute, 12, 35, 0x12345678UL, 0,
                           kClockMinuteAnimationMs)) {
    std::printf("FAIL: beginClockAnimation rejected minute animation\n");
    return 1;
  }

  std::array<uint16_t, kTestCellCount> sourceFrame = matrix.pixels;
  renderClockAnimationFrame(0);
  if (matrix.pixels != sourceFrame) {
    std::printf("FAIL: clock gathering jumps away from displayed source colors\n");
    return 1;
  }

  // The precomputed face (nextRows) is the set of digit-shape target pixels.
  std::array<bool, kTestCellCount> isTarget = {};
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
  std::array<bool, kTestCellCount> isColon = {};
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
  uint32_t coolAuroraSamples = 0;
  uint32_t warmGoldSamples = 0;
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
        uint8_t b = bright8(color);
        if (isColon[y * MATRIX_WIDTH + x]) colonBrightSum += b;
        if (isTarget[y * MATRIX_WIDTH + x]) {
          targetBrightSum += b;
          if (now >= kClockTransitionMoveMs) {
            if (isCoolAurora(color)) coolAuroraSamples++;
            if (isWarmGold(color)) warmGoldSamples++;
          }
        } else if (b > 24) {
          offTargetLit++;
        }
      }
    }
    uint8_t meanPct = static_cast<uint8_t>((targetBrightSum * 100UL) / (targetCount * 255UL));
    uint8_t colonBright = static_cast<uint8_t>(colonBrightSum / colonCount);
    samples.push_back({now, offTargetLit, meanPct, colonBright});
  }

  if (coolAuroraSamples == 0 || warmGoldSamples == 0) {
    std::printf("FAIL: rendered minute palette missing Aurora colors (cool=%lu gold=%lu)\n",
                static_cast<unsigned long>(coolAuroraSamples),
                static_cast<unsigned long>(warmGoldSamples));
    return 1;
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

  std::array<uint16_t, kTestCellCount> displayed = matrix.pixels;
  commitClockFaceToLife();
  for (uint16_t index = 0; index < displayed.size(); index++) {
    uint16_t visual = calibrateColor565(
        hsv565(visualHue[index], visualSat[index], visualValue[index]));
    if (absDiff16(red8(visual), red8(displayed[index])) > 9 ||
        absDiff16(green8(visual), green8(displayed[index])) > 9 ||
        absDiff16(blue8(visual), blue8(displayed[index])) > 9) {
      std::printf("FAIL: clock release lost displayed color at %u (0x%04x -> 0x%04x)\n",
                  index, displayed[index], visual);
      return 1;
    }
    if (calibrateColor565(drawnColor[index]) != displayed[index]) {
      std::printf("FAIL: clock release changed framebuffer cache at index %u "
                  "(displayed=0x%04x cached=0x%04x)\n",
                  index, displayed[index], drawnColor[index]);
      return 1;
    }
  }

  std::array<uint8_t, kTestCellCount> releasedValues = {};
  for (uint16_t index = 0; index < kTestCellCount; index++) releasedValues[index] = visualValue[index];
  renderFrame();
  for (uint16_t index = 0; index < kTestCellCount; index++) {
    if (absDiff16(visualValue[index], releasedValues[index]) > gLive.liveValueStep ||
        calibrateColor565(drawnColor[index]) != matrix.pixels[index]) {
      std::printf("FAIL: first Life frame after clock release jumped or lost cache consistency\n");
      return 1;
    }
  }

  std::printf("clock minute geometry: %ux%u\n", panelWidth, panelHeight);
  std::printf("clock minute palette: cool Aurora=%lu, warm gold=%lu rendered samples\n",
              static_cast<unsigned long>(coolAuroraSamples),
              static_cast<unsigned long>(warmGoldSamples));
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
