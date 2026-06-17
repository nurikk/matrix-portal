#include <Arduino.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Protomatter.h>

// MatrixPortal M4 HUB75 pinout from Adafruit Protomatter's official example.
uint8_t rgbPins[] = {7, 8, 9, 10, 11, 12};
uint8_t addrPins[] = {17, 18, 19, 20, 21};
uint8_t clockPin = 14;
uint8_t latchPin = 15;
uint8_t oePin = 16;

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

#if MATRIX_WIDTH > 64 || MATRIX_TILE != 1 || MATRIX_RGB_CHAINS != 1
#error "The Life firmware currently supports one 64x64 chain. Use MATRIX_BENCHMARK for larger panel timing tests."
#endif

constexpr uint8_t kMaxWidth = 64;
constexpr uint8_t kMaxHeight = 64;
constexpr uint16_t kCellCount = kMaxWidth * kMaxHeight;
constexpr uint16_t kMinLiveCells = 8;
constexpr uint8_t kTypeCount = 6;
constexpr uint8_t kHueStep = 7;
constexpr uint8_t kSatStep = 18;
constexpr uint8_t kLiveValueStep = 36;
constexpr uint8_t kDeathValueStep = 24;
constexpr uint8_t kAccelAddressHigh = 0x19;
constexpr uint8_t kAccelAddressLow = 0x18;
constexpr uint8_t kAccelPollMs = 35;
constexpr int16_t kTiltDeadzone = 650;
constexpr int16_t kStrongTilt = 2500;
constexpr uint16_t kShakeDelta = 10000;
constexpr uint8_t kBurnRingWidth = 2;
constexpr uint8_t kBurnFadeStep = 34;

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

const uint8_t speciesHues[kTypeCount] = {128, 86, 214, 24, 160, 0};

uint8_t panelWidth = kMaxWidth;
uint8_t panelHeight = kMaxHeight;
uint64_t activeMask = UINT64_MAX;
uint64_t currentRows[kMaxHeight];
uint64_t nextRows[kMaxHeight];
uint64_t bitForX[kMaxWidth];
uint64_t leftBitForX[kMaxWidth];
uint64_t rightBitForX[kMaxWidth];
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
uint32_t lastAccelReadAt;
uint32_t lastKnockAt;
uint32_t lastShakeAt;
uint16_t interactionEventsThisPeriod;
uint16_t knockEventsThisPeriod;
uint16_t burnEventsThisPeriod;
uint16_t shakeEventsThisPeriod;
uint16_t tiltEventsThisPeriod;
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

void addProfile(uint32_t &total, uint32_t &maximum, uint32_t elapsed) {
  total += elapsed;
  if (elapsed > maximum) {
    maximum = elapsed;
  }
}

uint32_t averageProfile(uint32_t total, uint16_t samples) {
  return samples ? total / samples : 0;
}

uint32_t profiledSimulationMicros() {
  uint32_t renderAndShow = profileRenderMicros + profileShowMicros;
  return profileLifeMicros > renderAndShow ? profileLifeMicros - renderAndShow : 0;
}

void resetProfileCounters() {
  profileLoopMicros = 0;
  profileLifeMicros = 0;
  profileAccelMicros = 0;
  profileRenderMicros = 0;
  profileShowMicros = 0;
  profileLoopMaxMicros = 0;
  profileLifeMaxMicros = 0;
  profileRenderMaxMicros = 0;
  profileShowMaxMicros = 0;
  profileSamples = 0;
  profileRenderSamples = 0;
}

uint32_t random32() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

uint8_t popcount64(uint64_t value) {
  return __builtin_popcountll(value);
}

uint8_t ctz64(uint64_t value) {
  return __builtin_ctzll(value);
}

uint8_t randomType() {
  return random32() % kTypeCount;
}

uint8_t triWave6(uint8_t value) {
  value &= 63;
  return value < 32 ? value : 63 - value;
}

uint8_t addSaturated(uint8_t a, uint8_t b) {
  uint16_t sum = a + b;
  return sum > 255 ? 255 : sum;
}

uint16_t abs16(int16_t value) {
  return value < 0 ? -value : value;
}

uint16_t absDiff16(int16_t a, int16_t b) {
  int32_t delta = static_cast<int32_t>(a) - b;
  if (delta < 0) {
    delta = -delta;
  }
  return delta > 65535 ? 65535 : delta;
}

int16_t clamp16(int16_t value, int16_t low, int16_t high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

void raiseMotionGlow(uint8_t amount) {
  if (amount > motionGlow) {
    motionGlow = amount;
  }
}

uint8_t wrapHue(int16_t hue) {
  return static_cast<uint8_t>(hue);
}

uint8_t motionType() {
  return ((static_cast<uint16_t>(wrapHue(motionGlow + tiltStrength + 19)) *
           kTypeCount) >>
          8) %
         kTypeCount;
}

uint8_t motionHue(uint8_t type) {
  return wrapHue(speciesHues[type % kTypeCount] + tiltHueBias * 3 + motionGlow);
}

bool i2cResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

int16_t hueDelta(uint8_t from, uint8_t to) {
  int16_t delta = static_cast<int16_t>(to) - from;
  if (delta > 127) {
    delta -= 256;
  } else if (delta < -128) {
    delta += 256;
  }
  return delta;
}

uint8_t blendHue(uint8_t from, uint8_t to, uint8_t amount) {
  return wrapHue(from + (hueDelta(from, to) * amount) / 255);
}

uint8_t approachHue(uint8_t current, uint8_t target, uint8_t step) {
  int16_t delta = hueDelta(current, target);
  if (delta > step) {
    return wrapHue(current + step);
  }
  if (delta < -step) {
    return wrapHue(current - step);
  }
  return target;
}

uint8_t approach(uint8_t current, uint8_t target, uint8_t step) {
  if (current < target) {
    uint16_t next = current + step;
    return next > target ? target : next;
  }

  if (current > target) {
    return current - target > step ? current - step : target;
  }

  return current;
}

uint8_t relatedHue(uint8_t type) {
  uint8_t hue = speciesHues[type % kTypeCount];
  uint8_t roll = random32() & 31;

  if (roll == 0) {
    return wrapHue(hue + 128); // complement
  }
  if (roll < 4) {
    return wrapHue(hue + ((random32() & 1) ? 85 : -85)); // triad
  }
  if (roll < 18) {
    return wrapHue(hue + static_cast<int16_t>((random32() % 33) - 16)); // analogous
  }

  return hue;
}

uint8_t mutateHue(uint8_t hue) {
  uint8_t roll = random32() & 255;

  if (roll == 0) {
    return wrapHue(hue + 128);
  }
  if (roll < 3) {
    return wrapHue(hue + ((random32() & 1) ? 85 : -85));
  }
  if (roll < 18) {
    return wrapHue(hue + static_cast<int16_t>((random32() % 25) - 12));
  }

  return hue;
}

void addNeighbor(NeighborMix &mix, uint16_t index) {
  uint8_t type = cellType[index] % kTypeCount;
  uint8_t hue = cellHue[index];

  mix.typeCounts[type]++;
  if (!mix.hasHue) {
    mix.firstHue = hue;
    mix.hasHue = true;
  } else {
    mix.hueDeltaSum += hueDelta(mix.firstHue, hue);
  }
  mix.saturationSum += cellSat[index];
  mix.count++;
}

uint8_t mixedHue(const NeighborMix &mix) {
  if (mix.count == 0) {
    return relatedHue(randomType());
  }
  return wrapHue(mix.firstHue + mix.hueDeltaSum / mix.count);
}

uint8_t mixedSaturation(const NeighborMix &mix) {
  if (mix.count == 0) {
    return 225;
  }
  return addSaturated(mix.saturationSum / mix.count, 10);
}

uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<uint16_t>(r & 0xF8) << 8) |
         (static_cast<uint16_t>(g & 0xFC) << 3) |
         (b >> 3);
}

uint16_t hsv565(uint8_t hue, uint8_t saturation, uint8_t value) {
  if (value == 0) {
    return 0;
  }
  if (saturation == 0) {
    return color565(value, value, value);
  }

  uint16_t scaledHue = static_cast<uint16_t>(hue) * 6;
  uint8_t sector = scaledHue >> 8;
  uint8_t fraction = scaledHue & 255;
  uint8_t p = (static_cast<uint16_t>(value) * (255 - saturation)) >> 8;
  uint8_t q = (static_cast<uint16_t>(value) *
               (255 - ((static_cast<uint16_t>(saturation) * fraction) >> 8))) >>
              8;
  uint8_t t = (static_cast<uint16_t>(value) *
               (255 - ((static_cast<uint16_t>(saturation) * (255 - fraction)) >> 8))) >>
              8;

  switch (sector) {
  case 0:
    return color565(value, t, p);
  case 1:
    return color565(q, value, p);
  case 2:
    return color565(p, value, t);
  case 3:
    return color565(p, q, value);
  case 4:
    return color565(t, p, value);
  default:
    return color565(value, p, q);
  }
}

void configureLifeBounds() {
  int16_t width = matrix.width();
  int16_t height = matrix.height();
  panelWidth = width < kMaxWidth ? width : kMaxWidth;
  panelHeight = height < kMaxHeight ? height : kMaxHeight;
  activeMask = panelWidth == 64 ? UINT64_MAX : ((1ULL << panelWidth) - 1ULL);

  for (uint8_t x = 0; x < panelWidth; x++) {
    bitForX[x] = 1ULL << x;
    leftBitForX[x] = 1ULL << (x == 0 ? panelWidth - 1 : x - 1);
    rightBitForX[x] = 1ULL << (x + 1 == panelWidth ? 0 : x + 1);
  }
}

int16_t tiltMappedX();
int16_t tiltMappedY();
void seedLife();

Hsv targetColorFor(uint16_t index, uint8_t x, uint8_t y, bool alive) {
  if (burnHeat[index]) {
    uint8_t heat = burnHeat[index];
    uint8_t hue = heat > 190 ? 10 : wrapHue(4 + (190 - heat) / 4);
    uint8_t saturation = heat > 225 ? 55 : 245;
    return {hue, saturation, heat};
  }

  if (!alive) {
    return {visualHue[index], 0, 0};
  }

  uint8_t wave = triWave6(generation * 5 + x * 4 + y * 7 + cellType[index] * 11);
  uint8_t shimmer = triWave6(generation * 3 + x * 5 + y * 3);
  uint8_t hue = wrapHue(cellHue[index] + tiltHueBias +
                        static_cast<int16_t>(shimmer / 2) - 8);
  uint8_t saturation = cellSat[index];
  uint8_t value = 150 + wave * 3;

  if (motionGlow) {
    value = addSaturated(value, motionGlow >> 1);
    saturation = addSaturated(saturation, motionGlow >> 3);
  }

  if (cellAge[index] < 6) {
    uint8_t bloom = (6 - cellAge[index]) * 26;
    value = addSaturated(value, bloom);
    saturation = saturation > bloom ? saturation - bloom : 0;
  }

  return {hue, saturation, value};
}

void renderFrame() {
  uint32_t renderStartedAt = micros();
  updatedPixels = 0;

  for (uint8_t y = 0; y < panelHeight; y++) {
    uint64_t row = currentRows[y] & activeMask;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = baseIndex + x;
      bool alive = row & bitForX[x];
      Hsv target = targetColorFor(index, x, y, alive);
      bool force = forceRedraw[index];
      forceRedraw[index] = false;
      uint8_t nextHueValue = approachHue(visualHue[index], target.h, kHueStep);
      uint8_t nextSatValue = approach(visualSat[index], target.s, kSatStep);
      uint8_t valueStep = burnHeat[index] ? kLiveValueStep
                                          : (alive ? kLiveValueStep : kDeathValueStep);
      uint8_t nextValue = approach(visualValue[index], target.v, valueStep);

      if (!force && nextHueValue == visualHue[index] && nextSatValue == visualSat[index] &&
          nextValue == visualValue[index]) {
        continue;
      }

      uint16_t color = hsv565(nextHueValue, nextSatValue, nextValue);

      visualHue[index] = nextHueValue;
      visualSat[index] = nextSatValue;
      visualValue[index] = nextValue;

      if (force || color != drawnColor[index]) {
        drawnColor[index] = color;
        matrix.drawPixel(x, y, color);
        updatedPixels++;
      }
    }
  }

  uint32_t showStartedAt = micros();
  matrix.show();
  uint32_t showEndedAt = micros();

  addProfile(profileRenderMicros, profileRenderMaxMicros,
             showStartedAt - renderStartedAt);
  addProfile(profileShowMicros, profileShowMaxMicros, showEndedAt - showStartedAt);
  profileRenderSamples++;
}

void clearBurnHeat() {
  for (uint16_t index = 0; index < kCellCount; index++) {
    burnHeat[index] = 0;
  }
}

void startBurnWave() {
  pendingKnocks = 0;
  if (burnWaveActive) {
    return;
  }

  burnWaveActive = true;
  burnRadius = 0;
  pendingShakes = 0;
  int16_t centerX = panelWidth / 2;
  int16_t centerY = panelHeight / 2;
  uint16_t farthestSquared = 0;

  for (uint8_t yCorner = 0; yCorner < 2; yCorner++) {
    int16_t y = yCorner ? panelHeight - 1 : 0;
    for (uint8_t xCorner = 0; xCorner < 2; xCorner++) {
      int16_t x = xCorner ? panelWidth - 1 : 0;
      int16_t dx = x - centerX;
      int16_t dy = y - centerY;
      uint16_t distanceSquared = static_cast<uint16_t>(dx * dx + dy * dy);
      if (distanceSquared > farthestSquared) {
        farthestSquared = distanceSquared;
      }
    }
  }

  burnEndRadius = 0;
  while (static_cast<uint16_t>(burnEndRadius) * burnEndRadius < farthestSquared &&
         burnEndRadius < 240) {
    burnEndRadius++;
  }
  burnEndRadius += kBurnRingWidth + 8;
  clearBurnHeat();
  burnEventsThisPeriod++;
  raiseMotionGlow(255);
}

void finishBurnWave() {
  burnWaveActive = false;
  burnRadius = 0;
  clearBurnHeat();
  seedLife();
}

void stepBurnWave() {
  pendingKnocks = 0;
  pendingShakes = 0;
  uint16_t nextLiveCells = 0;
  uint16_t burnedCells = 0;
  bool hasHeat = false;
  int16_t centerX = panelWidth / 2;
  int16_t centerY = panelHeight / 2;
  uint8_t innerRadius = burnRadius > kBurnRingWidth ? burnRadius - kBurnRingWidth : 0;
  uint8_t outerRadius = burnRadius + kBurnRingWidth;
  uint16_t killRadiusSquared = static_cast<uint16_t>(burnRadius) * burnRadius;
  uint16_t innerSquared = static_cast<uint16_t>(innerRadius) * innerRadius;
  uint16_t outerSquared = static_cast<uint16_t>(outerRadius) * outerRadius;

  for (uint8_t y = 0; y < panelHeight; y++) {
    uint64_t row = currentRows[y] & activeMask;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = baseIndex + x;
      uint8_t heat = burnHeat[index];
      if (heat > kBurnFadeStep) {
        heat -= kBurnFadeStep;
      } else {
        heat = 0;
      }

      int16_t dx = x - centerX;
      int16_t dy = y - centerY;
      uint16_t distanceSquared = static_cast<uint16_t>(dx * dx + dy * dy);
      if (distanceSquared >= innerSquared && distanceSquared <= outerSquared) {
        heat = 255;
      }

      if (distanceSquared <= killRadiusSquared && (row & bitForX[x])) {
        row &= ~bitForX[x];
        burnedCells++;
      }

      burnHeat[index] = heat;
      hasHeat = hasHeat || heat;
    }

    currentRows[y] = row;
    nextLiveCells += popcount64(row);
  }

  liveCells = nextLiveCells;
  changedCells = burnedCells;
  generation++;
  renderFrame();

  if (burnRadius < 250) {
    burnRadius++;
  }
  if (burnRadius > burnEndRadius && !hasHeat) {
    finishBurnWave();
  }
}

void seedLife() {
  rngState ^= micros() + 0x9E3779B9;
  liveCells = 0;
  changedCells = panelWidth * panelHeight;
  generation = 0;

  for (uint8_t y = 0; y < panelHeight; y++) {
    uint64_t row = 0;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = baseIndex + x;
      if ((random32() & 255) < 72) {
        row |= bitForX[x];
        cellType[index] = randomType();
        cellHue[index] = relatedHue(cellType[index]);
        cellSat[index] = 205 + (random32() & 31);
        cellAge[index] = random32() & 31;
      } else {
        cellAge[index] = 0;
      }
    }

    currentRows[y] = row & activeMask;
    nextRows[y] = 0;
    liveCells += popcount64(currentRows[y]);
  }

  renderFrame();
}

uint8_t dominantType(uint8_t counts[kTypeCount], uint8_t fallback) {
  uint8_t bestType = fallback % kTypeCount;
  uint8_t bestCount = counts[bestType];

  for (uint8_t type = 0; type < kTypeCount; type++) {
    if (counts[type] > bestCount) {
      bestType = type;
      bestCount = counts[type];
    }
  }

  return bestCount == 0 ? randomType() : bestType;
}

uint8_t maybeMutate(uint8_t type) {
  if ((random32() & 255) == 0) {
    return randomType();
  }

  return type % kTypeCount;
}

void wrapPoint(int16_t &x, int16_t &y) {
  while (x < 0) {
    x += panelWidth;
  }
  while (x >= panelWidth) {
    x -= panelWidth;
  }
  while (y < 0) {
    y += panelHeight;
  }
  while (y >= panelHeight) {
    y -= panelHeight;
  }
}

void setNextAliveHsv(int16_t x, int16_t y, uint8_t type, uint8_t hue,
                     uint8_t saturation) {
  wrapPoint(x, y);
  uint16_t index = y * kMaxWidth + x;
  uint8_t normalizedType = type % kTypeCount;
  nextRows[y] |= bitForX[x];
  nextType[index] = normalizedType;
  nextHue[index] = hue;
  nextSat[index] = saturation;
}

void setNextAlive(int16_t x, int16_t y, uint8_t type) {
  uint8_t normalizedType = type % kTypeCount;
  setNextAliveHsv(x, y, normalizedType, mutateHue(relatedHue(normalizedType)),
                  210 + (random32() & 31));
}

void setNextDead(int16_t x, int16_t y) {
  wrapPoint(x, y);
  nextRows[y] &= ~bitForX[x];
}

void addGlider(int16_t cx, int16_t cy, uint8_t type, uint8_t rotation) {
  const int8_t offsets[5][2] = {{0, -1}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};

  for (uint8_t i = 0; i < 5; i++) {
    int8_t x = offsets[i][0];
    int8_t y = offsets[i][1];
    int8_t rx = x;
    int8_t ry = y;

    if ((rotation & 3) == 1) {
      rx = -y;
      ry = x;
    } else if ((rotation & 3) == 2) {
      rx = -x;
      ry = -y;
    } else if ((rotation & 3) == 3) {
      rx = y;
      ry = -x;
    }

    setNextAlive(cx + rx, cy + ry, type + (i == 0 ? 0 : random32() & 1));
  }
}

void addBurst(int16_t cx, int16_t cy, uint8_t type) {
  for (int8_t y = -2; y <= 2; y++) {
    for (int8_t x = -2; x <= 2; x++) {
      uint8_t distance = abs(x) + abs(y);
      if (distance <= 2 && (distance == 0 || (random32() & 1))) {
        setNextAlive(cx + x, cy + y, type + (random32() & 3));
      }
    }
  }
}

void addVoid(int16_t cx, int16_t cy, uint8_t radius) {
  for (int8_t y = -radius; y <= radius; y++) {
    for (int8_t x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        setNextDead(cx + x, cy + y);
      }
    }
  }
}

int16_t tiltMappedX() {
  int16_t mapped = panelWidth / 2 +
                   (static_cast<int32_t>(accelX) * panelWidth) / 18000;
  return clamp16(mapped, 0, panelWidth - 1);
}

int16_t tiltMappedY() {
  int16_t mapped = panelHeight / 2 +
                   (static_cast<int32_t>(accelY) * panelHeight) / 18000;
  return clamp16(mapped, 0, panelHeight - 1);
}

uint8_t tiltRotation() {
  if (abs16(accelX) > abs16(accelY)) {
    return accelX >= 0 ? 1 : 3;
  }
  return accelY >= 0 ? 2 : 0;
}

void addMotionBurst(int16_t cx, int16_t cy, uint8_t type, uint8_t hue,
                    uint8_t radius) {
  for (int8_t y = -radius; y <= radius; y++) {
    for (int8_t x = -radius; x <= radius; x++) {
      uint8_t distance = abs(x) + abs(y);
      if (distance <= radius && (distance <= 1 || (random32() & 1))) {
        setNextAliveHsv(cx + x, cy + y, type + distance,
                        wrapHue(hue + x * 8 + y * 5), 235);
      }
    }
  }
}

void addTiltStream() {
  if (tiltStrength < 45 || (generation & 1)) {
    return;
  }

  uint8_t extra = (tiltStrength - 40) >> 5;
  if (extra > 5) {
    extra = 5;
  }
  uint8_t count = 2 + extra;
  uint8_t type = motionType();
  uint8_t hue = motionHue(type);
  bool horizontal = abs16(accelX) > abs16(accelY);

  for (uint8_t i = 0; i < count; i++) {
    int16_t x = random32() % panelWidth;
    int16_t y = random32() % panelHeight;

    if (horizontal) {
      x = tiltDx > 0 ? 0 : panelWidth - 1;
    } else {
      y = tiltDy > 0 ? 0 : panelHeight - 1;
    }

    setNextAliveHsv(x, y, type, wrapHue(hue + i * 11), 230);
    setNextAliveHsv(x + tiltDx, y + tiltDy, type, wrapHue(hue + 18 + i * 9), 220);
    if (random32() & 1) {
      setNextDead(x - tiltDx, y - tiltDy);
    }
  }

  tiltEventsThisPeriod += count;
  interactionEventsThisPeriod += count;
}

void applyInteractionEvents() {
  if (!accelerometerReady) {
    return;
  }

  uint8_t type = motionType();
  uint8_t hue = motionHue(type);
  int16_t cx = tiltMappedX();
  int16_t cy = tiltMappedY();

  while (pendingShakes) {
    addVoid(panelWidth / 2, panelHeight / 2, 3);
    addMotionBurst(panelWidth / 2, panelHeight / 2, type, wrapHue(hue + 128), 5);
    for (uint8_t i = 0; i < 4; i++) {
      addGlider(random32() % panelWidth, random32() % panelHeight, type + i,
                random32());
    }
    pendingShakes--;
    interactionEventsThisPeriod += 5;
  }

  addTiltStream();

  if (tiltStrength > 135 && (generation & 15) == 0) {
    addMotionBurst(cx, cy, type, hue, 2);
    interactionEventsThisPeriod++;
  }
}

void applyRandomEvents(bool stagnant) {
  uint8_t events = 0;

  if (stagnant || (random32() & 15) == 0) {
    addGlider(random32() % panelWidth, random32() % panelHeight, randomType(), random32());
    events++;
  }

  if (stagnant || (random32() & 63) == 0) {
    addBurst(random32() % panelWidth, random32() % panelHeight, randomType());
    events++;
  }

  if ((random32() & 127) == 0) {
    addVoid(random32() % panelWidth, random32() % panelHeight, 1 + (random32() & 1));
    events++;
  }

  uint8_t jitter = stagnant ? 12 : 1;
  for (uint8_t i = 0; i < jitter; i++) {
    if (stagnant || (random32() & 31) == 0) {
      int16_t x = random32() % panelWidth;
      int16_t y = random32() % panelHeight;
      if (random32() & 3) {
        setNextAlive(x, y, randomType());
      } else {
        setNextDead(x, y);
      }
      events++;
    }
  }

  randomEventsThisPeriod += events;
}

void recountNextStats(uint16_t &nextLiveCells, uint16_t &nextChangedCells) {
  nextLiveCells = 0;
  nextChangedCells = 0;

  for (uint8_t y = 0; y < panelHeight; y++) {
    uint64_t row = nextRows[y] & activeMask;
    nextLiveCells += popcount64(row);
    nextChangedCells += popcount64((currentRows[y] ^ row) & activeMask);
  }
}

void commitNextGeneration() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    uint64_t previousRow = currentRows[y];
    uint64_t nextRow = nextRows[y] & activeMask;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint64_t bit = bitForX[x];
      uint16_t index = baseIndex + x;
      bool wasAlive = previousRow & bit;
      bool isAlive = nextRow & bit;

      if (isAlive) {
        if (wasAlive && cellAge[index] < 255) {
          cellAge[index]++;
        } else {
          cellAge[index] = 0;
          visualHue[index] = nextHue[index];
          visualSat[index] = 0;
          visualValue[index] = 0;
        }
        cellType[index] = nextType[index] % kTypeCount;
        cellHue[index] = nextHue[index];
        cellSat[index] = nextSat[index];
      } else if (wasAlive) {
        cellAge[index] = 0;
      }
    }

    currentRows[y] = nextRow;
  }
}

void stepLife() {
  if (pendingKnocks) {
    startBurnWave();
  }

  if (burnWaveActive) {
    stepBurnWave();
    return;
  }

  uint16_t nextLiveCells = 0;
  uint16_t nextChangedCells = 0;

  for (uint8_t y = 0; y < panelHeight; y++) {
    uint8_t aboveY = y == 0 ? panelHeight - 1 : y - 1;
    uint8_t belowY = y + 1 == panelHeight ? 0 : y + 1;
    uint64_t above = currentRows[aboveY];
    uint64_t row = currentRows[y];
    uint64_t below = currentRows[belowY];
    uint64_t nextRow = 0;
    uint16_t aboveBase = aboveY * kMaxWidth;
    uint16_t rowBase = y * kMaxWidth;
    uint16_t belowBase = belowY * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint8_t leftX = x == 0 ? panelWidth - 1 : x - 1;
      uint8_t rightX = x + 1 == panelWidth ? 0 : x + 1;
      uint64_t leftBit = leftBitForX[x];
      uint64_t bit = bitForX[x];
      uint64_t rightBit = rightBitForX[x];
      uint8_t neighbors =
          !!(above & leftBit) + !!(above & bit) + !!(above & rightBit) +
          !!(row & leftBit) + !!(row & rightBit) +
          !!(below & leftBit) + !!(below & bit) + !!(below & rightBit);

      if (neighbors == 3 || ((row & bit) && neighbors == 2)) {
        NeighborMix mix = {};
        if (above & leftBit) {
          addNeighbor(mix, aboveBase + leftX);
        }
        if (above & bit) {
          addNeighbor(mix, aboveBase + x);
        }
        if (above & rightBit) {
          addNeighbor(mix, aboveBase + rightX);
        }
        if (row & leftBit) {
          addNeighbor(mix, rowBase + leftX);
        }
        if (row & rightBit) {
          addNeighbor(mix, rowBase + rightX);
        }
        if (below & leftBit) {
          addNeighbor(mix, belowBase + leftX);
        }
        if (below & bit) {
          addNeighbor(mix, belowBase + x);
        }
        if (below & rightBit) {
          addNeighbor(mix, belowBase + rightX);
        }

        uint16_t index = rowBase + x;
        uint8_t type = row & bit ? cellType[index] : dominantType(mix.typeCounts, randomType());
        if ((row & bit) && (random32() & 15) == 0) {
          type = dominantType(mix.typeCounts, type);
        }
        uint8_t mixedNeighborHue = mixedHue(mix);
        uint8_t hue;
        uint8_t saturation;

        if (row & bit) {
          hue = blendHue(cellHue[index], mixedNeighborHue, 44);
          hue = blendHue(hue, speciesHues[type % kTypeCount], 20);
          saturation = approach(cellSat[index], mixedSaturation(mix), 7);
        } else {
          hue = blendHue(mixedNeighborHue, speciesHues[type % kTypeCount], 72);
          saturation = mixedSaturation(mix);
        }

        nextType[index] = maybeMutate(type);
        if (nextType[index] != type) {
          hue = blendHue(hue, speciesHues[nextType[index]], 120);
          saturation = addSaturated(saturation, 18);
        }
        nextHue[index] = mutateHue(hue);
        nextSat[index] = saturation < 175 ? 175 : saturation;
        nextRow |= bit;
      }
    }

    nextRows[y] = nextRow & activeMask;
  }

  recountNextStats(nextLiveCells, nextChangedCells);
  applyRandomEvents(nextChangedCells < 6 || nextLiveCells < kMinLiveCells);
  applyInteractionEvents();
  recountNextStats(nextLiveCells, nextChangedCells);

  if (nextLiveCells < kMinLiveCells) {
    seedLife();
    return;
  }

  commitNextGeneration();
  generation++;
  liveCells = nextLiveCells;
  changedCells = nextChangedCells;
  renderFrame();
}

void updateTiltState() {
  uint16_t planar = abs16(accelX) + abs16(accelY);
  tiltStrength = planar > 8160 ? 255 : planar >> 5;
  tiltDx = accelX > kTiltDeadzone ? 1 : (accelX < -kTiltDeadzone ? -1 : 0);
  tiltDy = accelY > kTiltDeadzone ? 1 : (accelY < -kTiltDeadzone ? -1 : 0);
  tiltHueBias = clamp16((accelX + accelY) >> 8, -48, 48);

  if (planar > kStrongTilt) {
    raiseMotionGlow(55 + (tiltStrength >> 2));
  } else if (planar > kTiltDeadzone * 2) {
    raiseMotionGlow(28 + (tiltStrength >> 3));
  }
}

void initAccelerometer() {
  Wire.begin();

  uint8_t address = 0;
  if (i2cResponds(kAccelAddressHigh)) {
    address = kAccelAddressHigh;
  } else if (i2cResponds(kAccelAddressLow)) {
    address = kAccelAddressLow;
  }

  accelerometerReady = address && accelerometer.begin(address);
  Serial.print("Accelerometer: ");
  if (!accelerometerReady) {
    Serial.println("not found");
    return;
  }

  accelerometer.setRange(LIS3DH_RANGE_4_G);
  accelerometer.setDataRate(LIS3DH_DATARATE_100_HZ);
  accelerometer.setClick(1, 45, 10, 20, 80);
  accelerometer.read();

  accelX = accelerometer.x;
  accelY = accelerometer.y;
  accelZ = accelerometer.z;
  lastAccelX = accelX;
  lastAccelY = accelY;
  lastAccelZ = accelZ;
  accelerometerPrimed = true;
  updateTiltState();

  Serial.print("LIS3DH @ 0x");
  Serial.println(address, HEX);
}

void pollAccelerometer() {
  if (!accelerometerReady) {
    return;
  }

  uint32_t now = millis();
  if (now - lastAccelReadAt < kAccelPollMs) {
    return;
  }
  lastAccelReadAt = now;

  accelerometer.read();
  int16_t rawX = accelerometer.x;
  int16_t rawY = accelerometer.y;
  int16_t rawZ = accelerometer.z;

  if (!accelerometerPrimed) {
    accelX = rawX;
    accelY = rawY;
    accelZ = rawZ;
    lastAccelX = rawX;
    lastAccelY = rawY;
    lastAccelZ = rawZ;
    accelerometerPrimed = true;
  } else {
    accelX += (rawX - accelX) / 4;
    accelY += (rawY - accelY) / 4;
    accelZ += (rawZ - accelZ) / 4;
  }

  uint32_t delta = static_cast<uint32_t>(absDiff16(rawX, lastAccelX)) +
                   absDiff16(rawY, lastAccelY) + absDiff16(rawZ, lastAccelZ);
  lastAccelX = rawX;
  lastAccelY = rawY;
  lastAccelZ = rawZ;
  updateTiltState();

  uint8_t click = accelerometer.getClick();
  if ((click & 0x30) && now - lastKnockAt > 120) {
    uint8_t knocks = (click & 0x20) ? 2 : 1;
    pendingKnocks = pendingKnocks + knocks > 8 ? 8 : pendingKnocks + knocks;
    knockEventsThisPeriod += knocks;
    lastKnockAt = now;
    raiseMotionGlow(knocks == 2 ? 190 : 145);
  }

  if (delta > kShakeDelta && now - lastShakeAt > 300) {
    if (pendingShakes < 4) {
      pendingShakes++;
    }
    shakeEventsThisPeriod++;
    lastShakeAt = now;
    raiseMotionGlow(220);
  }
}

void decayMotionEffects() {
  if (motionGlow > 5) {
    motionGlow -= 5;
  } else {
    motionGlow = 0;
  }
}

void reportFps() {
  framesThisPeriod++;
  uint32_t now = millis();
  uint32_t elapsed = now - fpsStartedAt;
  if (elapsed < 1000) {
    return;
  }

  uint32_t refreshCount = matrix.getFrameCount();
  uint32_t refreshFps = (refreshCount * 1000UL) / elapsed;
  uint32_t lifeFps = (framesThisPeriod * 1000UL) / elapsed;

  Serial.print("Life FPS: ");
  Serial.print(lifeFps);
  Serial.print(" | Refresh FPS: ");
  Serial.print(refreshFps);
  Serial.print(" | live: ");
  Serial.print(liveCells);
  Serial.print(" | changed: ");
  Serial.print(changedCells);
  Serial.print(" | pixels: ");
  Serial.print(updatedPixels);
  Serial.print(" | events: ");
  Serial.print(randomEventsThisPeriod);
  Serial.print(" | motion: ");
  Serial.print(interactionEventsThisPeriod);
  Serial.print(" | knocks: ");
  Serial.print(knockEventsThisPeriod);
  Serial.print(" | burns: ");
  Serial.print(burnEventsThisPeriod);
  Serial.print(" | shakes: ");
  Serial.print(shakeEventsThisPeriod);
  Serial.print(" | tilt: ");
  Serial.print(tiltStrength);
  Serial.print(" | gen: ");
  Serial.print(generation);
  Serial.print(" | avg us loop/step/sim/render/show/accel: ");
  Serial.print(averageProfile(profileLoopMicros, profileSamples));
  Serial.print('/');
  Serial.print(averageProfile(profileLifeMicros, profileSamples));
  Serial.print('/');
  Serial.print(averageProfile(profiledSimulationMicros(), profileSamples));
  Serial.print('/');
  Serial.print(averageProfile(profileRenderMicros, profileRenderSamples));
  Serial.print('/');
  Serial.print(averageProfile(profileShowMicros, profileRenderSamples));
  Serial.print('/');
  Serial.print(averageProfile(profileAccelMicros, profileSamples));
  Serial.print(" | max us loop/life/render/show: ");
  Serial.print(profileLoopMaxMicros);
  Serial.print('/');
  Serial.print(profileLifeMaxMicros);
  Serial.print('/');
  Serial.print(profileRenderMaxMicros);
  Serial.print('/');
  Serial.println(profileShowMaxMicros);

  fpsStartedAt = now;
  framesThisPeriod = 0;
  randomEventsThisPeriod = 0;
  interactionEventsThisPeriod = 0;
  knockEventsThisPeriod = 0;
  burnEventsThisPeriod = 0;
  shakeEventsThisPeriod = 0;
  tiltEventsThisPeriod = 0;
  resetProfileCounters();
}

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
  initAccelerometer();
  matrix.fillScreen(0);
  matrix.show();
  seedLife();
  resetProfileCounters();
  fpsStartedAt = millis();
  framesThisPeriod = 0;
  randomEventsThisPeriod = 0;
  interactionEventsThisPeriod = 0;
  knockEventsThisPeriod = 0;
  burnEventsThisPeriod = 0;
  shakeEventsThisPeriod = 0;
  tiltEventsThisPeriod = 0;
  matrix.getFrameCount();

  Serial.print("Game of Life: ");
  Serial.print(panelWidth);
  Serial.print('x');
  Serial.println(panelHeight);
}

void loop() {
  uint32_t loopStartedAt = micros();
  uint32_t accelStartedAt = loopStartedAt;
  pollAccelerometer();
  uint32_t lifeStartedAt = micros();
  stepLife();
  uint32_t lifeEndedAt = micros();
  decayMotionEffects();
  uint32_t loopEndedAt = micros();

  profileAccelMicros += lifeStartedAt - accelStartedAt;
  addProfile(profileLifeMicros, profileLifeMaxMicros, lifeEndedAt - lifeStartedAt);
  addProfile(profileLoopMicros, profileLoopMaxMicros, loopEndedAt - loopStartedAt);
  profileSamples++;
  reportFps();
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
