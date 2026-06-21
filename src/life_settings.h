#pragma once
// Pure, host-testable runtime tuning knobs for the Game of Life simulation.
// Everything is generated from one X-macro field list so the struct, defaults,
// clamping, apply-by-key, and the field-metadata table can never drift apart.
// This header is dependency-free (no Arduino/ArduinoJson); the web portal walks
// the metadata table to build JSON with ArduinoJson, so no serializer lives here.
//
// X(type, name, label, group, default, min, max, step, desc)
// `desc` is a human-readable explanation surfaced under each slider in the web UI.
// Keep desc ASCII; the portal serializes it to JSON with ArduinoJson (which escapes it).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LIFE_SETTINGS_FIELDS(X)                                                                                                                                  \
  X(uint16_t, lifeStepMs,              "Life step (ms)",     "Simulation",    100,   10,  1000,   5, "Milliseconds between Game of Life generations. Lower = faster evolution.") \
  X(uint16_t, burnStepMs,              "Burn step (ms)",     "Simulation",     29,    5,   200,   1, "Milliseconds per frame of the knock-triggered burn wave. Lower = faster burn.") \
  X(uint16_t, renderFrameMs,           "Render frame (ms)",  "Simulation",     33,   10,   200,   1, "Minimum milliseconds between LED redraws. Lower = higher frame rate, more CPU.") \
  X(uint16_t, minLiveCells,            "Min live cells",     "Simulation",      8,    0,   200,   1, "When live cells fall below this, fresh life is injected so the board never dies out.") \
  X(uint8_t,  disableReseed,           "Disable auto-reseed","Simulation",      0,    0,     1,   1, "Let the board evolve untouched and die out: no min-live reseed and no random spawns.") \
  X(uint8_t,  hueStep,                 "Hue step",           "Color/fade",      3,    1,    64,   1, "Steps each cell's hue moves toward its target per frame. Higher = snappier color shifts.") \
  X(uint8_t,  satStep,                 "Saturation step",    "Color/fade",      7,    1,    64,   1, "How fast saturation approaches its target per frame. Higher = quicker intensity changes.") \
  X(uint8_t,  liveValueStep,           "Fade-in step",       "Color/fade",     10,    1,    64,   1, "How fast a newly born cell brightens to full. Higher = quicker fade-in.") \
  X(uint8_t,  deathValueStep,          "Fade-out step",      "Color/fade",      8,    1,    64,   1, "How fast a dying cell dims to black. Higher = quicker fade-out.") \
  X(uint8_t,  noFade,                  "Classic (no fade)",  "Color/fade",      0,    0,     1,   1, "Cells snap fully on or off each frame -- classic Game of Life, no colour fade in or out.") \
  X(uint8_t,  mediumChunkMass,         "Medium chunk mass",  "Density",         7,    1,    32,   1, "Local cluster mass above which births slow slightly, throttling medium-density blobs.") \
  X(uint8_t,  largeChunkMass,          "Large chunk mass",   "Density",        12,    1,    48,   1, "Local cluster mass above which births slow more, throttling large blobs.") \
  X(uint8_t,  hugeChunkMass,           "Huge chunk mass",    "Density",        18,    1,    64,   1, "Local cluster mass above which births are throttled most, curbing runaway dense regions.") \
  X(uint8_t,  accelPollMs,             "Accel poll (ms)",    "Accelerometer",  35,    5,   200,   1, "Milliseconds between accelerometer reads. Lower = more responsive tilt and shake.") \
  X(int16_t,  tiltDeadzone,            "Tilt deadzone",      "Accelerometer", 650,    0,  8000,  50, "Tilt below this magnitude is ignored as noise. Higher = needs a firmer tilt to steer.") \
  X(int16_t,  strongTilt,              "Strong tilt",        "Accelerometer",2500,    0, 16000,  50, "Tilt at or above this counts as a full-strength tilt for spawn bias. Lower = easier to max out.") \
  X(uint16_t, shakeDelta,              "Shake threshold",    "Accelerometer",10000,1000, 32000, 100, "Jolt size that registers as a shake (scatters gliders and bursts). Higher = needs a harder shake.") \
  X(uint8_t,  burnRingWidth,           "Burn ring width",    "Burn wave",       2,    1,    16,   1, "Thickness in pixels of the expanding burn-wave ring.") \
  X(uint8_t,  burnFadeStep,            "Burn fade step",     "Burn wave",      18,    1,    64,   1, "How fast scorched cells cool and fade after the burn passes. Higher = quicker recovery.") \
  X(uint8_t,  motionGlowFadeStep,      "Motion glow fade",   "Burn wave",       3,    1,    32,   1, "How fast the motion-reaction glow fades out. Higher = shorter-lived glow.") \
  X(int16_t,  knockAxisMinimumImpulse, "Knock min impulse",  "Knock",        6000,    0, 32000, 100, "Smallest knock impulse per axis that counts as a tap. Higher = ignores gentle taps.") \
  X(int16_t,  knockImpulseFullScale,   "Knock full scale",   "Knock",       18000, 1000, 32000, 100, "Knock impulse mapped to a maximum-strength burst. Lower = small knocks feel stronger.")

struct LifeSettings {
#define X(type, name, label, group, def, lo, hi, step, desc) type name;
  LIFE_SETTINGS_FIELDS(X)
#undef X
};

inline long clampLong(long v, long lo, long hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

inline LifeSettings defaultLifeSettings() {
  LifeSettings s = {};   // zero-init (incl. padding) so defaults are byte-deterministic
#define X(type, name, label, group, def, lo, hi, step, desc) s.name = (type)(def);
  LIFE_SETTINGS_FIELDS(X)
#undef X
  return s;
}

inline void clampSettings(LifeSettings &s) {
#define X(type, name, label, group, def, lo, hi, step, desc) \
  s.name = (type)clampLong((long)s.name, (long)(lo), (long)(hi));
  LIFE_SETTINGS_FIELDS(X)
#undef X
}

// Set one field by key from a numeric value, clamped to its range.
// Returns true if the key matched a field.
inline bool applyLifeSettingField(LifeSettings &s, const char *key, long value) {
#define X(type, name, label, group, def, lo, hi, step, desc) \
  if (strcmp(key, #name) == 0) { s.name = (type)clampLong(value, (long)(lo), (long)(hi)); return true; }
  LIFE_SETTINGS_FIELDS(X)
#undef X
  return false;
}

// Per-field metadata as a pure data table (no ArduinoJson, no Arduino). The web portal
// walks this to build JSON; the X-macro stays format-agnostic.
//
// static constexpr (not inline constexpr): this header is included into the main.cpp and
// web_portal.cpp TUs. inline-constexpr *variables* are C++17-only and platformio.ini does
// not pin -std, so we use static constexpr — internal linkage per TU, ODR-safe in C++11+.
// A TU that doesn't reference the table optimizes it away under -O3.
struct LifeFieldMeta {
  const char *key;
  const char *label;
  const char *group;
  const char *desc;
  long min;
  long max;
  long step;
};

static constexpr LifeFieldMeta kLifeFieldMeta[] = {
#define X(type, name, label, group, def, lo, hi, step, desc) \
  { #name, label, group, desc, (long)(lo), (long)(hi), (long)(step) },
  LIFE_SETTINGS_FIELDS(X)
#undef X
};

static constexpr size_t kLifeFieldCount = sizeof(kLifeFieldMeta) / sizeof(kLifeFieldMeta[0]);

// Read field `i`'s current value from a settings instance as a long.
inline long getLifeSettingByIndex(const LifeSettings &s, size_t i) {
  size_t j = 0;
#define X(type, name, label, group, def, lo, hi, step, desc) \
  if (j++ == i) return (long)s.name;
  LIFE_SETTINGS_FIELDS(X)
#undef X
  return 0;
}

// Read a field's current value by key. Returns false (and leaves *out untouched) if unknown.
inline bool getLifeSettingByKey(const LifeSettings &s, const char *key, long *out) {
#define X(type, name, label, group, def, lo, hi, step, desc) \
  if (strcmp(key, #name) == 0) { *out = (long)s.name; return true; }
  LIFE_SETTINGS_FIELDS(X)
#undef X
  return false;
}

