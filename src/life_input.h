#pragma once
// life_input.h — accelerometer read, knock detection, knock/burn glow decay.
// Included once by main.cpp after life_burn.h. Not a standalone TU.

bool i2cResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
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

  lastAccelX = accelerometer.x;
  lastAccelY = accelerometer.y;
  accelerometerPrimed = true;

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
  int16_t knockImpulseX = accelerometerPrimed ? rawX - lastAccelX : 0;
  int16_t knockImpulseY = accelerometerPrimed ? rawY - lastAccelY : 0;
  lastAccelX = rawX;
  lastAccelY = rawY;
  accelerometerPrimed = true;

  uint8_t click = accelerometer.getClick();
  if ((click & kClickEventMask) && now - lastKnockAt > 120) {
    uint8_t knocks = (click & kClickDouble) ? 2 : 1;
    recordKnockOrigin(click, knockImpulseX, knockImpulseY);
    pendingKnocks = pendingKnocks + knocks > 8 ? 8 : pendingKnocks + knocks;
    knockEventsThisPeriod += knocks;
    lastKnockAt = now;
    raiseMotionGlow(knocks == 2 ? 190 : 145);
  }
}

void decayMotionEffects() {
  if (motionGlow > gLive.motionGlowFadeStep) {
    motionGlow -= gLive.motionGlowFadeStep;
  } else {
    motionGlow = 0;
  }
}
