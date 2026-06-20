#pragma once
// Pure, host-testable runtime tuning knobs for the Game of Life simulation.
// Everything is generated from one X-macro field list so the struct, defaults,
// metadata, clamping, apply-by-key, and JSON serializer can never drift apart.
//
// X(type, name, label, group, default, min, max, step, desc)
// `desc` is a human-readable explanation surfaced under each slider in the web UI.
// Keep desc ASCII and free of double-quotes (it is emitted into JSON as-is).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LIFE_SETTINGS_FIELDS(X)                                                                                                                                  \
  X(uint16_t, lifeStepMs,              "Life step (ms)",     "Simulation",    100,   10,  1000,   5, "Milliseconds between Game of Life generations. Lower = faster evolution.") \
  X(uint16_t, burnStepMs,              "Burn step (ms)",     "Simulation",     29,    5,   200,   1, "Milliseconds per frame of the knock-triggered burn wave. Lower = faster burn.") \
  X(uint16_t, renderFrameMs,           "Render frame (ms)",  "Simulation",     33,   10,   200,   1, "Minimum milliseconds between LED redraws. Lower = higher frame rate, more CPU.") \
  X(uint16_t, minLiveCells,            "Min live cells",     "Simulation",      8,    0,   200,   1, "When live cells fall below this, fresh life is injected so the board never dies out.") \
  X(uint8_t,  hueStep,                 "Hue step",           "Color/fade",      3,    1,    64,   1, "Steps each cell's hue moves toward its target per frame. Higher = snappier color shifts.") \
  X(uint8_t,  satStep,                 "Saturation step",    "Color/fade",      7,    1,    64,   1, "How fast saturation approaches its target per frame. Higher = quicker intensity changes.") \
  X(uint8_t,  liveValueStep,           "Fade-in step",       "Color/fade",     10,    1,    64,   1, "How fast a newly born cell brightens to full. Higher = quicker fade-in.") \
  X(uint8_t,  deathValueStep,          "Fade-out step",      "Color/fade",      8,    1,    64,   1, "How fast a dying cell dims to black. Higher = quicker fade-out.") \
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

// Emit JSON: {"fields":[{key,label,group,min,max,step,live,saved,default,desc}, ...]}.
// snprintf semantics: returns the number of bytes that *would* be written.
inline size_t serializeSettingsJson(const LifeSettings &live, const LifeSettings &saved,
                                    const LifeSettings &defaults, char *out, size_t cap) {
  size_t n = 0;
#define APPEND(...)                                                              \
  do {                                                                           \
    int w = snprintf(out + (n < cap ? n : cap), (n < cap ? cap - n : 0), __VA_ARGS__); \
    if (w > 0) n += (size_t)w;                                                   \
  } while (0)
  APPEND("{\"fields\":[");
  bool first = true;
#define X(type, name, label, group, def, lo, hi, step, desc)                     \
  APPEND("%s{\"key\":\"%s\",\"label\":\"%s\",\"group\":\"%s\","                  \
         "\"min\":%ld,\"max\":%ld,\"step\":%ld,"                                 \
         "\"live\":%ld,\"saved\":%ld,\"default\":%ld,\"desc\":\"%s\"}",          \
         first ? "" : ",", #name, label, group,                                  \
         (long)(lo), (long)(hi), (long)(step),                                   \
         (long)live.name, (long)saved.name, (long)defaults.name, desc);          \
  first = false;
  LIFE_SETTINGS_FIELDS(X)
#undef X
  APPEND("]}");
#undef APPEND
  return n;
}
