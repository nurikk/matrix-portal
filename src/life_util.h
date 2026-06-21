#pragma once
// life_util.h — low-level numeric / RNG / motion-glow helpers.
// Included once by main.cpp after life_state.h. Not a standalone TU.

uint32_t random32() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

uint8_t popcount64(uint64_t value) {
  return __builtin_popcountll(value);
}

uint8_t popcount64(RowBits value) {
  return __builtin_popcountll(value.low) + __builtin_popcountll(value.high);
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
