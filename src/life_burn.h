#pragma once
// life_burn.h — the expanding burn-wave state machine.
// Included once by main.cpp after life_render.h. Uses seedLife (fwd-declared in
// life_state.h) and raiseMotionGlow/clamp16 (life_util.h). Not a standalone TU.

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
