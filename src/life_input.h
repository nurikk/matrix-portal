#pragma once
// life_input.h — accelerometer read, knock-triggered clock requests, and motion glow decay.
// Included once by main.cpp after life_clock.h. Not a standalone TU.

bool i2cResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
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
  uint8_t click = accelerometer.getClick();
  if ((click & kClickEventMask) && now - lastKnockAt > 120) {
    uint8_t knocks = (click & kClickDouble) ? 2 : 1;
#if WIFI_PORTAL_ENABLED
    if (!gReqClockAnimation) {
      gReqClockAnimation = kClockAnimationRequestKnockHour;
    }
#endif
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
