#pragma once
// life_color.h — hue math, neighbor color mixing, and RGB565 conversion.
// Included once by main.cpp after life_profile.h. Not a standalone TU.

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
