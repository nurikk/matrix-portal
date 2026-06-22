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
#include "life_clock.h"
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
  knockEventsThisPeriod = 0;
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
  if (gReqClear)  { gReqClear = false; clearBoard(); }                     // clearBoard() pauses so the empty board persists
  if (gReqPause)  { gPaused = (gReqPause > 0); gReqPause = 0; }             // explicit Stop/Resume; overrides clear's implicit pause
  if (gReqForget) { gReqForget = false; WiFi.disconnect(true, true); delay(200); ESP.restart(); }  // blocks ~200ms intentionally — device reboots immediately after
#endif
  uint32_t loopStartedAt = micros();
  uint32_t accelStartedAt = loopStartedAt;
  pollAccelerometer();
  uint32_t lifeStartedAt = micros();
  uint32_t now = millis();
  bool clockJustStarted = false;
#if WIFI_PORTAL_ENABLED
  if (gReqClockAnimation) {
    uint8_t request = gReqClockAnimation;
    gReqClockAnimation = 0;
    clockJustStarted = startClockAnimationRequest(request, now);
  }
#endif
  clockJustStarted = updateClockAnimation(now) || clockJustStarted;
  bool clockActive = clockAnimationActive();
  bool runSimulation = now - lastSimulationStepAt >= gLive.lifeStepMs;
#if WIFI_PORTAL_ENABLED
  if (gPaused) runSimulation = false;   // Stop / Clear all from the web portal freezes the sim
#endif
  if (clockActive) runSimulation = false;  // freeze the current Life state while it morphs into the clock face
  bool rendered = false;

  if (runSimulation) {
    stepLife();
    lastSimulationStepAt = now;
  }

  uint32_t lifeEndedAt = micros();

  bool clockFinalDue = clockAnimationFinalFrameDue(now);
  if (runSimulation || clockJustStarted || clockFinalDue || now - lastRenderAt >= gLive.renderFrameMs) {
    if (clockActive) {
      renderClockAnimationFrame(now);
      if (finishClockAnimationAfterRender(now)) {
        lastSimulationStepAt = now;   // release the clock face into Life without a catch-up step burst
      }
    } else {
      renderFrame();
    }
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

#include "benchmark.h"

#endif
