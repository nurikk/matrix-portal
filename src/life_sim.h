#pragma once
// life_sim.h — board setup, seeding, density throttling, generation commit, and the
// main bit-parallel Conway step (stepLife). Included once by main.cpp after
// life_spawn.h, last of the Life headers. Not a standalone TU.

void configureLifeBounds() {
  int16_t width = matrix.width();
  int16_t height = matrix.height();
  panelWidth = width < kMaxWidth ? width : kMaxWidth;
  panelHeight = height < kMaxHeight ? height : kMaxHeight;
  activeMask = activeMaskFor(panelWidth);

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

#if WIFI_PORTAL_ENABLED
// Inject browser-drawn cells into the current generation. mask is a tight row-major
// bitmask packed by w (bitIndex = y*w + x, LSB-first). Newly-live cells get living-cell
// metadata and start at cellAge=6 so they render at full saturation immediately: drawing is
// typically done while the sim is paused, where stepLife() never advances cellAge, so the
// age<6 "bloom" desaturation in targetColorFor would otherwise leave them washed-out white.
// Called only from webPortalTick() on core 1 (owns the cell arrays). The frame is already
// clipped to the panel (x<w, y<h), so this only sets in-bounds cells; the toroidal
// neighbour wrap the sim relies on lives in bitForX and is unaffected.
void applyDrawnCells(const uint8_t *mask, uint8_t w, uint8_t h) {
  for (uint8_t y = 0; y < h; y++) {
    uint16_t bitBase = (uint16_t)y * w;        // mask stride: packed tight by w
    uint16_t cellBase = (uint16_t)y * kMaxWidth;  // metadata stride: kMaxWidth

    for (uint8_t x = 0; x < w; x++) {
      uint16_t bitIndex = bitBase + x;
      if (!(mask[bitIndex >> 3] & (1u << (bitIndex & 7)))) {
        continue;
      }
      RowBits bit = bitForX[x];
      if (currentRows[y] & bit) {
        continue;   // already alive — don't double-count liveCells
      }

      currentRows[y] |= bit;
      uint16_t index = cellBase + x;
      cellType[index] = randomType();
      cellHue[index] = relatedHue(cellType[index]);
      cellSat[index] = 205 + (random32() & 31);
      cellAge[index] = 6;   // skip the age<6 bloom desaturation so the cell is coloured even while paused
      forceRedraw[index] = true;
      liveCells++;
    }
  }
}

// Wipe the board to empty and black. Clear-all also stops the sim (gPaused set here on
// core 1) so the cleared board persists instead of being instantly refilled by the
// minLiveCells auto-reseed in stepLife(). Called only from the core-1 loop().
void clearBoard() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    currentRows[y] = 0;
    nextRows[y] = 0;
  }
  for (uint16_t i = 0; i < kCellCount; i++) {
    cellAge[i] = 0;
    visualHue[i] = 0;
    visualSat[i] = 0;
    visualValue[i] = 0;
    forceRedraw[i] = true;   // force renderFrame to repaint each cell black this frame
  }
  liveCells = 0;
  changedCells = 0;
  generation = 0;
  gPaused = true;            // Clear-all freezes the sim so the empty board stays empty
}
#endif

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

bool denseBirthAllowed(uint8_t x, uint8_t y) {
  // Classic mode is unmodified Conway: never throttle, and skip the 5x5 mass
  // scan for every legal birth. The same bypass lives in throttledBirthAllowed
  // (pure, host-tested in life_settings.h), which owns the cadence policy.
  if (gLive.disableReseed) {
    return true;
  }
  return throttledBirthAllowed(gLive, generation, x, y, localChunkMass(x, y));
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
  // disableReseed = classic mode: no automatic life injection, so the board can die out.
  if (!gLive.disableReseed) {
    applyRandomEvents(nextChangedCells < 6 || nextLiveCells < gLive.minLiveCells);
  }
  recountNextStats(nextLiveCells, nextChangedCells);

  if (!gLive.disableReseed && nextLiveCells < gLive.minLiveCells) {
    seedLife();
    return;
  }

  commitNextGeneration();
  generation++;
  liveCells = nextLiveCells;
  changedCells = nextChangedCells;
}
