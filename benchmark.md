# MatrixPortal M4 128x128 Benchmark Notes

Date: 2026-06-17

## Summary

The current firmware is tuned for a 64x64 HUB75 matrix. Running a true 128x128 version on the MatrixPortal M4 is possible for display refresh tests, but the current Life implementation cannot simply scale to 128x128 because its per-cell state arrays would exceed available RAM.

Best practical direction for 128x128 on MatrixPortal M4: use 3-bit Protomatter color depth, reduce or pack per-cell state, and avoid full-rate full-frame simulation work.

## Hardware And Firmware Context

- Board: Adafruit MatrixPortal M4, SAMD51J19A, 120 MHz, 192 KB RAM.
- Driver: Adafruit Protomatter.
- Current app target: 64x64, 4-bit color depth.
- Benchmark target: 128x128 via `MATRIX_WIDTH=128` and `MATRIX_TILE=2`.
- Serial port used during testing: `/dev/cu.usbmodem2021301`.

## Current 64x64 App Baseline

Measured from the normal firmware with added profiler output:

- Life FPS: 45-46 FPS.
- Protomatter refresh: 221-222 Hz.
- Average loop time: about 21.5 ms.
- Average Life step including render/show: about 20.3 ms.
- Estimated simulation-only time: about 10.6-10.9 ms.
- Average render time before `matrix.show()`: about 6.4-6.6 ms.
- Average `matrix.show()` conversion time: about 3.3 ms.
- Accelerometer polling overhead: about 1.2-1.3 ms average in this run.

Example profiler line:

```text
Life FPS: 46 | Refresh FPS: 221 | live: 353 | changed: 279 | pixels: 680 | events: 5 | motion: 164 | knocks: 0 | burns: 0 | shakes: 0 | tilt: 255 | gen: 584 | avg us loop/step/sim/render/show/accel: 21507/20276/10609/6355/3311/1230 | max us loop/life/render/show: 22992/20596/6488/3331
```

## 128x128 Synthetic Benchmark Results

The benchmark firmware draws a dim animated full-frame pattern and calls `matrix.show()` every frame. This isolates display/canvas update costs without the full Life simulation.

### 128x128, 3-bit Color

- App FPS: 32 FPS.
- Protomatter refresh: 224-225 Hz.
- Average draw time: about 16.5 ms.
- Average `matrix.show()` time: about 14.0 ms.
- Average full loop: about 30.5 ms.

Example output:

```text
Benchmark 128x128 @ 3 bit | app FPS: 32 | Refresh FPS: 224 | avg us draw/show/loop: 16476/13985/30462 | max us draw/show/loop: 16507/14011/30498
```

### 128x128, 4-bit Color

- App FPS: 37 FPS.
- Protomatter refresh: 110 Hz.
- Average draw time: about 12.25 ms.
- Average `matrix.show()` time: about 14.12 ms.
- Average full loop: about 26.37 ms.

Example output:

```text
Benchmark 128x128 @ 4 bit | app FPS: 37 | Refresh FPS: 110 | avg us draw/show/loop: 12250/14120/26371 | max us draw/show/loop: 12301/14165/26418
```

## Findings

- 3-bit 128x128 is the better anti-lag/anti-flicker setting on MatrixPortal M4 because it kept refresh around 224 Hz.
- 4-bit 128x128 can animate, but refresh around 110 Hz is marginal and more likely to flicker or lose USB/power margin.
- A first full-bright 128x128 4-bit benchmark caused USB serial to drop. A dimmed benchmark stayed stable, which points to power/brownout or very tight interrupt/USB headroom.
- The current app uses `uint64_t` row masks, so it cannot represent x coordinates beyond 63.
- The current visual state model scales poorly in RAM. At 128x128, the per-cell arrays alone would be about 229 KB before the GFX canvas and Protomatter buffers, which exceeds the M4's 192 KB RAM.

## Recommendations For 128x128 Without Lag

- Default to 3-bit color depth for 128x128 on MatrixPortal M4.
- Keep panel brightness/current conservative; avoid full-bright all-pixel test patterns unless power is known-good.
- Do not scale the existing 64-bit-row Life engine directly to 128x128.
- Replace `uint64_t` rows with either two 64-bit words per row or a compact byte/word bitset.
- Pack cell metadata aggressively. Avoid separate 1-byte arrays for every visual field at 128x128.
- Consider simulating at a lower rate than display refresh, for example 15-20 Life generations per second while rendering interpolated fades at display frame rate.
- Consider a 64x64 simulation scaled 2x if visual density is acceptable; this gives 128x128 output without 4x simulation cost.
- Keep dirty-pixel updates, but remember `matrix.show()` still converts the full canvas to Protomatter format.

## Useful Commands

Build normal firmware:

```sh
pio run -e matrixportal_m4
```

Upload normal firmware:

```sh
pio run -e matrixportal_m4 -t upload --upload-port /dev/cu.usbmodem2021301
```

Build and upload 128x128 3-bit benchmark:

```sh
pio run -e matrixportal_m4_128_bench_3bit -t upload --upload-port /dev/cu.usbmodem2021301
```

Build and upload 128x128 4-bit benchmark:

```sh
pio run -e matrixportal_m4_128_bench_4bit -t upload --upload-port /dev/cu.usbmodem2021301
```

If the board is already in bootloader mode and PlatformIO's 1200-baud touch is fighting the bootloader port, use the PlatformIO bootloader environment:

```sh
pio run -e matrixportal_m4_bootloader -t upload --upload-port /dev/cu.usbmodem2021301
```

## Benchmark Environments Added

- `matrixportal_m4_128_bench_3bit`
- `matrixportal_m4_128_bench_4bit`
- `matrixportal_m4_bootloader`
