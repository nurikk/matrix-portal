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

uint8_t approachEased(uint8_t current, uint8_t target, uint8_t step) {
  uint8_t easedStep = (absDiff16(current, target) + 3) / 4;
  return approach(current, target, easedStep < step ? easedStep : step);
}

uint8_t clampAuroraHue(int16_t hue) {
  if (hue < kAuroraHueMin) return kAuroraHueMin;
  if (hue > kAuroraHueMax) return kAuroraHueMax;
  return static_cast<uint8_t>(hue);
}

uint8_t relatedHue(uint8_t type) {
  uint8_t hue = speciesHues[type % kTypeCount];
  if ((random32() & 31) < 18) {
    return clampAuroraHue(hue + static_cast<int16_t>((random32() % 17) - 8));
  }
  return hue;
}

uint8_t mutateHue(uint8_t hue) {
  if ((random32() & 255) < 18) {
    hue = clampAuroraHue(hue + static_cast<int16_t>((random32() % 17) - 8));
  }
  return clampAuroraHue(hue);
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

Hsv hsvFrom565(uint16_t color) {
  uint8_t r = ((color >> 11) & 31) * 255 / 31;
  uint8_t g = ((color >> 5) & 63) * 255 / 63;
  uint8_t b = (color & 31) * 255 / 31;
  uint8_t value = r > g ? r : g;
  if (b > value) value = b;
  uint8_t minimum = r < g ? r : g;
  if (b < minimum) minimum = b;
  uint8_t chroma = value - minimum;
  if (chroma == 0) return {0, 0, value};
  int16_t hue;
  if (value == r) {
    hue = ((static_cast<int16_t>(g) - b) * 256) / (6 * chroma);
  } else if (value == g) {
    hue = 85 + ((static_cast<int16_t>(b) - r) * 256) / (6 * chroma);
  } else {
    hue = 171 + ((static_cast<int16_t>(r) - g) * 256) / (6 * chroma);
  }
  return {wrapHue(hue), static_cast<uint8_t>((chroma * 255U) / value), value};
}

uint16_t blendColor565(uint16_t from, uint16_t to, uint8_t amount) {
  uint8_t r = (((from >> 11) * (255 - amount)) + ((to >> 11) * amount) + 127) / 255;
  uint8_t g = ((((from >> 5) & 63) * (255 - amount)) + (((to >> 5) & 63) * amount) + 127) / 255;
  uint8_t b = (((from & 31) * (255 - amount)) + ((to & 31) * amount) + 127) / 255;
  return (static_cast<uint16_t>(r) << 11) | (static_cast<uint16_t>(g) << 5) | b;
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
