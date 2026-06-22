# MatrixPortal 128x128 / Four 64x64 Benchmark Notes

Date: 2026-06-19

## Summary

The current firmware is tuned for a 64x64 HUB75 matrix. Running a true 128x128 version on the MatrixPortal M4 is possible for display refresh tests, but the current Life implementation cannot simply scale to 128x128 because its per-cell state arrays would exceed available RAM.

Best practical direction for 128x128 on MatrixPortal S3 or M4: use 3-bit Protomatter color depth, reduce or pack per-cell state, and avoid full-rate full-frame simulation work.

## Optimization Results (2026-06-20, MatrixPortal S3)

Two changes to the production Life path, each measured with a deterministic A/B
(fixed RNG seed + accelerometer disabled, so both builds evolve bit-identically;
verified by matched live-cell counts at matched generations). Times are the
steady-state mean of the per-second profiler, in microseconds.

1. **Bitwise simulation** (`src/life_bits.h`): neighbour counting via bit-parallel
   SWAR over the two-word `RowBits` rows instead of 8 bit-tests per cell.
2. **Render skip**: settled black cells (dead, fully faded, not forced to redraw) are
   skipped before the per-cell colour work in `renderFrame`. Output-identical --
   those cells never produced a `drawPixel`.

| Stage  | Grid    | Before  | After   | Speedup |
| ------ | ------- | ------: | ------: | ------: |
| `life` (sim)    | 64x64   |  7,218  |  2,771  | 2.6x |
| `life` (sim)    | 128x128 | 28,349  | 10,145  | 2.8x |
| `render` (loop) | 64x64   |  3,867  |  1,274  | 3.0x |
| `render` (loop) | 128x128 | 15,257  |  5,167  | 3.0x |

Combined per-step compute (sim + render, excluding `matrix.show()`):

- 64x64:   ~11.1 ms -> ~4.0 ms
- 128x128: ~43.6 ms -> ~15.3 ms

After these changes the remaining 128x128 per-frame display cost is `render`
(~5.2 ms) + `matrix.show()` (~6.1 ms, Protomatter canvas->bitplane conversion,
library code). The sim (~10.1 ms) runs at the `kLifeStepMs` cadence (~10 Hz), not
per frame. Further gains would come from an active-cell list for `render` or
reducing `show()` cost, both higher-effort / lower-return.

## Hardware And Firmware Context

- Board: Adafruit MatrixPortal M4, SAMD51J19A, 120 MHz, 192 KB RAM.
- Board: Adafruit MatrixPortal ESP32-S3, ESP32-S3, 240 MHz, 320 KB RAM, 2 MB PSRAM.
- Driver: Adafruit Protomatter.
- Current source default: 64x64, 5-bit color depth.
- Four-panel benchmark target: four 64x64 panels as a 128x128 canvas via `MATRIX_WIDTH=128` and `MATRIX_TILE=2`.
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
Life FPS: 46 | Refresh FPS: 221 | live: 353 | changed: 279 | pixels: 680 | events: 5 | motion: 164 | knocks: 0 | shakes: 0 | tilt: 255 | gen: 584 | avg us loop/step/sim/render/show/accel: 21507/20276/10609/6355/3311/1230 | max us loop/life/render/show: 22992/20596/6488/3331
```

## 128x128 Synthetic Benchmark Results

The benchmark firmware draws a dim animated full-frame pattern and calls `matrix.show()` every frame. This isolates display/canvas update costs without the full Life simulation.

### MatrixPortal S3, 128x128 / Four 64x64, 3-bit Color

- App FPS: 90 FPS.
- Protomatter refresh: 154-155 Hz.
- Average draw time: about 5.6 ms.
- Average `matrix.show()` time: about 5.5 ms.
- Average full loop: about 11.1 ms.

Example output:

```text
Benchmark 128x128 @ 3 bit | app FPS: 90 | Refresh FPS: 154 | avg us draw/show/loop: 5619/5463/11082 | max us draw/show/loop: 5652/5508/11119
```

### MatrixPortal S3, 128x128 / Four 64x64, 4-bit Color

- App FPS: 92 FPS.
- Protomatter refresh: 77-78 Hz.
- Average draw time: about 4.7 ms.
- Average `matrix.show()` time: about 6.1 ms.
- Average full loop: about 10.8 ms.

Example output:

```text
Benchmark 128x128 @ 4 bit | app FPS: 92 | Refresh FPS: 78 | avg us draw/show/loop: 4690/6083/10774 | max us draw/show/loop: 4774/6136/10858
```

### MatrixPortal S3, 128x128 / Four 64x64, 5-bit Color

- App FPS: 90 FPS.
- Protomatter refresh: 38-39 Hz.
- Average draw time: about 4.2 ms.
- Average `matrix.show()` time: about 6.8 ms.
- Average full loop: about 11.1 ms.

Example output:

```text
Benchmark 128x128 @ 5 bit | app FPS: 90 | Refresh FPS: 39 | avg us draw/show/loop: 4247/6820/11068 | max us draw/show/loop: 4289/6908/11147
```

### MatrixPortal S3, 128x128 / Four 64x64, Production Life, 4-bit, 20 MHz Clock

- Life UPS: 8-9 UPS after initial seed settles.
- Render FPS: 24-27 FPS.
- Protomatter refresh: 82-83 Hz.
- Average production Life step time: about 26-33 ms.
- Average production render time: about 15.5-18.7 ms.
- Average `matrix.show()` time: about 6.2 ms.
- This runs the normal production Life path, not benchmark firmware.

Example output:

```text
Life UPS: 8 | Render FPS: 27 | Refresh FPS: 82 | live: 568 | changed: 108 | pixels: 255 | events: 3 | motion: 0 | knocks: 0 | shakes: 0 | tilt: 231 | gen: 79 | avg us loop/life/render/show/accel: 7024/26506/15579/6200/440 | max us loop/life/render/show: 48542/26642/15781/6250
```

### MatrixPortal M4, 128x128, 3-bit Color

- App FPS: 32 FPS.
- Protomatter refresh: 224-225 Hz.
- Average draw time: about 16.5 ms.
- Average `matrix.show()` time: about 14.0 ms.
- Average full loop: about 30.5 ms.

Example output:

```text
Benchmark 128x128 @ 3 bit | app FPS: 32 | Refresh FPS: 224 | avg us draw/show/loop: 16476/13985/30462 | max us draw/show/loop: 16507/14011/30498
```

### MatrixPortal M4, 128x128, 4-bit Color

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

- On MatrixPortal S3, 3-bit color is the only tested 4-panel setting that gives comfortable refresh headroom at about 154-155 Hz.
- On MatrixPortal S3, 4-bit color can update the app frame at about 92 FPS, but the underlying panel refresh is only about 77-78 Hz.
- On MatrixPortal S3, 5-bit color is not recommended for four 64x64 panels because the matrix refresh drops to about 38-39 Hz.
- 3-bit 128x128 is the better anti-lag/anti-flicker setting on MatrixPortal M4 because it kept refresh around 224 Hz.
- 4-bit 128x128 can animate, but refresh around 110 Hz is marginal and more likely to flicker or lose USB/power margin.
- A first full-bright 128x128 4-bit benchmark caused USB serial to drop. A dimmed benchmark stayed stable, which points to power/brownout or very tight interrupt/USB headroom.
- The current visual state model scales poorly in RAM. At 128x128, the per-cell arrays alone would be about 229 KB before the GFX canvas and Protomatter buffers, which exceeds the M4's 192 KB RAM.

## Recommendations For 128x128 Without Lag

- Default to 3-bit color depth for 128x128 on MatrixPortal M4.
- Keep panel brightness/current conservative; avoid full-bright all-pixel test patterns unless power is known-good.
- Keep the two-word row bitset for production 128x128 Life on S3.
- Pack cell metadata aggressively. Avoid separate 1-byte arrays for every visual field at 128x128.
- Consider simulating at a lower rate than display refresh, for example 15-20 Life generations per second while rendering interpolated fades at display frame rate.
- Consider a 64x64 simulation scaled 2x if visual density is acceptable; this gives 128x128 output without 4x simulation cost.
- Keep dirty-pixel updates, but remember `matrix.show()` still converts the full canvas to Protomatter format.

## Useful Commands

Build normal MatrixPortal S3 firmware:

```sh
pio run -e matrixportal_s3
```

Upload normal MatrixPortal S3 firmware:

```sh
pio run -e matrixportal_s3 -t upload --upload-port /dev/cu.usbmodem2021301
```

Build normal MatrixPortal M4 firmware:

```sh
pio run -e matrixportal_m4
```

Build and upload MatrixPortal S3 four-panel 3-bit benchmark:

```sh
pio run -e matrixportal_s3_4x64_bench_3bit -t upload --upload-port /dev/cu.usbmodem2021301
```

Build and upload MatrixPortal S3 four-panel 4-bit benchmark:

```sh
pio run -e matrixportal_s3_4x64_bench_4bit -t upload --upload-port /dev/cu.usbmodem2021301
```

Build and upload MatrixPortal S3 four-panel 5-bit benchmark:

```sh
pio run -e matrixportal_s3_4x64_bench_5bit -t upload --upload-port /dev/cu.usbmodem2021301
```

Build and upload MatrixPortal S3 four-panel production Life with 20 MHz HUB75 clock:

```sh
pio run -e matrixportal_s3_4x64_life_4bit_fast -t upload --upload-port /dev/cu.usbmodem2021301
```

Upload normal MatrixPortal M4 firmware:

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

## Environments Added

- `matrixportal_s3_4x64_bench_3bit`
- `matrixportal_s3_4x64_bench_4bit`
- `matrixportal_s3_4x64_bench_5bit`
- `matrixportal_s3_4x64_life_4bit_fast`
- `matrixportal_m4_128_bench_3bit`
- `matrixportal_m4_128_bench_4bit`
- `matrixportal_m4_bootloader`
