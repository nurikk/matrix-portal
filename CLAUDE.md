# CLAUDE.md

Guidance for working in this repo. See `README.md` for the user-facing overview.

## What this is

Arduino/PlatformIO firmware for Adafruit MatrixPortal boards (ESP32-S3 and SAMD51 M4)
that runs a colored, accelerometer-interactive Conway's Game of Life on a HUB75 LED
matrix via Adafruit Protomatter. The entire firmware is one file: `src/main.cpp`.

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
`MATRIX_BENCHMARK`. There is no runtime configuration yet — every tuning change requires a
rebuild + reflash.

## Host tests (no board)

The pure simulation core is in `src/life_bits.h` and is unit-tested on the host:

```sh
sh tests/run.sh        # compiles & runs tests/test_life_bits.cpp with clang++
```

`tests/test_life_bits.cpp` checks the bitwise `computeConwayNext`/`conwayNextRow` against
an independent cell-by-cell Conway oracle on random boards at widths that straddle the
64-bit word boundary (63/64/65/127/128), plus blinker/block/glider patterns. Run this after
any change to the simulation's neighbour/rule math. (`tests/` is intentionally *not*
PlatformIO's `test_dir`, so `pio test` won't try to run these on-target.)

## Things that will trip you up

- **Two firmwares in one file.** `MATRIX_BENCHMARK` splits `main.cpp` into the Life path
  (`#if !MATRIX_BENCHMARK`, ~line 55) and a synthetic display benchmark (`#else`, ~line
  1431). Each has its own `setup()`/`loop()`. Edits to shared helpers affect both.
- **Library-patching pre-script.** `scripts/patch_protomatter_s3_clock.py` rewrites
  `LCD_CLK_PRESCALE` inside the *downloaded* Protomatter source under `.pio/libdeps` to
  overclock the HUB75 clock. Only the `..._life_4bit_fast` env uses it. It re-applies on
  clean builds; if the macro name changes upstream it silently no-ops.
- **RAM ceiling.** Per-cell state is ~14 bytes/cell (the `cell*`/`next*`/`visual*` arrays
  near the top of the Life section). 128×128 = ~224 KB — fits the S3, **overflows the M4's
  192 KB**. M4 can run the 128×128 benchmark but not 128×128 Life.
- **Single chain only for Life.** A compile-time `#error` enforces ≤128×128, single
  RGB chain, tile in [-2, 2].
- **M4 bootloader uploads.** Use `matrixportal_m4_bootloader` when the 1200-baud touch
  fights an already-in-bootloader board.

## Code structure (src/main.cpp)

- **Board pinout** — `#if`/`#elif` on `ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3` /
  `_VARIANT_MATRIXPORTAL_M4_` (top of file).
- **`RowBits`** (in `src/life_bits.h`) — a row is two `uint64_t` (128 bits). Bitwise
  operators + `popcount64`. `bitForX` / `leftBitForX` / `rightBitForX` precompute per-column
  masks **with toroidal wrap** for the active panel width (`configureLifeBounds`).
  `activeMask` (from `activeMaskFor`) clips words to `panelWidth`.
- **Simulation** — `stepLife` uses the bit-parallel `conwayNextRow` (life_bits.h) to get a
  per-row birth/survival candidate mask, then fills colour/type metadata for the candidates;
  `commitNextGeneration` applies `next*` → `cell*` + ages; `seedLife`; plus the burn-wave
  state machine (`startBurnWave`/`stepBurnWave`/`finishBurnWave`). The neighbour/rule math
  is host-tested (see above); the metadata/events around it are not.
- **Color** — `hsv565`/`color565`, `relatedHue`/`mutateHue`/`blendHue`, `NeighborMix`.
- **Input** — `pollAccelerometer`, `updateTiltState`, knock/shake/tilt → events applied in
  `applyInteractionEvents` / `applyRandomEvents`.
- **Rendering** — `targetColorFor` (per-cell target HSV) + `renderFrame` (approach + dirty
  draw). Note: `renderFrame` currently scans **every** cell each frame.
- **Profiling** — `reportFps` dumps timing once per second.

## Conventions

- `constexpr` k-prefixed constants for all tuning knobs; `camelCase` functions/vars.
- Toroidal (wrap-around) topology everywhere — preserve it in any simulation change.
- Keep it warning-clean under `-O3` (set in `platformio.ini`).
