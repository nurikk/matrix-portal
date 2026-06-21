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

void configureLifeBounds() {
  int16_t width = matrix.width();
  int16_t height = matrix.height();
  panelWidth = width < kMaxWidth ? width : kMaxWidth;
  panelHeight = height < kMaxHeight ? height : kMaxHeight;
  activeMask = activeMaskFor(panelWidth);
  burnCenterX = pendingBurnCenterX = panelWidth / 2;
  burnCenterY = pendingBurnCenterY = panelHeight / 2;

  for (uint8_t x = 0; x < panelWidth; x++) {
    uint8_t bitX = x & 63;
    uint8_t leftX = (x == 0 ? panelWidth - 1 : x - 1) & 63;
    uint8_t rightX = (x + 1 == panelWidth ? 0 : x + 1) & 63;
    bitForX[x] = x < 64 ? RowBits(1ULL << bitX, 0) : RowBits(0, 1ULL << bitX);
    leftBitForX[x] = (x == 0 ? panelWidth - 1 : x - 1) < 64
                         ? RowBits(1ULL << leftX, 0)
                         : RowBits(0, 1ULL << leftX);
    rightBitForX[x] = (x + 1 == panelWidth ? 0 : x + 1) < 64
                          ? RowBits(1ULL << rightX, 0)
                          : RowBits(0, 1ULL << rightX);
  }
}

int16_t tiltMappedX();
int16_t tiltMappedY();

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

  uint8_t wave = triWave6(generation * 2 + x * 3 + y * 5 + cellType[index] * 11);
  uint8_t shimmer = triWave6(generation + x * 4 + y * 2);
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
    RowBits row = currentRows[y] & activeMask;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = baseIndex + x;
      bool alive = row & bitForX[x];

      // Settled black cells (dead, fully faded, no heat, not forced) are
      // already correct and unchanging -- skip the per-cell colour work.
      if (!alive && visualValue[index] == 0 && burnHeat[index] == 0 &&
          !forceRedraw[index]) {
        continue;
      }

      Hsv target = targetColorFor(index, x, y, alive);
      bool force = forceRedraw[index];
      forceRedraw[index] = false;
      uint8_t nextHueValue = approachHue(visualHue[index], target.h, gLive.hueStep);
      uint8_t nextSatValue = approach(visualSat[index], target.s, gLive.satStep);
      uint8_t valueStep = burnHeat[index] ? gLive.liveValueStep
                                          : (alive ? gLive.liveValueStep : gLive.deathValueStep);
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
  framesThisPeriod++;
}

void clearBurnHeat() {
  for (uint16_t index = 0; index < kCellCount; index++) {
    burnHeat[index] = 0;
  }
}

void recordKnockOrigin(uint8_t click, int16_t impulseX, int16_t impulseY) {
  int16_t clickSign = (click & kClickSignNegative) ? -1 : 1;

  if (click & kClickAxisX) {
    uint16_t magnitude = abs16(impulseX);
    if (magnitude < gLive.knockAxisMinimumImpulse) {
      magnitude = gLive.knockAxisMinimumImpulse;
    } else if (magnitude > gLive.knockImpulseFullScale) {
      magnitude = gLive.knockImpulseFullScale;
    }
    impulseX = clickSign * static_cast<int16_t>(magnitude);
  }

  if (click & kClickAxisY) {
    uint16_t magnitude = abs16(impulseY);
    if (magnitude < gLive.knockAxisMinimumImpulse) {
      magnitude = gLive.knockAxisMinimumImpulse;
    } else if (magnitude > gLive.knockImpulseFullScale) {
      magnitude = gLive.knockImpulseFullScale;
    }
    impulseY = clickSign * static_cast<int16_t>(magnitude);
  }

  if (!(click & (kClickAxisX | kClickAxisY)) &&
      abs16(impulseX) + abs16(impulseY) < gLive.knockAxisMinimumImpulse) {
    impulseX = static_cast<int16_t>(random32() % (gLive.knockAxisMinimumImpulse + 1)) -
               (gLive.knockAxisMinimumImpulse / 2);
    impulseY = static_cast<int16_t>(random32() % (gLive.knockAxisMinimumImpulse + 1)) -
               (gLive.knockAxisMinimumImpulse / 2);
  }

  int16_t x = panelWidth / 2 -
              (static_cast<int32_t>(impulseX) * panelWidth) /
                  gLive.knockImpulseFullScale;
  int16_t y = panelHeight / 2 -
              (static_cast<int32_t>(impulseY) * panelHeight) /
                  gLive.knockImpulseFullScale;
  pendingBurnCenterX = clamp16(x, 0, panelWidth - 1);
  pendingBurnCenterY = clamp16(y, 0, panelHeight - 1);
}

void startBurnWave() {
  pendingKnocks = 0;
  if (burnWaveActive) {
    return;
  }

  burnWaveActive = true;
  burnRadius = 0;
  pendingShakes = 0;
  burnCenterX = pendingBurnCenterX;
  burnCenterY = pendingBurnCenterY;
  int16_t centerX = burnCenterX;
  int16_t centerY = burnCenterY;
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
  burnEndRadius += gLive.burnRingWidth + 8;
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
  int16_t centerX = burnCenterX;
  int16_t centerY = burnCenterY;
  uint8_t innerRadius = burnRadius > gLive.burnRingWidth ? burnRadius - gLive.burnRingWidth : 0;
  uint8_t outerRadius = burnRadius + gLive.burnRingWidth;
  uint16_t killRadiusSquared = static_cast<uint16_t>(burnRadius) * burnRadius;
  uint16_t innerSquared = static_cast<uint16_t>(innerRadius) * innerRadius;
  uint16_t outerSquared = static_cast<uint16_t>(outerRadius) * outerRadius;

  for (uint8_t y = 0; y < panelHeight; y++) {
    RowBits row = currentRows[y] & activeMask;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = baseIndex + x;
      uint8_t heat = burnHeat[index];
      if (heat > gLive.burnFadeStep) {
        heat -= gLive.burnFadeStep;
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
    RowBits row = 0;
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
    RowBits row = nextRows[y] & activeMask;
    nextLiveCells += popcount64(row);
    nextChangedCells += popcount64((currentRows[y] ^ row) & activeMask);
  }
}

uint8_t wrappedOffset(uint8_t value, int8_t offset, uint8_t limit) {
  int16_t wrapped = static_cast<int16_t>(value) + offset;
  if (wrapped < 0) {
    return wrapped + limit;
  }
  if (wrapped >= limit) {
    return wrapped - limit;
  }
  return wrapped;
}

uint8_t localChunkMass(uint8_t x, uint8_t y) {
  uint8_t mass = 0;

  for (int8_t dy = -2; dy <= 2; dy++) {
    uint8_t yy = wrappedOffset(y, dy, panelHeight);
    RowBits row = currentRows[yy] & activeMask;

    for (int8_t dx = -2; dx <= 2; dx++) {
      uint8_t xx = wrappedOffset(x, dx, panelWidth);
      if (row & bitForX[xx]) {
        mass++;
      }
    }
  }

  return mass;
}

uint8_t chunkBirthCadence(uint8_t mass) {
  if (mass >= gLive.hugeChunkMass) {
    return 4;
  }
  if (mass >= gLive.largeChunkMass) {
    return 3;
  }
  if (mass >= gLive.mediumChunkMass) {
    return 2;
  }
  return 1;
}

bool denseBirthAllowed(uint8_t x, uint8_t y) {
  uint8_t cadence = chunkBirthCadence(localChunkMass(x, y));
  return cadence == 1 || (generation + x * 3 + y * 5) % cadence == 0;
}

void commitNextGeneration() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    RowBits previousRow = currentRows[y];
    RowBits nextRow = nextRows[y] & activeMask;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      RowBits bit = bitForX[x];
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
  lifeStepsThisPeriod++;

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
    RowBits above = currentRows[aboveY];
    RowBits row = currentRows[y];
    RowBits below = currentRows[belowY];
    // Bit-parallel Conway step: one candidate mask for the whole row
    // (births on 3, survivors on 2/3) instead of counting 8 neighbours per
    // cell. See life_bits.h / tests/test_life_bits.cpp.
    RowBits candidates = conwayNextRow(above & activeMask, row & activeMask,
                                       below & activeMask, panelWidth, activeMask);
    RowBits nextRow = 0;
    uint16_t aboveBase = aboveY * kMaxWidth;
    uint16_t rowBase = y * kMaxWidth;
    uint16_t belowBase = belowY * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      RowBits bit = bitForX[x];
      if (!(candidates & bit)) {
        continue;
      }

      bool alive = row & bit;
      if (!alive && !denseBirthAllowed(x, y)) {
        continue;
      }

      uint8_t leftX = x == 0 ? panelWidth - 1 : x - 1;
      uint8_t rightX = x + 1 == panelWidth ? 0 : x + 1;
      RowBits leftBit = leftBitForX[x];
      RowBits rightBit = rightBitForX[x];

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
      uint8_t type = alive ? cellType[index] : dominantType(mix.typeCounts, randomType());
      if (alive && (random32() & 15) == 0) {
        type = dominantType(mix.typeCounts, type);
      }
      uint8_t mixedNeighborHue = mixedHue(mix);
      uint8_t hue;
      uint8_t saturation;

      if (alive) {
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

    nextRows[y] = nextRow & activeMask;
  }
  recountNextStats(nextLiveCells, nextChangedCells);
  applyRandomEvents(nextChangedCells < 6 || nextLiveCells < gLive.minLiveCells);
  applyInteractionEvents();
  recountNextStats(nextLiveCells, nextChangedCells);

  if (nextLiveCells < gLive.minLiveCells) {
    seedLife();
    return;
  }

  commitNextGeneration();
  generation++;
  liveCells = nextLiveCells;
  changedCells = nextChangedCells;
}

void updateTiltState() {
  uint16_t planar = abs16(accelX) + abs16(accelY);
  tiltStrength = planar > 8160 ? 255 : planar >> 5;
  tiltDx = accelX > gLive.tiltDeadzone ? 1 : (accelX < -gLive.tiltDeadzone ? -1 : 0);
  tiltDy = accelY > gLive.tiltDeadzone ? 1 : (accelY < -gLive.tiltDeadzone ? -1 : 0);
  tiltHueBias = clamp16((accelX + accelY) >> 8, -48, 48);

  if (planar > gLive.strongTilt) {
    raiseMotionGlow(55 + (tiltStrength >> 2));
  } else if (planar > gLive.tiltDeadzone * 2) {
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
  if (now - lastAccelReadAt < gLive.accelPollMs) {
    return;
  }
  lastAccelReadAt = now;

  accelerometer.read();
  int16_t rawX = accelerometer.x;
  int16_t rawY = accelerometer.y;
  int16_t rawZ = accelerometer.z;
  int16_t knockImpulseX = accelerometerPrimed ? rawX - lastAccelX : 0;
  int16_t knockImpulseY = accelerometerPrimed ? rawY - lastAccelY : 0;

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
  if ((click & kClickEventMask) && now - lastKnockAt > 120) {
    uint8_t knocks = (click & kClickDouble) ? 2 : 1;
    recordKnockOrigin(click, knockImpulseX, knockImpulseY);
    pendingKnocks = pendingKnocks + knocks > 8 ? 8 : pendingKnocks + knocks;
    knockEventsThisPeriod += knocks;
    lastKnockAt = now;
    raiseMotionGlow(knocks == 2 ? 190 : 145);
  }

  if (delta > gLive.shakeDelta && now - lastShakeAt > 300) {
    if (pendingShakes < 4) {
      pendingShakes++;
    }
    shakeEventsThisPeriod++;
    lastShakeAt = now;
    raiseMotionGlow(220);
  }
}

void decayMotionEffects() {
  if (motionGlow > gLive.motionGlowFadeStep) {
    motionGlow -= gLive.motionGlowFadeStep;
  } else {
    motionGlow = 0;
  }
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
