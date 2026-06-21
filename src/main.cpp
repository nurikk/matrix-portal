#include <Arduino.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Protomatter.h>
#include <Wire.h>

#include "life_bits.h"
#include "life_settings.h"

#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3)
// MatrixPortal S3 HUB75 pinout from Adafruit Protomatter's official example.
uint8_t rgbPins[] = {42, 41, 40, 38, 39, 37};
uint8_t addrPins[] = {45, 36, 48, 35, 21};
uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin = 14;
#elif defined(_VARIANT_MATRIXPORTAL_M4_)
// MatrixPortal M4 HUB75 pinout from Adafruit Protomatter's official example.
uint8_t rgbPins[] = {7, 8, 9, 10, 11, 12};
uint8_t addrPins[] = {17, 18, 19, 20, 21};
uint8_t clockPin = 14;
uint8_t latchPin = 15;
uint8_t oePin = 16;
#else
#error "Unsupported board: configure Matrix Portal HUB75 pins for this target."
#endif

#ifndef MATRIX_WIDTH
#define MATRIX_WIDTH 64
#endif
#ifndef MATRIX_BIT_DEPTH
#define MATRIX_BIT_DEPTH 5
#endif
#ifndef MATRIX_RGB_CHAINS
#define MATRIX_RGB_CHAINS 1
#endif
#ifndef MATRIX_TILE
#define MATRIX_TILE 1
#endif
#ifndef MATRIX_BENCHMARK
#define MATRIX_BENCHMARK 0
#endif

#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3) && !MATRIX_BENCHMARK
#define WIFI_PORTAL_ENABLED 1
#else
#define WIFI_PORTAL_ENABLED 0
#endif

#if WIFI_PORTAL_ENABLED
#include <WiFi.h>
#include "web_portal.h"
extern volatile bool gShowIpScroll;
extern char gIpText[32];
#endif

constexpr uint16_t kMatrixWidth = MATRIX_WIDTH;
constexpr uint8_t kMatrixBitDepth = MATRIX_BIT_DEPTH;
constexpr uint8_t kMatrixRgbChains = MATRIX_RGB_CHAINS;
constexpr int8_t kMatrixTile = MATRIX_TILE;
constexpr uint8_t kMatrixAddrLines = 5;

Adafruit_Protomatter matrix(
    kMatrixWidth, kMatrixBitDepth,
    kMatrixRgbChains, rgbPins,
    kMatrixAddrLines, addrPins,
    clockPin, latchPin, oePin,
    false, kMatrixTile);

Adafruit_LIS3DH accelerometer = Adafruit_LIS3DH();

#if !MATRIX_BENCHMARK

#include "life_state.h"
#include "life_util.h"
#include "life_profile.h"
#include "life_color.h"
#include "life_render.h"
#include "life_burn.h"
#include "life_input.h"
#include "life_spawn.h"
#include "life_sim.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  ProtomatterStatus status = matrix.begin();
  Serial.print("Protomatter begin status: ");
  Serial.println(static_cast<int>(status));

  if (status != PROTOMATTER_OK) {
    while (true) {
      delay(1000);
    }
  }

  configureLifeBounds();
#if WIFI_PORTAL_ENABLED
  webPortalBegin();   // loads gSaved/gLive from NVS (defaults on first boot)
#endif
  initAccelerometer();
  matrix.fillScreen(0);
  matrix.show();
  seedLife();
  renderFrame();
  resetProfileCounters();
  fpsStartedAt = millis();
  lastSimulationStepAt = fpsStartedAt;
  lastRenderAt = fpsStartedAt;
  framesThisPeriod = 0;
  lifeStepsThisPeriod = 0;
  randomEventsThisPeriod = 0;
  interactionEventsThisPeriod = 0;
  knockEventsThisPeriod = 0;
  burnEventsThisPeriod = 0;
  shakeEventsThisPeriod = 0;
  matrix.getFrameCount();

  Serial.print("Game of Life: ");
  Serial.print(panelWidth);
  Serial.print('x');
  Serial.println(panelHeight);
}

#if WIFI_PORTAL_ENABLED
void scrollIpOnce() {
  matrix.setTextWrap(false);
  matrix.setTextColor(color565(0, 255, 80));
  int16_t textW = 6 * (int16_t)strlen(gIpText);   // default GFX font is 6px wide
  for (int16_t x = panelWidth; x > -textW; x--) {
    matrix.fillScreen(0);
    matrix.setCursor(x, panelHeight / 2 - 4);
    matrix.print(gIpText);
    matrix.show();
    delay(15);
  }
}
#endif

void loop() {
#if WIFI_PORTAL_ENABLED
  webPortalTick();
  if (gShowIpScroll) {
    gShowIpScroll = false;
    scrollIpOnce();
    lastSimulationStepAt = millis();   // avoid a catch-up burst after the pause
    lastRenderAt = lastSimulationStepAt;
  }
  if (gReqReseed) { gReqReseed = false; seedLife(); }
  if (gReqBurn)   { gReqBurn = false; startBurnWave(); }
  if (gReqForget) { gReqForget = false; WiFi.disconnect(true, true); delay(200); ESP.restart(); }  // blocks ~200ms intentionally — device reboots immediately after
#endif
  uint32_t loopStartedAt = micros();
  uint32_t accelStartedAt = loopStartedAt;
  pollAccelerometer();
  uint32_t lifeStartedAt = micros();
  uint32_t now = millis();
  uint16_t simulationInterval = burnWaveActive ? gLive.burnStepMs : gLive.lifeStepMs;
  bool runSimulation = pendingKnocks || now - lastSimulationStepAt >= simulationInterval;
  bool rendered = false;

  if (runSimulation) {
    stepLife();
    lastSimulationStepAt = now;
  }

  uint32_t lifeEndedAt = micros();

  if (runSimulation || now - lastRenderAt >= gLive.renderFrameMs) {
    renderFrame();
    lastRenderAt = now;
    decayMotionEffects();
    rendered = true;
  }

  uint32_t loopEndedAt = micros();

  profileAccelMicros += lifeStartedAt - accelStartedAt;
  if (runSimulation) {
    addProfile(profileLifeMicros, profileLifeMaxMicros, lifeEndedAt - lifeStartedAt);
  }
  addProfile(profileLoopMicros, profileLoopMaxMicros, loopEndedAt - loopStartedAt);
  profileSamples++;
  reportFps();

  if (!rendered && !runSimulation) {
    delay(1);
  }
}

#else

uint32_t benchmarkStartedAt;
uint32_t benchmarkFrames;
uint32_t benchmarkDrawMicros;
uint32_t benchmarkShowMicros;
uint32_t benchmarkLoopMicros;
uint32_t benchmarkDrawMaxMicros;
uint32_t benchmarkShowMaxMicros;
uint32_t benchmarkLoopMaxMicros;

void addBenchmarkTiming(uint32_t &total, uint32_t &maximum, uint32_t elapsed) {
  total += elapsed;
  if (elapsed > maximum) {
    maximum = elapsed;
  }
}

void resetBenchmarkCounters() {
  benchmarkStartedAt = millis();
  benchmarkFrames = 0;
  benchmarkDrawMicros = 0;
  benchmarkShowMicros = 0;
  benchmarkLoopMicros = 0;
  benchmarkDrawMaxMicros = 0;
  benchmarkShowMaxMicros = 0;
  benchmarkLoopMaxMicros = 0;
}

uint16_t benchmarkColor(uint16_t x, uint16_t y, uint32_t frame) {
  uint16_t red = (x + frame) & 7;
  uint16_t green = ((y << 1) + frame) & 15;
  uint16_t blue = (x + y + frame * 3) & 7;
  return (red << 11) | (green << 5) | blue;
}

void drawBenchmarkFrame(uint32_t frame) {
  uint16_t width = matrix.width();
  uint16_t height = matrix.height();

  for (uint16_t y = 0; y < height; y++) {
    for (uint16_t x = 0; x < width; x++) {
      matrix.drawPixel(x, y, benchmarkColor(x, y, frame));
    }
  }
}

void reportBenchmark() {
  uint32_t now = millis();
  uint32_t elapsed = now - benchmarkStartedAt;
  if (elapsed < 1000 || benchmarkFrames == 0) {
    return;
  }

  uint32_t refreshCount = matrix.getFrameCount();
  uint32_t appFps = (benchmarkFrames * 1000UL) / elapsed;
  uint32_t refreshFps = (refreshCount * 1000UL) / elapsed;

  Serial.print("Benchmark ");
  Serial.print(matrix.width());
  Serial.print('x');
  Serial.print(matrix.height());
  Serial.print(" @ ");
  Serial.print(kMatrixBitDepth);
  Serial.print(" bit | app FPS: ");
  Serial.print(appFps);
  Serial.print(" | Refresh FPS: ");
  Serial.print(refreshFps);
  Serial.print(" | avg us draw/show/loop: ");
  Serial.print(benchmarkDrawMicros / benchmarkFrames);
  Serial.print('/');
  Serial.print(benchmarkShowMicros / benchmarkFrames);
  Serial.print('/');
  Serial.print(benchmarkLoopMicros / benchmarkFrames);
  Serial.print(" | max us draw/show/loop: ");
  Serial.print(benchmarkDrawMaxMicros);
  Serial.print('/');
  Serial.print(benchmarkShowMaxMicros);
  Serial.print('/');
  Serial.println(benchmarkLoopMaxMicros);

  resetBenchmarkCounters();
}

void setup() {
  Serial.begin(115200);
  uint32_t serialStartedAt = millis();
  while (!Serial && millis() - serialStartedAt < 5000) {
    delay(10);
  }

  ProtomatterStatus status = matrix.begin();
  Serial.print("Protomatter begin status: ");
  Serial.println(static_cast<int>(status));

  if (status != PROTOMATTER_OK) {
    while (true) {
      delay(1000);
    }
  }

  matrix.fillScreen(0);
  matrix.show();
  matrix.getFrameCount();
  resetBenchmarkCounters();

  Serial.print("Matrix benchmark: ");
  Serial.print(matrix.width());
  Serial.print('x');
  Serial.print(matrix.height());
  Serial.print(" @ ");
  Serial.print(kMatrixBitDepth);
  Serial.println(" bit");
}

void loop() {
  uint32_t loopStartedAt = micros();
  drawBenchmarkFrame(benchmarkFrames);
  uint32_t showStartedAt = micros();
  matrix.show();
  uint32_t loopEndedAt = micros();

  addBenchmarkTiming(benchmarkDrawMicros, benchmarkDrawMaxMicros,
                     showStartedAt - loopStartedAt);
  addBenchmarkTiming(benchmarkShowMicros, benchmarkShowMaxMicros,
                     loopEndedAt - showStartedAt);
  addBenchmarkTiming(benchmarkLoopMicros, benchmarkLoopMaxMicros,
                     loopEndedAt - loopStartedAt);
  benchmarkFrames++;
  reportBenchmark();
}

#endif
