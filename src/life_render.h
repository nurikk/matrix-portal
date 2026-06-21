#pragma once
// life_render.h — per-cell target color + the dirty-draw frame renderer.
// Included once by main.cpp after life_color.h. Calls addProfile (life_profile.h).
// Not a standalone TU.

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
