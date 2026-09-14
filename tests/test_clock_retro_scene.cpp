#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#ifndef MATRIX_WIDTH
#define MATRIX_WIDTH 64
#endif
#ifndef MATRIX_TILE
#define MATRIX_TILE 1
#endif
#define MATRIX_BIT_DEPTH 5
#define MATRIX_RGB_CHAINS 1
#define WIFI_PORTAL_ENABLED 1
constexpr int kTestHeight = 64 * (MATRIX_TILE < 0 ? -MATRIX_TILE : MATRIX_TILE);
static uint32_t gNowMs = 0;
static time_t gEpoch = 0;
uint32_t millis() { return gNowMs; }
uint32_t micros() { return gNowMs * 1000; }
extern "C" time_t time(time_t *out) {
  if (out) *out = gEpoch;
  return gEpoch;
}
struct FakeSerial {
  template <typename T> void print(T) {}
  template <typename T> void println(T) {}
} Serial;
struct FakeMatrix {
  std::array<uint16_t, MATRIX_WIDTH * kTestHeight> pixels = {};
  uint32_t frameCount = 0;
  void drawPixel(int x, int y, uint16_t color) {
    assert(x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < kTestHeight);
    pixels[y * MATRIX_WIDTH + x] = color;
  }
  void show() { ++frameCount; }
  uint32_t getFrameCount() const { return frameCount; }
} matrix;

#include "../src/life_bits.h"
#include "../src/life_settings.h"
#include "../src/weather_state.h"
#include "../src/web_portal.h"
bool weatherCopySnapshot(WeatherSnapshot &out) { out = {}; return false; }
void weatherRequestRefresh() {}
#include "../src/life_state.h"
#include "../src/life_util.h"
#include "../src/life_profile.h"
#include "../src/life_color.h"
#include "../src/life_render.h"
#include "../src/life_clock.h"

static void resetHarness(bool empty = false) {
  panelWidth = MATRIX_WIDTH;
  panelHeight = kTestHeight;
  activeMask = activeMaskFor(panelWidth);
  gClockAnimation = {};
  gClockSequencePhase = ClockSequencePhase::Clock;
  gClockHaveSecondAnchor = false;
  gClockSecondAnchorSynced = false;
  gClockLastScheduledMinuteId = 0;
  gPaused = false;
  for (uint8_t x = 0; x < panelWidth; ++x) {
    bitForX[x] = 0;
    rowBitSet(bitForX[x], x);
  }
  for (uint8_t y = 0; y < panelHeight; ++y) {
    currentRows[y] = 0;
    for (uint8_t x = 0; x < panelWidth; ++x) {
      uint16_t i = y * kMaxWidth + x;
      bool alive = !empty && (x + y) % 3 == 0;
      if (alive) currentRows[y] |= bitForX[x];
      cellHue[i] = visualHue[i] = 180;
      cellSat[i] = visualSat[i] = 220;
      visualValue[i] = alive ? 200 : 0;
      cellType[i] = 2;
      cellAge[i] = 9;
      drawnColor[i] = hsv565(visualHue[i], visualSat[i], visualValue[i]);
      matrix.drawPixel(x, y, calibrateColor565(drawnColor[i]));
    }
  }
}

static uint32_t frameHash(const RetroCanvas &canvas) {
  uint32_t hash = 2166136261UL;
  for (uint16_t color : canvas.pixels) hash = (hash ^ color) * 16777619UL;
  return hash;
}
static unsigned countColor(const RetroCanvas &canvas, uint16_t color) {
  unsigned count = 0;
  for (uint16_t pixel : canvas.pixels) count += pixel == color;
  return count;
}

template <std::size_t H, std::size_t W, std::size_t N>
static uint32_t spriteHash(const char (&sprite)[H][W], const uint16_t (&palette)[N]) {
  RetroCanvas canvas;
  canvas.clear(0xF81F);
  canvas.sprite(0, 0, sprite, palette);
  return frameHash(canvas);
}

template <std::size_t F, std::size_t H, std::size_t W, std::size_t N>
static void checkFrameHashes(const char (&sprites)[F][H][W], const uint16_t (&palette)[N],
                             const std::array<uint32_t, F> &expected) {
  for (unsigned frame = 0; frame < F; ++frame)
    assert(spriteHash(sprites[frame], palette) == expected[frame]);
}

static void checkOriginalSprites() {
  checkFrameHashes(kMarioPose, kMarioPalette,
                   std::array<uint32_t, 2>{0x9A81D5CA, 0x8ADDA06E});
  checkFrameHashes(kMarioWalk, kMarioWalkPalette,
                   std::array<uint32_t, 3>{0x175C6688, 0xDCBB2609, 0xEA0CE08F});
  checkFrameHashes(kMarioBlock, kMarioBlockPalette,
                   std::array<uint32_t, 4>{0x81B9114E, 0xD65BB60A, 0xAE7272CA, 0xC902D693});
  for (const auto &frame : kMarioBlock) {
    RetroCanvas block;
    block.sprite(0, 0, frame, kMarioBlockPalette);
    assert(countColor(block, 0x421F) == 0 && countColor(block, 0x0151) == 0);
  }
  checkFrameHashes(kMarioCoin, kMarioCoinPalette,
                   std::array<uint32_t, 3>{0x6C5F869D, 0xC722B1FD, 0x3B629BFD});
  for (const auto &frame : kMarioCoin) {
    RetroCanvas coin;
    coin.sprite(0, 0, frame, kMarioCoinPalette);
    assert(countColor(coin, 0x421F) == 0 && countColor(coin, 0x0151) == 0);
  }
  checkFrameHashes(kPacManLeft, kPacManPalette,
                   std::array<uint32_t, 3>{0xE0B441D1, 0x8639CB45, 0xE10C5F26});
  checkFrameHashes(kGhostLeft, kGhostPalette,
                   std::array<uint32_t, 2>{0x825BCD77, 0x61810DDD});
  checkFrameHashes(kFrightenedGhost, kFrightenedPalette,
                   std::array<uint32_t, 2>{0x88C4146D, 0x75FD366D});
  checkFrameHashes(kKirbyWalk, kKirbyPalette,
                   std::array<uint32_t, 3>{0xDC59BA65, 0x4B4DC019, 0x4229AD35});
  checkFrameHashes(kKirbyStar, kKirbyStarPalette, std::array<uint32_t, 1>{0x762C162E});
  checkFrameHashes(kMegaManTeleport, kMegaManTeleportPalette,
                   std::array<uint32_t, 3>{0x5775CF8D, 0xBC29D125, 0x62CBC265});
  checkFrameHashes(kMegaManWalk, kMegaManPalette,
                   std::array<uint32_t, 3>{0x599158D9, 0x781810D1, 0xC50AC9E6});
  checkFrameHashes(kMegaManShoot, kMegaManShootPalette,
                   std::array<uint32_t, 3>{0x6B1D241A, 0x1D1BEA83, 0x4F96D63D});
  checkFrameHashes(kDuckFly, kDuckPalette,
                   std::array<uint32_t, 3>{0xB3985332, 0xFF23B83D, 0x5497422A});
  checkFrameHashes(kDogLaugh, kDogPalette,
                   std::array<uint32_t, 2>{0x02215C1D, 0x5B21C9D2});
}

static void checkSprites() {
  checkOriginalSprites();
  RetroCanvas canvas;
  constexpr uint8_t sceneCount = static_cast<uint8_t>(RetroScene::Count);
  std::array<uint32_t, sceneCount> firstHashes = {};
  for (uint8_t scene = 0; scene < sceneCount; ++scene) {
    renderRetroScene(canvas, static_cast<RetroScene>(scene), 0);
    firstHashes[scene] = frameHash(canvas);
    for (uint8_t other = 0; other < scene; ++other) assert(firstHashes[scene] != firstHashes[other]);
    for (uint32_t t = 0; t < kRetroSceneDurationMs; t += 33) {
      renderRetroScene(canvas, static_cast<RetroScene>(scene), t);
      assert(countColor(canvas, kBlack) < 4096 - 50);
    }
    renderRetroScene(canvas, static_cast<RetroScene>(scene), 420);
    assert(frameHash(canvas) != firstHashes[scene]);
    renderRetroScene(canvas, static_cast<RetroScene>(scene), 6999);
    uint32_t last = frameHash(canvas);
    renderRetroScene(canvas, static_cast<RetroScene>(scene), UINT32_MAX);
    assert(frameHash(canvas) == last);
  }
  renderRetroScene(canvas, RetroScene::Mario, 2700);
  assert(canvas.pixels[0] == kBlack && countColor(canvas, kSpriteBlack) > 20);
  assert(countColor(canvas, kMarioPalette[1]) > 30);
  renderRetroScene(canvas, RetroScene::PacMan, 0);
  uint32_t pacFirstFrame = frameHash(canvas);
  renderRetroScene(canvas, RetroScene::PacMan, kPacManFrameMs - 1);
  assert(frameHash(canvas) == pacFirstFrame);
  renderRetroScene(canvas, RetroScene::PacMan, kPacManFrameMs);
  assert(frameHash(canvas) != pacFirstFrame);
  renderRetroScene(canvas, RetroScene::PacMan, 0);
  for (uint16_t color : {kGhostPalette[1], kPinkyPalette[1], kInkyPalette[1], kClydePalette[1]})
    assert(countColor(canvas, color) > 20);
  assert(countColor(canvas, kPacDot) > 0);
  renderRetroScene(canvas, RetroScene::PacMan, 3200);
  for (uint16_t color : {kGhostPalette[1], kPinkyPalette[1], kInkyPalette[1], kClydePalette[1]})
    assert(countColor(canvas, color) == 0);
  assert(canvas.pixels[30 * 64 + 60] != kPacDot);
  renderRetroScene(canvas, RetroScene::Kirby, 0);
  assert(canvas.pixels[63] == kBlack && countColor(canvas, kSpriteBlack) > 20);
  assert(countColor(canvas, kKirbyPalette[3]) > 50 && countColor(canvas, kKirbyStarPalette[2]) > 100);
  renderRetroScene(canvas, RetroScene::MegaMan, 600);
  assert(canvas.pixels[63] == kBlack && countColor(canvas, kSpriteBlack) > 20);
  assert(countColor(canvas, kMegaManTeleportPalette[2]) > 20);
  assert(canvas.pixels[28 * 64 + 15] == kBlack);
  renderRetroScene(canvas, RetroScene::MegaMan, 4250);
  assert(countColor(canvas, kMegaManPalette[2]) > 40);
  assert(canvas.pixels[41 * 64 + 57] == kMegaManPalette[2]);
  renderRetroScene(canvas, RetroScene::DuckHunt, 1000);
  assert(canvas.pixels[63] == kBlack && countColor(canvas, kSpriteBlack) > 20);
  assert(countColor(canvas, kDuckPalette[1]) > 20);
  renderRetroScene(canvas, RetroScene::DuckHunt, 4300);
  assert(countColor(canvas, kDuckPalette[1]) == 0);
  renderRetroScene(canvas, RetroScene::DuckHunt, 6000);
  assert(countColor(canvas, kDogPalette[2]) > 200 && countColor(canvas, kDogPalette[3]) > 100);
}

static void checkPanelMatchesScene() {
  for (uint8_t y = 0; y < panelHeight; ++y) {
    for (uint8_t x = 0; x < panelWidth; ++x) {
      uint16_t expected = gClockSceneCanvas.panelPixel(x, y, panelWidth, panelHeight);
      assert(drawnColor[y * kMaxWidth + x] == expected);
      assert(matrix.pixels[y * panelWidth + x] == calibrateColor565(expected));
    }
  }
}

static RetroScene checkSequence(uint8_t request, bool paused, bool empty, uint32_t start) {
  resetHarness(empty);
  gPaused = paused;
  std::array<RowBits, kMaxHeight> originalRows;
  std::array<Hsv, kCellCount> originalVisual;
  for (unsigned y = 0; y < kMaxHeight; ++y) originalRows[y] = currentRows[y];
  for (unsigned i = 0; i < kCellCount; ++i)
    originalVisual[i] = {visualHue[i], visualSat[i], visualValue[i]};
  gEpoch = 0;
  assert(startClockAnimationRequest(request, start));
  assert(gClockAnimation.fastReveal == (request == kClockAnimationRequestKnockHour));
  assert(gClockSequencePhase == ClockSequencePhase::SceneEntry);
  assert(gClockMoveTargetCount > 0);
  RetroScene chosen = gClockScene;
  assert(!startClockAnimationRequest(request, start + 1));
  assert(gClockScene == chosen);
  renderClockAnimationFrame(start);
  renderClockAnimationFrame(start + 1900);
  assert(!clockAnimationFinalFrameDue(start + 1900));
  renderClockAnimationFrame(start + 3799);
  assert(gClockSequencePhase == ClockSequencePhase::SceneEntry);
  renderClockAnimationFrame(start + 3800);
  assert(gClockSequencePhase == ClockSequencePhase::Scene);
  checkPanelMatchesScene();
  RetroCanvas expectedFirst;
  renderRetroScene(expectedFirst, chosen, 0);
  assert(frameHash(expectedFirst) == frameHash(gClockSceneCanvas));
  renderClockAnimationFrame(start + 3800 + 3400);
  checkPanelMatchesScene();
  renderClockAnimationFrame(start + 3800 + 6967);
  auto lastPixels = matrix.pixels;
  uint32_t lastHash = frameHash(gClockSceneCanvas);
  renderClockAnimationFrame(start + 3800 + 7000);
  assert(gClockSequencePhase == ClockSequencePhase::Clock);
  assert(frameHash(gClockSceneCanvas) == lastHash);
  assert(matrix.pixels == lastPixels);
  assert(gClockMoveSourceCount > 0 && gClockMoveTargetCount > 0);
  for (uint8_t y = 0; y < panelHeight; ++y)
    for (uint8_t x = 0; x < panelWidth; ++x)
      assert(bool(gClockSceneSourceRows[y] & bitForX[x]) == bool(lastPixels[y * panelWidth + x]));
  renderClockAnimationFrame(start + 12700);
  assert(matrix.pixels != lastPixels);
  uint32_t end = gClockAnimation.endsAt;
  assert(!clockAnimationFinalFrameDue(end - 1));
  assert(clockAnimationFinalFrameDue(end));
  renderClockAnimationFrame(end);
  assert(!finishClockAnimationAfterRender(end));
  assert(gClockAnimation.finalRendered);
  assert(!finishClockAnimationAfterRender(end + kClockPostAnimationHoldMs - 1));
  renderClockAnimationFrame(end + kClockPostAnimationHoldMs);
  assert(finishClockAnimationAfterRender(end + kClockPostAnimationHoldMs));
  assert(!clockAnimationActive());
  if (paused) {
    for (unsigned y = 0; y < kMaxHeight; ++y) {
      assert(currentRows[y].low == originalRows[y].low);
      assert(currentRows[y].high == originalRows[y].high);
    }
    for (unsigned i = 0; i < kCellCount; ++i) {
      assert(visualHue[i] == originalVisual[i].h && visualSat[i] == originalVisual[i].s &&
             visualValue[i] == originalVisual[i].v && forceRedraw[i]);
      assert(cellType[i] == 2 && cellAge[i] == 9 && cellHue[i] == 180 && cellSat[i] == 220);
    }
  } else assert(liveCells > 0 && clockCountLiveCells(currentRows) == liveCells);
  return chosen;
}

static time_t epochAt(int hour, int minute, int second) {
  struct tm local = {};
  local.tm_year = 125;
  local.tm_mon = 0;
  local.tm_mday = 1;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_sec = second;
  return mktime(&local);
}
static void checkSchedule() {
  resetHarness();
  gEpoch = 0;
  assert(!updateClockAnimation(0));
  gEpoch = epochAt(12, 4, 41);
  assert(!updateClockAnimation(0));
  ++gEpoch;
  assert(!updateClockAnimation(1000));
  assert(!updateClockAnimation(1199));
  assert(updateClockAnimation(1200));
  assert(gClockAnimation.endsAt == 19000);
  assert(gClockAnimation.minute == 5);
  assert(!updateClockAnimation(1300));
  renderClockAnimationFrame(5000);
  renderClockAnimationFrame(11999);
  gEpoch = epochAt(12, 4, 53);
  renderClockAnimationFrame(12000);
  assert(gClockAnimation.endsAt == 19000);
  assert(gClockAnimation.hour == 12 && gClockAnimation.minute == 5);
  gClockAnimation.active = false;
  assert(!updateClockAnimation(12001));

  resetHarness();
  gEpoch = epochAt(12, 59, 56);
  assert(!updateClockAnimation(1000));
  ++gEpoch;
  assert(updateClockAnimation(2000));
  assert(gClockAnimation.kind == kClockAnimationHour);
  renderClockAnimationFrame(5800);
  renderClockAnimationFrame(12799);
  gEpoch = epochAt(13, 0, 8);
  renderClockAnimationFrame(12800);
  assert(gClockAnimation.hour == 13 && gClockAnimation.minute == 0);
  assert(gClockAnimation.endsAt == 24800);

  resetHarness();
  gEpoch = epochAt(12, 59, 50);
  assert(startClockAnimationRequest(kClockAnimationRequestMinute, 0));
  renderClockAnimationFrame(30000);
  assert(gClockSequencePhase == ClockSequencePhase::Scene);
  assert(!clockAnimationFinalFrameDue(30000));
  renderClockAnimationFrame(36999);
  gEpoch = epochAt(13, 0, 27);
  renderClockAnimationFrame(37000);
  assert(gClockAnimation.hour == 13 && gClockAnimation.minute == 0);
  assert(gClockAnimation.endsAt == 44000);
}

static void checkSceneSelection() {
  constexpr uint8_t count = static_cast<uint8_t>(RetroScene::Count);
  for (uint8_t previous = 0; previous <= count; ++previous) {
    std::array<bool, count> selected = {};
    for (uint32_t seed = 1; seed <= 100; ++seed) {
      resetHarness();
      gClockScene = static_cast<RetroScene>(previous);
      rngState = seed;
      assert(startClockAnimationRequest(kClockAnimationRequestMinute, 0));
      uint8_t chosen = static_cast<uint8_t>(gClockScene);
      assert(chosen < count && chosen != previous);
      selected[chosen] = true;
      assert(!startClockAnimationRequest(kClockAnimationRequestHour, 1));
      assert(static_cast<uint8_t>(gClockScene) == chosen);
    }
    for (uint8_t scene = 0; scene < count; ++scene) {
      assert(selected[scene] == (scene != previous));
    }
  }
}

int main() {
  setenv("TZ", "UTC0", 1);
  tzset();
  checkSprites();
  checkSceneSelection();
  for (uint8_t request : {kClockAnimationRequestMinute, kClockAnimationRequestHour,
                          kClockAnimationRequestKnockHour}) {
    checkSequence(request, false, false, 0);
    checkSequence(request, true, false, UINT32_MAX - 5000);
    checkSequence(request, true, true, 0);
  }
  checkSchedule();
  bool selected[5] = {};
  RetroScene previous = gClockScene;
  for (uint32_t seed = 1; seed < 40; ++seed) {
    rngState = seed;
    RetroScene chosen = checkSequence(kClockAnimationRequestMinute, false, false, 0);
    assert(chosen != previous);
    previous = chosen;
    selected[static_cast<uint8_t>(chosen)] = true;
  }
  for (bool seen : selected) assert(seen);
  std::printf("retro scenes: five sprites, complete chains, exact last-frame sources, pause, wrap, schedule %ux%u passed\n",
              panelWidth, panelHeight);
  return 0;
}
