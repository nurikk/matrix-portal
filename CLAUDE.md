# CLAUDE.md

Guidance for working in this repo. See `README.md` for the user-facing overview.

## What this is

Arduino/PlatformIO firmware for Adafruit MatrixPortal boards (ESP32-S3 and SAMD51 M4)
that runs a colored, accelerometer-interactive Conway's Game of Life on a HUB75 LED
matrix via Adafruit Protomatter. `src/main.cpp` is a thin orchestrator (board setup,
`setup()`/`loop()`) that `#include`s the Life logic from focused `src/life_*.h` headers
into a single translation unit; the S3 build also compiles `src/web_portal.cpp` (WiFi
control panel). See "Code structure" below for the module map.

## Build & upload

```sh
pio run -e matrixportal_s3                                   # build (default env)
pio run -e matrixportal_s3 -t upload --upload-port <PORT>    # flash
pio device monitor -b 115200                                 # serial profiler output
```

The serial port used during development is `/dev/cu.usbmodem2021301` — confirm with
`pio device list` before flashing.

Environments are defined in `platformio.ini`. Behavior is selected at **compile time**
via `build_flags`: `MATRIX_WIDTH`, `MATRIX_BIT_DEPTH`, `MATRIX_RGB_CHAINS`, `MATRIX_TILE`,
`MATRIX_BENCHMARK`. Geometry and the benchmark/board selection are compile-time and require a rebuild + reflash; the ~20 simulation knobs are tunable at runtime via the WiFi control panel (ESP32-S3 only — see below).

## Host tests (no board)

The pure simulation core is in `src/life_bits.h` and is unit-tested on the host:

```sh
sh tests/run.sh        # compiles & runs tests/test_life_bits.cpp with clang++
```

`tests/test_life_bits.cpp` checks the bitwise `computeConwayNext`/`conwayNextRow` against
an independent cell-by-cell Conway oracle on random boards at widths that straddle the
64-bit word boundary (63/64/65/127/128), plus blinker/block/glider patterns. Run this after
any change to the simulation's neighbour/rule math. `tests/test_life_settings.cpp` verifies
the `LifeSettings` X-macro expansion, clamp logic, and field metadata — run it after
touching `src/life_settings.h`. (`tests/` is intentionally *not* PlatformIO's `test_dir`,
so `pio test` won't try to run these on-target.)

## Things that will trip you up

- **Two firmwares, one `MATRIX_BENCHMARK` gate.** In `main.cpp`, `#if !MATRIX_BENCHMARK`
  includes the Life headers (`life_state.h` … `life_sim.h`) and defines the Life
  `setup()`/`loop()`; `#else` includes `benchmark.h` (the synthetic display benchmark with
  its own `setup()`/`loop()`). The board pinout, geometry constants, and the
  `matrix`/`accelerometer` objects in the preamble are shared by both.
- **The Life code is one translation unit, split across headers.** The `src/life_*.h`
  files are *not* standalone — `main.cpp` `#include`s them (each has `#pragma once` and a
  header comment), exactly once, in dependency order. They define non-`inline` functions and
  globals, so **never include them from another `.cpp`** (duplicate symbols), and **keep the
  include order** in `main.cpp` (state → util → profile → color → render → burn → input →
  spawn → sim): e.g. `life_profile.h` must precede `life_render.h` because `renderFrame`
  calls `addProfile`, and `finishBurnWave` relies on the `seedLife` forward declaration in
  `life_state.h`. Staying one TU is deliberate — `-O3` with no LTO still inlines across the
  headers as if it were a single file.
- **Library-patching pre-script.** `scripts/patch_protomatter_s3_clock.py` rewrites
  `LCD_CLK_PRESCALE` inside the *downloaded* Protomatter source under `.pio/libdeps` to
  overclock the HUB75 clock. Only the `..._life_4bit_fast` env uses it. It re-applies on
  clean builds; if the macro name changes upstream it silently no-ops.
- **RAM ceiling.** Per-cell state is ~14 bytes/cell (the `cell*`/`next*`/`visual*` arrays
  in `src/life_state.h`). 128×128 = ~224 KB — fits the S3, **overflows the M4's
  192 KB**. M4 can run the 128×128 benchmark but not 128×128 Life.
- **Single chain only for Life.** A compile-time `#error` enforces ≤128×128, single
  RGB chain, tile in [-2, 2].
- **M4 bootloader uploads.** Use `matrixportal_m4_bootloader` when the 1200-baud touch
  fights an already-in-bootloader board.
- **Web portal is S3-only and must stay gated.** `src/web_portal.cpp` and `src/web_ui.h`
  are included in every PlatformIO build (all `.cpp` files compile automatically). Their
  bodies are wrapped in
  `#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3) && !defined(MATRIX_BENCHMARK)`.
  Do not remove that guard — M4 and benchmark builds have no networking and must remain
  unchanged.
- **Two cores, one `gLive`.** AsyncTCP is pinned to **core 0** via
  `-D CONFIG_ASYNC_TCP_RUNNING_CORE=0`; the Life `loop()` runs on **core 1**. WebSocket
  callbacks and a core-0 `wsPushTask` (broadcasts a binary board frame ~every 100 ms and
  stats ~every 500 ms, calls `cleanupClients`) stage incoming edits into `gPending` (under
  `gSettingsMux`); `webPortalTick()` on core 1 promotes them into `gLive` and handles
  deferred actions (reseed, burn, clear, pause/resume, forget-wifi reboot) — `gLive` is
  never written from core
  0. Browser-drawn cells follow the same protocol: a binary draw frame is staged into
  `gDrawBitmask` + the `gReqDraw` flag (under `gDrawMux`, a single-producer/single-consumer
  hand-off — core 0 fills the buffer only while the flag is clear, core 1 clears it only
  after `applyDrawnCells` has injected the cells into `currentRows`). Do not read
  `gPending`/`gDrawBitmask` or write `gLive`/the cell arrays outside this protocol.
  `liveCells`/`generation`
  are intentionally non-volatile (read per-cell in the render hot loop); stats **and** the
  board frame (core 0 reads `drawnColor` via `copyDrawnFrame` while core 1 writes it) are
  accepted benign cross-core races — do not add `volatile`.
- **`huge_app.csv` partition.** `[env:matrixportal_s3]` uses this partition table to fit
  BLE + WiFi + app code. Switching back to the default partition table will cause the S3
  binary to overflow.

## Code structure

`src/main.cpp` is a thin orchestrator: includes, board pinout (`#if`/`#elif` on
`ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3` / `_VARIANT_MATRIXPORTAL_M4_`), geometry
constants, the `matrix`/`accelerometer` objects, the `MATRIX_BENCHMARK` gate, and the Life
`setup()`/`scrollIpOnce()`/`loop()`. All other Life code lives in headers `#include`d into
that one TU (see the single-TU note above). The Life headers, in include/dependency order:

- **`life_state.h`** — the data model: Life constants, the `#error` geometry guard,
  `struct Hsv` / `struct NeighborMix`, `speciesHues`, the settings instances
  (`gLive`/`gSaved`/`gDefaults`), and **all** cell-state / sim / accel / burn / timing /
  profiling globals (the `cell*`/`next*`/`visual*` arrays, counters, etc.), plus the
  `seedLife` forward declaration.
- **`life_util.h`** — low-level numeric / RNG / motion-glow helpers (`popcount64`, `ctz64`,
  `random32`, `clamp16`, `wrapHue`, `raiseMotionGlow`, …).
- **`life_profile.h`** — timing accumulators (`addProfile`/`averageProfile`/
  `resetProfileCounters`) and `reportFps` (dumps timing once per second). Sits before
  `life_render.h` because `renderFrame` calls `addProfile`.
- **`life_color.h`** — `hsv565`/`color565`, `hueDelta`/`relatedHue`/`mutateHue`/`blendHue`,
  and `NeighborMix` mixing (`addNeighbor`/`mixedHue`/`mixedSaturation`).
- **`life_render.h`** — `targetColorFor` (per-cell target HSV) + `renderFrame` (approach +
  dirty draw). Note: `renderFrame` currently scans **every** cell each frame. Also defines
  `copyDrawnFrame` (S3 only): packs the active panel's `drawnColor` RGB565 image, de-strided
  from `kMaxWidth` to a tight `panelWidth*panelHeight`, for the core-0 web board stream.
- **`life_burn.h`** — the burn-wave state machine
  (`startBurnWave`/`stepBurnWave`/`finishBurnWave`, `clearBurnHeat`).
- **`life_input.h`** — `pollAccelerometer`, `updateTiltState`, knock/shake detection
  (`recordKnockOrigin`), `initAccelerometer`, `decayMotionEffects`.
- **`life_spawn.h`** — pattern spawning (`addGlider`/`addBurst`/`addVoid`/`addMotionBurst`),
  tilt mapping, and event application (`applyInteractionEvents` / `applyRandomEvents`) onto
  the `next*` buffers.
- **`life_sim.h`** — `configureLifeBounds`, `seedLife`, density throttling,
  `commitNextGeneration` (`next*` → `cell*` + ages), and `stepLife` (uses the bit-parallel
  `conwayNextRow` from `life_bits.h` for the per-row birth/survival mask, then fills
  colour/type metadata). The neighbour/rule math is host-tested; the metadata/events around
  it are not.

Standalone / unchanged supporting files:

- **`RowBits`** (`src/life_bits.h`) — a row is two `uint64_t` (128 bits). Bitwise operators
  + `popcount64`. `bitForX`/`leftBitForX`/`rightBitForX` precompute per-column masks **with
  toroidal wrap** for the active panel width (`configureLifeBounds`). `activeMask` (from
  `activeMaskFor`) clips words to `panelWidth`. A *true* standalone header — host-tested in
  `tests/test_life_bits.cpp`.
- **Runtime knobs** (`src/life_settings.h`) — ~20 simulation parameters (spawn rates, burn
  thresholds, hue weights, etc.) in a `LifeSettings` struct generated by the
  `LIFE_SETTINGS_FIELDS` X-macro with min/max/default metadata per field. The three
  instances — `gDefaults` (factory), `gSaved` (last NVS-persisted), `gLive` (active; written
  only on core 1 via `webPortalTick()`) — are *defined* in `life_state.h`. A pure
  `static constexpr` field-metadata table (`kLifeFieldMeta` / `kLifeFieldCount` /
  `getLifeSettingByIndex` / `getLifeSettingByKey`) lets the web portal walk settings to
  build JSON without embedding ArduinoJson in this header — `life_settings.h` stays
  ArduinoJson-free and host-tested. Hardware geometry (`MATRIX_WIDTH`, `MATRIX_BIT_DEPTH`,
  `MATRIX_TILE`, `MATRIX_RGB_CHAINS`) stays compile-time; shown read-only in the panel.
  Host-tested in `tests/test_life_settings.cpp`.
- **`benchmark.h`** — the synthetic display benchmark (its own `setup()`/`loop()` and timing
  helpers), included by `main.cpp` only in the `#else` of the `MATRIX_BENCHMARK` gate.
- **Web portal** (`src/web_portal.cpp`, `src/web_ui.h`, S3 Life only) — BLE WiFi
  provisioning (`PROV_MatrixLife`, PoP `matrixlife`), mDNS (`matrixportal.local`), an
  `AsyncWebServer` on port 80 with two routes only: `GET /` (HTML) and `/ws`
  (`AsyncWebSocket`). There is no REST API and no client polling. Control traffic (schema,
  stats, `set`/`action`) is JSON crafted/parsed with **ArduinoJson v7** on WS **text**
  frames; the live board is streamed on WS **binary** frames (a 6-byte little-endian header
  — magic `'L'`, version, width, height — then `width*height` row-major RGB565 pixels), so
  text and binary never collide on the one socket. The browser expands 565→888 onto a
  `<canvas>`. The reverse direction reuses that binary lane: the browser lets you **draw**
  cells (paint on a transparent overlay `<canvas>` stacked over the board, batched and sent
  on pointer-up) as a second binary frame type — magic `'D'`, same 6-byte header, then a
  tight row-major LSB-first bitmask of cells to bring alive — parsed by `dispatchDrawFrame`
  and injected via `applyDrawnCells` (see the two-core hand-off above). The portal walks
  `kLifeFieldMeta` from `life_settings.h` to build settings JSON. A core-0 `wsPushTask` broadcasts the board (~every 100 ms, via a single ref-counted
  `makeBuffer`/`binaryAll`) and stats (~every 500 ms) under one `availableForWriteAll()`
  backpressure gate, and calls `cleanupClients`. Incoming WS edits and actions are staged into
  `gPending` (under `gSettingsMux`); `webPortalTick()` (called from the core-1 loop)
  promotes them into `gLive` and handles deferred actions (reseed, burn, forget-wifi
  reboot). NVS persistence via `settingsLoadNvs` / `settingsSaveNvs`. New deps in
  `platformio.ini` S3 env: `esp32async/ESPAsyncWebServer`, `esp32async/AsyncTCP`,
  `bblanchon/ArduinoJson@^7`.

## Conventions

- `constexpr` k-prefixed constants for all tuning knobs; `camelCase` functions/vars.
- Toroidal (wrap-around) topology everywhere — preserve it in any simulation change.
- Keep it warning-clean under `-O3` (set in `platformio.ini`).
