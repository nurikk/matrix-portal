#pragma once
// life_profile.h — FPS / timing accumulators and the once-per-second serial report.
// Included once by main.cpp after life_util.h (before life_render.h: renderFrame calls
// addProfile). Not a standalone TU.

void addProfile(uint32_t &total, uint32_t &maximum, uint32_t elapsed) {
  total += elapsed;
  if (elapsed > maximum) {
    maximum = elapsed;
  }
}

uint32_t averageProfile(uint32_t total, uint16_t samples) {
  return samples ? total / samples : 0;
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

void reportFps() {
  uint32_t now = millis();
  uint32_t elapsed = now - fpsStartedAt;
  if (elapsed < 1000) {
    return;
  }

  uint32_t refreshCount = matrix.getFrameCount();
  uint32_t refreshFps = (refreshCount * 1000UL) / elapsed;
  uint32_t renderFps = (framesThisPeriod * 1000UL) / elapsed;
  uint32_t lifeUps = (lifeStepsThisPeriod * 1000UL) / elapsed;

  Serial.print("Life UPS: ");
  Serial.print(lifeUps);
  Serial.print(" | Render FPS: ");
  Serial.print(renderFps);
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
  Serial.print(" | knocks: ");
  Serial.print(knockEventsThisPeriod);
  Serial.print(" | gen: ");
  Serial.print(generation);
  Serial.print(" | avg us loop/life/render/show/accel: ");
  Serial.print(averageProfile(profileLoopMicros, profileSamples));
  Serial.print('/');
  Serial.print(averageProfile(profileLifeMicros, lifeStepsThisPeriod));
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

#if WIFI_PORTAL_ENABLED
  gStatRenderFps = (uint16_t)renderFps;
  gStatLifeUps = (uint16_t)lifeUps;
#endif
  fpsStartedAt = now;
  framesThisPeriod = 0;
  lifeStepsThisPeriod = 0;
  randomEventsThisPeriod = 0;
  knockEventsThisPeriod = 0;
  resetProfileCounters();
}
