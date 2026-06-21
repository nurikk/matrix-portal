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
  uint8_t hue = wrapHue(cellHue[index] +
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
      uint8_t nextHueValue, nextSatValue, nextValue;
      if (gLive.noFade) {
        // Classic Game of Life: snap straight to the target each frame, no per-frame fade.
        nextHueValue = target.h;
        nextSatValue = target.s;
        nextValue = target.v;
      } else {
        nextHueValue = approachHue(visualHue[index], target.h, gLive.hueStep);
        nextSatValue = approach(visualSat[index], target.s, gLive.satStep);
        uint8_t valueStep = burnHeat[index] ? gLive.liveValueStep
                                            : (alive ? gLive.liveValueStep : gLive.deathValueStep);
        nextValue = approach(visualValue[index], target.v, valueStep);
      }

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

#if WIFI_PORTAL_ENABLED
// Copy the active panel's drawn RGB565 image into dst, tightly packed row-major:
// dst[y * panelWidth + x]. dst must hold at least panelWidth*panelHeight uint16_t.
// drawnColor is strided by the compile-time kMaxWidth (panelWidth may be narrower),
// so the copy can't be a single memcpy -- it walks row by row.
//
// Called from the core-0 web push task while core 1 may be mid-renderFrame() writing
// drawnColor. This follows the same philosophy as the lock-free stats reads in
// web_portal.cpp, but is broader in scope: stats are one stale scalar, whereas this whole
// 32KB snapshot can blend two generations. That's fine here — each entry is a 16-bit
// aligned load (atomic on Xtensa) so no individual pixel tears, and a frame that mixes
// generations is cosmetically harmless and superseded by the next push ~100 ms later.
// Do NOT add volatile or locks (same project rule as the stats race).
void copyDrawnFrame(uint16_t *dst) {
  for (uint8_t y = 0; y < panelHeight; y++) {
    const uint16_t *srcRow = &drawnColor[(uint16_t)y * kMaxWidth];
    uint16_t *dstRow = &dst[(uint16_t)y * panelWidth];
    for (uint8_t x = 0; x < panelWidth; x++) {
      dstRow[x] = srcRow[x];
    }
  }
}
#endif
