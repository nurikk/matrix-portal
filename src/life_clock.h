#pragma once
// life_clock.h -- scheduled wall-clock animations that temporarily replace Life rendering.
// Included once by main.cpp after life_render.h. Not a standalone TU.

enum ClockAnimationKind : uint8_t {
  kClockAnimationNone = 0,
  kClockAnimationMinute,
  kClockAnimationHour,
};

struct ClockAnimationState {
  ClockAnimationKind kind;
  uint8_t hour;
  uint8_t minute;
  uint32_t eventMinuteId;
  uint32_t startedAt;
  uint32_t endsAt;
  uint16_t durationMs;
  bool active;
  bool finalRendered;
};

#if WIFI_PORTAL_ENABLED
#include <time.h>

constexpr uint16_t kClockMinuteAnimationMs = 2000;
constexpr uint16_t kClockHourAnimationMs = 12000;
constexpr uint16_t kClockPostAnimationHoldMs = 5000;
constexpr uint8_t kClockMinuteScheduleInterval = 5;
constexpr time_t kClockValidEpoch = 1609459200;   // before 2021 means SNTP has not synced yet

ClockAnimationState gClockAnimation = {};
uint32_t gClockLastEpochSecond = 0;
uint32_t gClockSecondStartedAt = 0;
uint32_t gClockLastScheduledMinuteId = 0;
bool gClockHaveSecondAnchor = false;
bool gClockSecondAnchorSynced = false;

const uint8_t kClockDigitRows[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},
    {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111},
    {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001},
    {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111},
    {0b111, 0b001, 0b010, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111},
    {0b111, 0b101, 0b111, 0b001, 0b111},
};

const int8_t kClockSin12[12] = {0, 64, 110, 127, 110, 64, 0, -64, -110, -127, -110, -64};
const int8_t kClockCos12[12] = {127, 110, 64, 0, -64, -110, -127, -110, -64, 0, 64, 110};

bool clockFacePixel(ClockAnimationKind kind, uint8_t hour, uint8_t minute,
                    uint8_t x, uint8_t y, Hsv &hsv);
void precomputeClockFace();

uint16_t clockMinDimension() {
  return panelWidth < panelHeight ? panelWidth : panelHeight;
}

uint8_t clockSmoothstep8(uint8_t t) {
  uint32_t x = t;
  return static_cast<uint8_t>((x * x * (765 - 2 * x)) / 65025);
}

uint8_t clockClamp8(uint16_t value) {
  return value > 255 ? 255 : value;
}

uint8_t clockAbsDiff8(uint8_t a, uint8_t b) {
  return a > b ? a - b : b - a;
}

uint8_t clockDurationMsFor(ClockAnimationKind kind) {
  switch (kind) {
  case kClockAnimationHour:
    return kClockHourAnimationMs / 1000;
  case kClockAnimationMinute:
    return kClockMinuteAnimationMs / 1000;
  default:
    return 0;
  }
}

uint16_t clockDurationMillisFor(ClockAnimationKind kind) {
  switch (kind) {
  case kClockAnimationHour:
    return kClockHourAnimationMs;
  case kClockAnimationMinute:
    return kClockMinuteAnimationMs;
  default:
    return 0;
  }
}

ClockAnimationKind clockKindForEvent(const struct tm &local) {
  if (local.tm_min == 0) {
    return kClockAnimationHour;
  }
  if ((local.tm_min % kClockMinuteScheduleInterval) == 0) {
    return kClockAnimationMinute;
  }
  return kClockAnimationNone;
}

bool clockReadLocal(struct tm &local, time_t &epoch) {
  epoch = time(nullptr);
  if (epoch < kClockValidEpoch) {
    return false;
  }
  localtime_r(&epoch, &local);
  return true;
}

bool beginClockAnimation(ClockAnimationKind kind, uint8_t hour, uint8_t minute,
                         uint32_t eventMinuteId, uint32_t startedAt, uint32_t endsAt) {
  if (gClockAnimation.active) {
    return false;
  }

  uint16_t durationMs = clockDurationMillisFor(kind);
  if (durationMs == 0) {
    return false;
  }

  gClockAnimation.kind = kind;
  gClockAnimation.hour = hour;
  gClockAnimation.minute = minute;
  gClockAnimation.eventMinuteId = eventMinuteId;
  gClockAnimation.durationMs = durationMs;
  gClockAnimation.startedAt = startedAt;
  gClockAnimation.endsAt = endsAt;
  gClockAnimation.active = true;
  gClockAnimation.finalRendered = false;
  precomputeClockFace();
  return true;
}

ClockAnimationKind clockKindForRequest(uint8_t request) {
  switch (request) {
  case kClockAnimationRequestMinute:
    return kClockAnimationMinute;
  case kClockAnimationRequestHour:
    return kClockAnimationHour;
  default:
    return kClockAnimationNone;
  }
}

bool startClockAnimationRequest(uint8_t request, uint32_t nowMs) {
  ClockAnimationKind kind = clockKindForRequest(request);
  if (kind == kClockAnimationNone) {
    return false;
  }

  struct tm local;
  time_t epoch;
  uint8_t hour = 12;
  uint8_t minute = 34;
  uint32_t eventMinuteId = (nowMs / 60000UL) ^ (0x80000000UL | request);
  if (clockReadLocal(local, epoch)) {
    hour = static_cast<uint8_t>(local.tm_hour);
    minute = static_cast<uint8_t>(local.tm_min);
    eventMinuteId = static_cast<uint32_t>(epoch / 60) ^ (0x80000000UL | request);
  } else {
    uint16_t uptimeMinute = static_cast<uint16_t>((nowMs / 60000UL) % (24 * 60));
    hour = uptimeMinute / 60;
    minute = uptimeMinute % 60;
  }

  return beginClockAnimation(kind, hour, minute, eventMinuteId, nowMs,
                             nowMs + clockDurationMillisFor(kind));
}

void clockUpdateSecondAnchor(time_t epoch, uint32_t nowMs) {
  uint32_t epochSecond = static_cast<uint32_t>(epoch);
  if (!gClockHaveSecondAnchor || epochSecond != gClockLastEpochSecond) {
    gClockSecondAnchorSynced = gClockHaveSecondAnchor && epochSecond == gClockLastEpochSecond + 1;
    gClockLastEpochSecond = epochSecond;
    gClockSecondStartedAt = nowMs;
    gClockHaveSecondAnchor = true;
  }
}

uint16_t clockMsIntoSecond(uint32_t nowMs) {
  if (!gClockHaveSecondAnchor) {
    return 0;
  }
  uint32_t elapsed = nowMs - gClockSecondStartedAt;
  return elapsed > 999 ? 999 : static_cast<uint16_t>(elapsed);
}

bool updateClockAnimation(uint32_t nowMs) {
  if (gClockAnimation.active) {
    return false;
  }

  struct tm local;
  time_t epoch;
  if (!clockReadLocal(local, epoch)) {
    return false;
  }

  clockUpdateSecondAnchor(epoch, nowMs);
  if (!gClockSecondAnchorSynced) {
    return false;
  }

  time_t eventEpoch = epoch - local.tm_sec + 60;
  struct tm eventLocal;
  localtime_r(&eventEpoch, &eventLocal);
  ClockAnimationKind kind = clockKindForEvent(eventLocal);
  uint16_t durationMs = clockDurationMillisFor(kind);
  uint8_t durationSeconds = clockDurationMsFor(kind);
  if (durationMs == 0 || durationSeconds == 0) {
    return false;
  }

  uint32_t eventMinuteId = static_cast<uint32_t>(eventEpoch / 60);
  if (eventMinuteId == gClockLastScheduledMinuteId) {
    return false;
  }

  uint32_t secondsUntilEvent = static_cast<uint32_t>(eventEpoch - epoch);
  uint32_t msUntilEvent = secondsUntilEvent * 1000UL;
  uint16_t msIntoSecond = clockMsIntoSecond(nowMs);
  msUntilEvent = msUntilEvent > msIntoSecond ? msUntilEvent - msIntoSecond : 0;
  if (msUntilEvent > durationMs) {
    return false;
  }

  if (!beginClockAnimation(kind, static_cast<uint8_t>(eventLocal.tm_hour),
                           static_cast<uint8_t>(eventLocal.tm_min), eventMinuteId,
                           nowMs - (durationMs - msUntilEvent), nowMs + msUntilEvent)) {
    return false;
  }
  gClockLastScheduledMinuteId = eventMinuteId;
  return true;
}

bool clockAnimationActive() {
  return gClockAnimation.active;
}

bool clockAnimationFinalFrameDue(uint32_t nowMs) {
  return gClockAnimation.active && !gClockAnimation.finalRendered &&
         static_cast<int32_t>(nowMs - gClockAnimation.endsAt) >= 0;
}

uint8_t clockAnimationProgress(uint32_t nowMs) {
  if (!gClockAnimation.active) {
    return 0;
  }
  uint32_t elapsed = nowMs - gClockAnimation.startedAt;
  if (elapsed >= gClockAnimation.durationMs) {
    return 255;
  }
  return static_cast<uint8_t>((elapsed * 255UL) / gClockAnimation.durationMs);
}

uint16_t clockPixelHash(uint8_t x, uint8_t y, uint32_t seed) {
  uint32_t h = seed ^ (static_cast<uint32_t>(x) * 0x45D9F3B) ^
               (static_cast<uint32_t>(y) * 0x119DE1F3);
  h ^= h >> 16;
  h *= 0x7FEB352D;
  h ^= h >> 15;
  return static_cast<uint16_t>(h ^ (h >> 16));
}

uint16_t clockApproxDistance(uint8_t x, uint8_t y, int16_t cx, int16_t cy) {
  uint16_t dx = absDiff16(x, cx);
  uint16_t dy = absDiff16(y, cy);
  uint16_t smaller = dx < dy ? dx : dy;
  return dx + dy - (smaller >> 1);
}

bool clockDigitPixel(uint8_t digit, uint8_t col, uint8_t row) {
  if (digit > 9 || col > 2 || row > 4) {
    return false;
  }
  return kClockDigitRows[digit][row] & (1 << (2 - col));
}

bool clockDigitalGrid(uint8_t x, uint8_t y, uint8_t &col, uint8_t &row) {
  uint8_t sx = panelWidth / 18;
  uint8_t sy = panelHeight / 8;
  if (sx < 1) sx = 1;
  if (sy < 2) sy = 2;

  while (17 * sx > panelWidth && sx > 1) sx--;
  while (5 * sy > panelHeight && sy > 1) sy--;

  uint16_t totalW = 17 * sx;
  uint16_t totalH = 5 * sy;
  int16_t ox = (static_cast<int16_t>(panelWidth) - totalW) / 2;
  int16_t oy = (static_cast<int16_t>(panelHeight) - totalH) / 2;
  if (x < ox || y < oy || x >= ox + totalW || y >= oy + totalH) {
    return false;
  }

  col = (x - ox) / sx;
  row = (y - oy) / sy;
  return true;
}

bool clockDigitalColonCell(uint8_t col, uint8_t row) {
  return col == 8 && (row == 1 || row == 3);
}

bool clockDigitalPixel(uint8_t hour, uint8_t minute, uint8_t x, uint8_t y, Hsv &hsv) {
  uint8_t col, row;
  if (!clockDigitalGrid(x, y, col, row)) {
    return false;
  }

  uint8_t digits[4] = {
      static_cast<uint8_t>(hour / 10), static_cast<uint8_t>(hour % 10),
      static_cast<uint8_t>(minute / 10), static_cast<uint8_t>(minute % 10)};

  if (col < 3) {
    if (!clockDigitPixel(digits[0], col, row)) return false;
    hsv = {wrapHue(132 + minute * 2), 210, 235};
    return true;
  }
  if (col >= 4 && col < 7) {
    if (!clockDigitPixel(digits[1], col - 4, row)) return false;
    hsv = {wrapHue(148 + minute * 2), 205, 235};
    return true;
  }
  if (col == 8) {
    if (!clockDigitalColonCell(col, row)) return false;
    hsv = {96, 180, 220};
    return true;
  }
  if (col >= 10 && col < 13) {
    if (!clockDigitPixel(digits[2], col - 10, row)) return false;
    hsv = {wrapHue(176 + minute * 2), 205, 235};
    return true;
  }
  if (col >= 14 && col < 17) {
    if (!clockDigitPixel(digits[3], col - 14, row)) return false;
    hsv = {wrapHue(192 + minute * 2), 210, 235};
    return true;
  }

  return false;
}

uint8_t clockMinuteColonScale(uint32_t elapsedMs) {
  uint16_t phase = elapsedMs % 1000;
  if (phase < 420) return 255;
  if (phase < 540) return static_cast<uint8_t>(255 - ((phase - 420) * 205UL) / 120);
  if (phase < 850) return 50;
  return static_cast<uint8_t>(50 + ((phase - 850) * 205UL) / 150);
}

uint8_t clockMinuteSparkle(uint8_t x, uint8_t y, bool inGrid, uint32_t elapsedMs) {
  if (!inGrid) {
    return 0;
  }

  uint16_t hash = clockPixelHash(x, y, gClockAnimation.eventMinuteId);
  if ((hash & 0x7F) != ((elapsedMs / 85) & 0x7F)) {
    return 0;
  }

  uint8_t twinkle = triWave6((elapsedMs / 28 + (hash >> 8)) & 63) * 5;
  return twinkle > 110 ? 110 : twinkle;
}

uint16_t clockMinuteAnimatedColor(uint16_t index, uint8_t x, uint8_t y,
                                  bool targetPixel, uint8_t targetWeight,
                                  uint8_t easedProgress, uint32_t nowMs) {
  uint32_t elapsedMs = nowMs - gClockAnimation.startedAt;
  uint8_t col = 0, row = 0;
  bool inGrid = clockDigitalGrid(x, y, col, row);

  if (targetPixel) {
    uint8_t hue = nextHue[index];
    uint8_t sat = nextSat[index];
    uint8_t value = nextType[index];
    uint8_t shimmer = triWave6(elapsedMs / 46 + x * 3 + y * 5) * 2;

    value = addSaturated(value, shimmer);
    hue = wrapHue(hue + (shimmer >> 3));

    if (inGrid && clockDigitalColonCell(col, row)) {
      uint8_t colon = clockMinuteColonScale(elapsedMs);
      targetWeight = (static_cast<uint16_t>(targetWeight) * colon) / 255;
      hue = wrapHue(88 + (colon >> 2));
      sat = 170;
      value = addSaturated(190, colon >> 2);
    }

    return hsv565(hue, sat, (static_cast<uint16_t>(value) * targetWeight) / 255);
  }

  uint8_t accent = 0;
  uint8_t sparkle = clockMinuteSparkle(x, y, inGrid, elapsedMs);
  if (sparkle > accent) accent = sparkle;
  if (accent == 0) {
    return 0;
  }

  accent = static_cast<uint8_t>((accent * static_cast<uint16_t>(easedProgress)) / 255);
  return hsv565(wrapHue(106 + (elapsedMs / 24) + x + y), 185, accent);
}

bool clockNearLine(int16_t x, int16_t y, int16_t x0, int16_t y0,
                   int16_t x1, int16_t y1, uint8_t thickness) {
  int32_t dx = x1 - x0;
  int32_t dy = y1 - y0;
  int32_t len2 = dx * dx + dy * dy;
  if (len2 == 0) {
    return absDiff16(x, x0) <= thickness && absDiff16(y, y0) <= thickness;
  }

  int32_t px = x - x0;
  int32_t py = y - y0;
  int32_t dot = px * dx + py * dy;
  if (dot < 0 || dot > len2) {
    return false;
  }

  int64_t cross = static_cast<int64_t>(px) * dy - static_cast<int64_t>(py) * dx;
  if (cross < 0) cross = -cross;
  int64_t limit = static_cast<int64_t>(thickness) * thickness * len2;
  return cross * cross <= limit;
}

bool clockDiscPixel(uint8_t x, uint8_t y, int16_t cx, int16_t cy, uint8_t radius) {
  int16_t dx = static_cast<int16_t>(x) - cx;
  int16_t dy = static_cast<int16_t>(y) - cy;
  return dx * dx + dy * dy <= radius * radius;
}

bool clockRingPixel(uint8_t x, uint8_t y, int16_t cx, int16_t cy,
                    uint8_t radius, uint8_t thickness) {
  int16_t dx = static_cast<int16_t>(x) - cx;
  int16_t dy = static_cast<int16_t>(y) - cy;
  int32_t d2 = dx * dx + dy * dy;
  int32_t r2 = static_cast<int32_t>(radius) * radius;
  int32_t band = 2L * radius * thickness + thickness * thickness;
  int32_t delta = d2 > r2 ? d2 - r2 : r2 - d2;
  return delta <= band;
}

uint8_t clockAnalogRadius(uint16_t md) {
  uint8_t radius = (md * 44) / 100;
  return radius < 4 ? 4 : radius;
}

int16_t clockSin60(uint8_t tick) {
  tick %= 60;
  uint8_t base = tick / 5;
  uint8_t frac = tick % 5;
  int16_t a = kClockSin12[base];
  int16_t b = kClockSin12[(base + 1) % 12];
  return (a * (5 - frac) + b * frac) / 5;
}

int16_t clockCos60(uint8_t tick) {
  tick %= 60;
  uint8_t base = tick / 5;
  uint8_t frac = tick % 5;
  int16_t a = kClockCos12[base];
  int16_t b = kClockCos12[(base + 1) % 12];
  return (a * (5 - frac) + b * frac) / 5;
}

int16_t clockPointX(int16_t cx, uint8_t radius, uint8_t tick) {
  return cx + (clockSin60(tick) * static_cast<int16_t>(radius)) / 127;
}

int16_t clockPointY(int16_t cy, uint8_t radius, uint8_t tick) {
  return cy - (clockCos60(tick) * static_cast<int16_t>(radius)) / 127;
}

bool clockHourNumeralPixel(uint8_t hour, uint8_t x, uint8_t y, Hsv &hsv) {
  uint16_t md = clockMinDimension();
  uint8_t hour12 = hour % 12;
  if (hour12 == 0) hour12 = 12;

  uint8_t sx = md / 42 + 1;
  uint8_t sy = sx + 1;
  uint8_t digitCount = hour12 >= 10 ? 2 : 1;
  uint8_t totalW = digitCount == 2 ? 7 * sx : 3 * sx;
  uint8_t totalH = 5 * sy;
  int16_t cx = (panelWidth - 1) / 2;
  int16_t cy = (panelHeight - 1) / 2;
  uint8_t radius = clockAnalogRadius(md);
  int16_t ox = cx - totalW / 2;
  int16_t oy = cy + radius / 6 - totalH / 2;

  if (x < ox || y < oy || x >= ox + totalW || y >= oy + totalH) {
    return false;
  }

  uint8_t col = (static_cast<int16_t>(x) - ox) / sx;
  uint8_t row = (static_cast<int16_t>(y) - oy) / sy;
  uint8_t digit = hour12;
  if (digitCount == 2) {
    if (col < 3) {
      digit = hour12 / 10;
    } else if (col >= 4 && col < 7) {
      digit = hour12 % 10;
      col -= 4;
    } else {
      return false;
    }
  }

  if (!clockDigitPixel(digit, col, row)) {
    return false;
  }

  hsv = {wrapHue(18 + hour12 * 9), 225, 250};
  return true;
}

bool clockAnalogPixel(uint8_t hour, uint8_t minute, uint8_t x, uint8_t y, Hsv &hsv) {
  uint16_t md = clockMinDimension();
  int16_t cx = (panelWidth - 1) / 2;
  int16_t cy = (panelHeight - 1) / 2;
  uint8_t radius = clockAnalogRadius(md);
  uint8_t ringThickness = md / 64 + 1;
  uint8_t handThickness = md / 52 + 1;

  uint8_t minuteTick = minute % 60;
  uint8_t hourTick = ((hour % 12) * 5 + minute / 12) % 60;
  int16_t minuteX = clockPointX(cx, (radius * 74) / 100, minuteTick);
  int16_t minuteY = clockPointY(cy, (radius * 74) / 100, minuteTick);
  int16_t hourX = clockPointX(cx, (radius * 50) / 100, hourTick);
  int16_t hourY = clockPointY(cy, (radius * 50) / 100, hourTick);

  if (clockHourNumeralPixel(hour, x, y, hsv)) {
    return true;
  }

  if (clockNearLine(x, y, cx, cy, hourX, hourY, handThickness + 1)) {
    hsv = {24, 230, 250};
    return true;
  }
  if (clockNearLine(x, y, cx, cy, minuteX, minuteY, handThickness)) {
    hsv = {136, 220, 242};
    return true;
  }
  if (clockDiscPixel(x, y, cx, cy, md / 32 + 1)) {
    hsv = {108, 120, 255};
    return true;
  }

  for (uint8_t i = 0; i < 12; i++) {
    bool major = (i % 3) == 0;
    uint8_t tick = i * 5;
    uint8_t inner = radius - (major ? radius / 5 : radius / 9);
    int16_t outerX = clockPointX(cx, radius, tick);
    int16_t outerY = clockPointY(cy, radius, tick);
    int16_t innerX = clockPointX(cx, inner, tick);
    int16_t innerY = clockPointY(cy, inner, tick);
    if (clockNearLine(x, y, innerX, innerY, outerX, outerY,
                      major ? ringThickness + 1 : ringThickness)) {
      hsv = {wrapHue(150 + i * 8), static_cast<uint8_t>(major ? 150 : 175),
             static_cast<uint8_t>(major ? 235 : 175)};
      return true;
    }
  }

  if (md >= 64) {
    uint8_t dotRadius = md >= 96 ? 1 : 0;
    for (uint8_t i = 0; i < 60; i++) {
      if ((i % 5) == 0) continue;
      uint8_t dotDistance = radius - ringThickness;
      int16_t dotX = clockPointX(cx, dotDistance, i);
      int16_t dotY = clockPointY(cy, dotDistance, i);
      if (clockDiscPixel(x, y, dotX, dotY, dotRadius)) {
        hsv = {168, 150, 105};
        return true;
      }
    }
  }

  if (clockRingPixel(x, y, cx, cy, radius, ringThickness)) {
    hsv = {158, 180, 95};
    return true;
  }

  uint8_t innerRadius = (radius * 72) / 100;
  if (clockRingPixel(x, y, cx, cy, innerRadius, 1)) {
    hsv = {188, 160, 55};
    return true;
  }

  return false;
}

struct ClockHourRenderState {
  uint32_t elapsedMs;
  uint16_t md;
  int16_t cx;
  int16_t cy;
  uint8_t radius;
  int16_t sweepX;
  int16_t sweepY;
};

ClockHourRenderState clockHourRenderStateFor(uint32_t nowMs) {
  uint32_t elapsedMs = nowMs - gClockAnimation.startedAt;
  uint16_t md = clockMinDimension();
  int16_t cx = (panelWidth - 1) / 2;
  int16_t cy = (panelHeight - 1) / 2;
  uint8_t radius = clockAnalogRadius(md);
  uint8_t sweepTick = ((elapsedMs % kClockHourAnimationMs) * 60UL) / kClockHourAnimationMs;
  uint8_t sweepRadius = (radius * 92) / 100;
  return {elapsedMs, md, cx, cy, radius,
          clockPointX(cx, sweepRadius, sweepTick),
          clockPointY(cy, sweepRadius, sweepTick)};
}

uint8_t clockHourSweepValue(uint8_t x, uint8_t y, const ClockHourRenderState &state) {
  if (clockDiscPixel(x, y, state.sweepX, state.sweepY, state.md / 42 + 1)) {
    return 190;
  }
  if (clockNearLine(x, y, state.cx, state.cy, state.sweepX, state.sweepY,
                    state.md / 74 + 1)) {
    return 88;
  }
  return 0;
}

uint16_t clockHourAnimatedColor(uint16_t index, uint8_t x, uint8_t y,
                                bool targetPixel, uint8_t targetWeight,
                                uint8_t easedProgress, const ClockHourRenderState &state) {
  uint8_t sweep = clockHourSweepValue(x, y, state);

  if (targetPixel) {
    uint8_t hue = nextHue[index];
    uint8_t sat = nextSat[index];
    uint8_t value = nextType[index];
    uint8_t shimmer = triWave6(state.elapsedMs / 48 + x * 2 + y * 3) * 2;

    value = addSaturated(value, shimmer);
    if (sweep) {
      hue = wrapHue(4 + (sweep >> 3));
      sat = 230;
      value = addSaturated(value, sweep >> 1);
    }

    return hsv565(hue, sat, (static_cast<uint16_t>(value) * targetWeight) / 255);
  }

  uint8_t accent = sweep;
  uint8_t hue = sweep ? 6 : 176;
  if (clockRingPixel(x, y, state.cx, state.cy, state.radius + 1, state.md / 96 + 1)) {
    uint8_t ring = 26 + triWave6(state.elapsedMs / 70 + x + y);
    if (ring > accent) {
      accent = ring;
      hue = wrapHue(168 + state.elapsedMs / 64);
    }
  }
  if (clockRingPixel(x, y, state.cx, state.cy, (state.radius * 72) / 100, 1)) {
    uint8_t inner = 16 + triWave6(state.elapsedMs / 58 + x * 3);
    if (inner > accent) {
      accent = inner;
      hue = 196;
    }
  }

  uint16_t hash = clockPixelHash(x, y, gClockAnimation.eventMinuteId ^ 0xA5A5A5A5UL);
  if ((hash & 0xFF) == ((state.elapsedMs / 45) & 0xFF)) {
    uint8_t spark = 38 + triWave6((state.elapsedMs / 18 + (hash >> 8)) & 63);
    if (spark > accent) {
      accent = spark;
      hue = wrapHue(32 + (hash >> 5));
    }
  }

  if (accent == 0) {
    return 0;
  }

  accent = (static_cast<uint16_t>(accent) * easedProgress) / 255;
  return hsv565(hue, sweep ? 225 : 185, accent);
}

bool clockFacePixel(ClockAnimationKind kind, uint8_t hour, uint8_t minute,
                    uint8_t x, uint8_t y, Hsv &hsv) {
  if (kind == kClockAnimationMinute) {
    return clockDigitalPixel(hour, minute, x, y, hsv);
  }

  if (kind == kClockAnimationHour) {
    return clockAnalogPixel(hour, minute, x, y, hsv);
  }

  return false;
}

void precomputeClockFace() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    RowBits row = 0;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = baseIndex + x;
      Hsv target;
      if (clockFacePixel(gClockAnimation.kind, gClockAnimation.hour,
                         gClockAnimation.minute, x, y, target)) {
        row |= bitForX[x];
        nextHue[index] = target.h;
        nextSat[index] = target.s;
        nextType[index] = target.v;
      } else {
        nextHue[index] = 0;
        nextSat[index] = 0;
        nextType[index] = 0;
      }
    }

    nextRows[y] = row & activeMask;
  }
}

uint8_t clockRevealWeight(ClockAnimationKind kind, uint8_t x, uint8_t y, uint8_t progress) {
  if (progress >= 254) {
    return 255;
  }

  uint16_t hash = clockPixelHash(x, y, gClockAnimation.eventMinuteId);
  uint8_t order = 0;
  int16_t cx = (panelWidth - 1) / 2;
  int16_t cy = (panelHeight - 1) / 2;
  uint16_t dist = clockApproxDistance(x, y, cx, cy);
  uint16_t maxDist = clockApproxDistance(0, 0, cx, cy);
  if (maxDist == 0) maxDist = 1;

  if (kind == kClockAnimationMinute) {
    order = clockClamp8((static_cast<uint16_t>(x) * 210) / panelWidth + (hash & 31));
  } else {
    order = clockClamp8(18 + (dist * 154) / maxDist + (hash & 23));
  }

  if (progress <= order) {
    return 0;
  }
  uint16_t delta = progress - order;
  return delta > 42 ? 255 : static_cast<uint8_t>((delta * 255) / 42);
}

uint16_t clockScaledHsv565(uint8_t hue, uint8_t saturation, uint8_t value, uint8_t scale) {
  return hsv565(hue, saturation, (static_cast<uint16_t>(value) * scale) / 255);
}

uint16_t approachColor565(uint16_t current, uint16_t target, uint8_t step) {
  uint8_t cr = ((current >> 11) & 0x1F) * 255 / 31;
  uint8_t cg = ((current >> 5) & 0x3F) * 255 / 63;
  uint8_t cb = (current & 0x1F) * 255 / 31;
  uint8_t tr = ((target >> 11) & 0x1F) * 255 / 31;
  uint8_t tg = ((target >> 5) & 0x3F) * 255 / 63;
  uint8_t tb = (target & 0x1F) * 255 / 31;
  return color565(approach(cr, tr, step), approach(cg, tg, step), approach(cb, tb, step));
}

uint8_t clockColorStep(ClockAnimationKind kind) {
  if (kind == kClockAnimationMinute) return 80;
  return 48;
}

void renderClockAnimationFrame(uint32_t nowMs) {
  uint32_t renderStartedAt = micros();
  updatedPixels = 0;
  uint8_t progress = clockAnimationProgress(nowMs);
  uint8_t eased = clockSmoothstep8(progress);
  ClockHourRenderState hourState = clockHourRenderStateFor(nowMs);

  for (uint8_t y = 0; y < panelHeight; y++) {
    RowBits targetRow = nextRows[y] & activeMask;
    uint16_t baseIndex = y * kMaxWidth;

    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = baseIndex + x;
      bool targetPixel = targetRow & bitForX[x];
      uint8_t targetWeight = targetPixel ? clockRevealWeight(gClockAnimation.kind, x, y, eased) : 0;
      uint16_t targetColor = 0;
      if (gClockAnimation.kind == kClockAnimationMinute) {
        targetColor = clockMinuteAnimatedColor(index, x, y, targetPixel, targetWeight,
                                               eased, nowMs);
      } else if (gClockAnimation.kind == kClockAnimationHour) {
        targetColor = clockHourAnimatedColor(index, x, y, targetPixel, targetWeight,
                                             eased, hourState);
      } else if (targetPixel) {
        targetColor = clockScaledHsv565(nextHue[index], nextSat[index],
                                        nextType[index], targetWeight);
      }
      uint16_t color = progress >= 254 ? targetColor :
                       approachColor565(drawnColor[index], targetColor,
                                         clockColorStep(gClockAnimation.kind));

      if (color != drawnColor[index]) {
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

void forceClockLifeRedraw() {
  for (uint8_t y = 0; y < panelHeight; y++) {
    uint16_t baseIndex = y * kMaxWidth;
    for (uint8_t x = 0; x < panelWidth; x++) {
      forceRedraw[baseIndex + x] = true;
    }
  }
}

void commitClockFaceToLife() {
  liveCells = 0;
  changedCells = panelWidth * panelHeight;

  for (uint8_t y = 0; y < panelHeight; y++) {
    RowBits cachedRow = nextRows[y] & activeMask;
    uint16_t baseIndex = y * kMaxWidth;
    for (uint8_t x = 0; x < panelWidth; x++) {
      uint16_t index = baseIndex + x;
      if (cachedRow & bitForX[x]) {
        cellType[index] = randomType();
        cellHue[index] = nextHue[index];
        cellSat[index] = nextSat[index] < 150 ? 150 : nextSat[index];
        cellAge[index] = 8;
        visualHue[index] = nextHue[index];
        visualSat[index] = nextSat[index];
        visualValue[index] = nextType[index];
        drawnColor[index] = hsv565(nextHue[index], nextSat[index], nextType[index]);
        liveCells++;
      } else {
        cellAge[index] = 0;
        visualHue[index] = 0;
        visualSat[index] = 0;
        visualValue[index] = 0;
        drawnColor[index] = 0;
      }
      forceRedraw[index] = true;
    }
    currentRows[y] = cachedRow;
    nextRows[y] = 0;
  }
}

bool finishClockAnimationAfterRender(uint32_t nowMs) {
  if (!gClockAnimation.active) {
    return false;
  }

  if (!gClockAnimation.finalRendered) {
    if (!clockAnimationFinalFrameDue(nowMs)) {
      return false;
    }
    gClockAnimation.finalRendered = true;
    return false;
  }

  if (static_cast<int32_t>(nowMs - (gClockAnimation.endsAt + kClockPostAnimationHoldMs)) < 0) {
    return false;
  }

  if (!gPaused) {
    commitClockFaceToLife();
  } else {
    forceClockLifeRedraw();
  }
  gClockAnimation.active = false;
  return true;
}

#else

bool updateClockAnimation(uint32_t nowMs) {
  (void)nowMs;
  return false;
}

bool startClockAnimationRequest(uint8_t request, uint32_t nowMs) {
  (void)request;
  (void)nowMs;
  return false;
}

bool clockAnimationActive() {
  return false;
}

bool clockAnimationFinalFrameDue(uint32_t nowMs) {
  (void)nowMs;
  return false;
}

void renderClockAnimationFrame(uint32_t nowMs) {
  (void)nowMs;
}

bool finishClockAnimationAfterRender(uint32_t nowMs) {
  (void)nowMs;
  return false;
}

#endif
