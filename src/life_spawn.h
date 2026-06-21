#pragma once
// life_spawn.h — pattern spawning (gliders/bursts/voids) and interaction/random
// event application onto the next-generation buffers.
// Included once by main.cpp after life_input.h. Not a standalone TU.

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
