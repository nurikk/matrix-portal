# MatrixPortal Game of Life

A colorful, interactive Conway's Game of Life for Adafruit **MatrixPortal** boards
driving HUB75 RGB LED matrices. Cells belong to six species with distinct hues,
blend colors with their neighbors as they breed, shimmer with HSV waves, and react
to the board's onboard accelerometer.

> _TODO: drop a photo or GIF of the panel here — it sells the project far better than words._

## Features

- **Multi-species colored Life** — six cell types (`kTypeCount`), each with a base hue.
  New cells inherit a blend of their neighbors' colors, with occasional mutations,
  complements, and triads (`relatedHue` / `mutateHue`).
- **Smooth rendering** — the simulation steps at ~10 Hz while the display renders at
  ~30 Hz, interpolating hue/saturation/value toward each cell's target so transitions
  fade instead of snapping (`renderFrame`, `approachHue`).
- **Dirty-pixel updates** — only pixels whose color actually changed are pushed to the
  panel.
- **Accelerometer interaction** (onboard LIS3DH):
  - **Tilt** steers where new life spawns and biases the palette.
  - **Shake** scatters gliders and bursts.
  - **Knock / double-tap** triggers a radial **burn wave** that scorches the board and
    reseeds it.
- **Self-stabilizing** — injects gliders, bursts, and voids when the board stagnates or
  the live-cell count drops too low, then reseeds entirely if it dies out.
- **Built-in benchmark mode** — a separate firmware path (`MATRIX_BENCHMARK`) that draws a
  full-frame animated pattern to isolate display/`matrix.show()` cost from simulation cost.
- **Serial profiling** — per-stage microsecond timing (loop / life / render / show / accel)
  plus live/changed/updated pixel counts at 115200 baud.

## Hardware

- **Board:** Adafruit MatrixPortal **ESP32-S3** (240 MHz, 320 KB RAM + 2 MB PSRAM) _or_
  MatrixPortal **M4** (SAMD51, 120 MHz, 192 KB RAM).
- **Display:** one 64×64 HUB75 panel by default; up to a 128×128 single-chain tiled
  arrangement (four 64×64 panels) on the S3.
- **Accelerometer:** onboard LIS3DH (auto-detected at I²C `0x19` or `0x18`; the firmware
  runs fine without it).

HUB75 pin assignments for each board live at the top of [`src/main.cpp`](src/main.cpp)
and come from Adafruit Protomatter's official examples.

> **RAM note:** the per-cell visual state is ~14 bytes/cell, so a 128×128 board needs
> ~224 KB just for cell arrays. That fits the S3 but **exceeds the M4's 192 KB** — the M4
> can run the 128×128 *benchmark* but not the full 128×128 Life. See
> [`benchmark.md`](benchmark.md) for the measurements behind this.

## Quick start

This repo uses [PlatformIO](https://platformio.org/). The included `pyproject.toml`
pins PlatformIO via [uv](https://docs.astral.sh/uv/), so you can either use a global
`pio` or run it from a local environment:

```sh
uv sync          # installs PlatformIO into .venv (optional)
```

Build and upload the default S3 firmware (replace the port with yours):

```sh
pio run -e matrixportal_s3
pio run -e matrixportal_s3 -t upload --upload-port /dev/cu.usbmodem2021301
pio device monitor -b 115200          # watch the profiler output
```

Build for the M4 instead:

```sh
pio run -e matrixportal_m4 -t upload --upload-port /dev/cu.usbmodem2021301
```

## Build configuration

Behavior is selected at compile time through `build_flags` (see
[`platformio.ini`](platformio.ini)):

| Flag                 | Default | Meaning                                                        |
| -------------------- | ------- | -------------------------------------------------------------- |
| `MATRIX_WIDTH`       | `64`    | Canvas width in pixels (≤ 128).                                |
| `MATRIX_BIT_DEPTH`   | `5`     | Protomatter color depth (bits per channel). Lower = faster refresh. |
| `MATRIX_RGB_CHAINS`  | `1`     | Number of chained HUB75 outputs (must be 1 for the Life path). |
| `MATRIX_TILE`        | `1`     | Vertical tiling factor; `2` stacks two 64-high panels for 128 rows. |
| `MATRIX_BENCHMARK`   | `0`     | `1` builds the benchmark firmware instead of the Life firmware. |

### Predefined environments

| Environment                          | What it builds                                                  |
| ------------------------------------ | -------------------------------------------------------------- |
| `matrixportal_s3`                    | Default 64×64 Life firmware (S3).                              |
| `matrixportal_s3_4x64_life_4bit_fast`| 128×128 Life, 4-bit, **overclocked HUB75 clock** (see below). |
| `matrixportal_s3_4x64_bench_{3,4,5}bit` | 128×128 display benchmarks at 3/4/5-bit color (S3).        |
| `matrixportal_m4`                    | Default 64×64 Life firmware (M4).                             |
| `matrixportal_m4_bootloader`        | M4 upload variant that skips the 1200-baud touch (see below).|
| `matrixportal_m4_128_bench_{3,4}bit` | 128×128 display benchmarks (M4).                              |

### HUB75 overclock patch

`matrixportal_s3_4x64_life_4bit_fast` runs
[`scripts/patch_protomatter_s3_clock.py`](scripts/patch_protomatter_s3_clock.py) as a
PlatformIO `pre:` script. It rewrites Protomatter's `LCD_CLK_PRESCALE` in the downloaded
library source to push the HUB75 pixel clock faster (controlled by
`custom_protomatter_s3_lcd_clk_prescale`). This **mutates a file inside `.pio/libdeps`**,
so it re-applies on a clean build and is reverted by `pio run -t clean` + re-fetch.

### M4 upload gotcha

If the M4 is already in bootloader mode and PlatformIO's 1200-baud touch is fighting the
bootloader port, upload with `matrixportal_m4_bootloader`, which disables the touch.

## WiFi control panel (ESP32-S3)

S3 Life builds include a browser-based control panel. M4 and all benchmark builds have
no networking code. The control panel and API are unauthenticated and intended for use on a trusted home LAN only.

**First-boot WiFi provisioning** uses Espressif's WiFiProv over BLE. Install the
**ESP BLE Provisioning** app (iOS or Android), scan for `PROV_MatrixLife`, and enter
proof-of-possession `matrixlife`. Credentials are written to NVS and never hardcoded;
BT memory is freed automatically after provisioning. To re-provision, press the
**Forget WiFi** button in the control panel (sends `{"type":"action","action":"forget"}`
over the WebSocket, which erases NVS credentials and reboots the device).

**Reaching the panel:** after connecting, the device advertises mDNS as
`matrixportal.local` and scrolls its IP address across the panel once. Open
`http://matrixportal.local/` (plain HTTP, port 80) in any browser on the same network (or use the scrolled IP
directly).

**Live-preview + Save model:** every slider move applies the new value immediately so
you can see the effect in real time. Changes are ephemeral until you press **Save**,
which persists them to NVS. **Revert** restores the last saved values; **Reset**
restores factory defaults. **Reseed** and **Trigger burn** are instant actions.
Hardware geometry (`MATRIX_WIDTH`, `MATRIX_BIT_DEPTH`, etc.) stays compile-time and is
shown read-only in the panel.

**Partition note:** `[env:matrixportal_s3]` uses `board_build.partitions = huge_app.csv`
to accommodate BLE + WiFi + application code. The S3 binary occupies roughly 48% of the
3 MB app partition.

## Development

The simulation's neighbour-counting and Conway rule live in a dependency-free header
([`src/life_bits.h`](src/life_bits.h)) so they can be unit-tested on your machine — no
board required:

```sh
sh tests/run.sh
```

These tests check the bit-parallel core against an independent cell-by-cell oracle across
toroidal widths that cross the 64-bit word boundary, plus the classic blinker/block/glider
patterns. Run them after touching the simulation math.

## Project layout

```
src/main.cpp           # Life simulation, rendering, input, and benchmark path
src/life_bits.h        # bit-parallel Conway core (host-testable, no Arduino deps)
src/life_settings.h    # runtime-tunable knobs struct (X-macro, host-testable)
src/web_portal.cpp     # WiFi provisioning, web server, NVS persistence (S3 only)
src/web_ui.h           # self-contained HTML control panel (S3 only)
platformio.ini         # board + build-flag environments
scripts/               # PlatformIO pre-build scripts (Protomatter clock patch)
tests/                 # host unit tests (clang++, no board required)
benchmark.md           # measured FPS / refresh / timing across boards, sizes, color depths
wf4_driver_design.md   # design notes for a from-scratch 4-output Huidu HD-WF4 driver
```

## Further reading

- [`benchmark.md`](benchmark.md) — empirical performance data and 128×128 scaling findings.
- [`wf4_driver_design.md`](wf4_driver_design.md) — a separate ESP32-S3 LCD_CAM + RMT driver
  design for driving four independent panels from a Huidu HD-WF4.
