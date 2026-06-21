#pragma once
// life_state.h — Game of Life data model (constants, structs, globals).
// Included once by main.cpp inside the Life build, first in dependency order.
// NOT a standalone translation unit: depends on MATRIX_* macros, WIFI_PORTAL_ENABLED,
// and the types in life_bits.h / life_settings.h (all set up by the main.cpp preamble).

#if MATRIX_WIDTH > 128 || MATRIX_TILE > 2 || MATRIX_TILE < -2 || MATRIX_RGB_CHAINS != 1
#error "The Life firmware currently supports up to a 128x128 single-chain tiled matrix."
#endif

constexpr uint8_t kMaxWidth = MATRIX_WIDTH;
constexpr uint8_t kMaxHeight = MATRIX_TILE < 0 ? 64 * -MATRIX_TILE : 64 * MATRIX_TILE;
constexpr uint16_t kCellCount = kMaxWidth * kMaxHeight;
constexpr uint8_t kTypeCount = 6;
constexpr uint8_t kAccelAddressHigh = 0x19;
constexpr uint8_t kAccelAddressLow = 0x18;
constexpr uint8_t kClickAxisX = 0x01;
constexpr uint8_t kClickAxisY = 0x02;
constexpr uint8_t kClickSignNegative = 0x08;
constexpr uint8_t kClickEventMask = 0x30;
constexpr uint8_t kClickDouble = 0x20;

LifeSettings gLive = defaultLifeSettings();
LifeSettings gSaved = defaultLifeSettings();
LifeSettings gDefaults = defaultLifeSettings();   // non-const: external linkage so web_portal can extern it

#if WIFI_PORTAL_ENABLED
volatile bool gReqReseed = false;
volatile bool gReqBurn = false;
volatile bool gReqForget = false;
volatile bool gPaused = false;     // sim freeze state — written ONLY on core 1 (loop + clearBoard)
volatile int8_t gReqPause = 0;     // core-0 Stop/Resume request: +1 pause, -1 resume; applied on core 1
volatile bool gReqClear = false;   // Clear-all deferred action, consumed by loop() on core 1
volatile uint16_t gStatRenderFps = 0;
volatile uint16_t gStatLifeUps = 0;
int gGeoBitDepth = MATRIX_BIT_DEPTH;
int gGeoTile = MATRIX_TILE;
#endif

struct Hsv {
  uint8_t h;
  uint8_t s;
  uint8_t v;
};

struct NeighborMix {
  uint8_t count;
  uint8_t typeCounts[kTypeCount];
  uint8_t firstHue;
  int16_t hueDeltaSum;
  uint16_t saturationSum;
  bool hasHue;
};

// RowBits and the bitwise Game of Life core live in life_bits.h so they can be
// unit-tested on the host (tests/test_life_bits.cpp).

const uint8_t speciesHues[kTypeCount] = {128, 86, 214, 24, 160, 0};

uint8_t panelWidth = kMaxWidth;
uint8_t panelHeight = kMaxHeight;
RowBits activeMask = RowBits(UINT64_MAX, kMaxWidth > 64 ? UINT64_MAX : 0);
RowBits currentRows[kMaxHeight];
RowBits nextRows[kMaxHeight];
RowBits bitForX[kMaxWidth];
RowBits leftBitForX[kMaxWidth];
RowBits rightBitForX[kMaxWidth];
uint8_t cellType[kCellCount];
uint8_t nextType[kCellCount];
uint8_t cellHue[kCellCount];
uint8_t nextHue[kCellCount];
uint8_t cellSat[kCellCount];
uint8_t nextSat[kCellCount];
uint8_t cellAge[kCellCount];
uint8_t visualHue[kCellCount];
uint8_t visualSat[kCellCount];
uint8_t visualValue[kCellCount];
uint16_t drawnColor[kCellCount];
uint8_t burnHeat[kCellCount];
bool forceRedraw[kCellCount];
uint16_t liveCells;
uint16_t changedCells;
uint16_t updatedPixels;
uint32_t generation;
uint32_t rngState = 0x43D12F5B;
uint32_t fpsStartedAt;
uint32_t framesThisPeriod;
uint16_t lifeStepsThisPeriod;
uint16_t randomEventsThisPeriod;
bool accelerometerReady;
bool accelerometerPrimed;
int16_t accelX;
int16_t accelY;
int16_t accelZ;
int16_t lastAccelX;
int16_t lastAccelY;
int16_t lastAccelZ;
int8_t tiltDx;
int8_t tiltDy;
int8_t tiltHueBias;
uint8_t tiltStrength;
uint8_t motionGlow;
uint8_t pendingKnocks;
uint8_t pendingShakes;
bool burnWaveActive;
uint8_t burnRadius;
uint8_t burnEndRadius;
uint8_t burnCenterX;
uint8_t burnCenterY;
uint8_t pendingBurnCenterX;
uint8_t pendingBurnCenterY;
uint32_t lastAccelReadAt;
uint32_t lastKnockAt;
uint32_t lastShakeAt;
uint32_t lastSimulationStepAt;
uint32_t lastRenderAt;
uint16_t interactionEventsThisPeriod;
uint16_t knockEventsThisPeriod;
uint16_t burnEventsThisPeriod;
uint16_t shakeEventsThisPeriod;
uint32_t profileLoopMicros;
uint32_t profileLifeMicros;
uint32_t profileAccelMicros;
uint32_t profileRenderMicros;
uint32_t profileShowMicros;
uint32_t profileLoopMaxMicros;
uint32_t profileLifeMaxMicros;
uint32_t profileRenderMaxMicros;
uint32_t profileShowMaxMicros;
uint16_t profileSamples;
uint16_t profileRenderSamples;

void seedLife();
