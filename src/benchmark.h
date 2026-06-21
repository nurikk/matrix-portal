#pragma once
// benchmark.h — synthetic HUB75 display benchmark firmware (its own setup()/loop()).
// Included once by main.cpp inside the `#else` of `#if !MATRIX_BENCHMARK`.
// Uses the shared `matrix` object from the main.cpp preamble. Not a standalone TU.

uint32_t benchmarkStartedAt;
uint32_t benchmarkFrames;
uint32_t benchmarkDrawMicros;
uint32_t benchmarkShowMicros;
uint32_t benchmarkLoopMicros;
uint32_t benchmarkDrawMaxMicros;
uint32_t benchmarkShowMaxMicros;
uint32_t benchmarkLoopMaxMicros;

void addBenchmarkTiming(uint32_t &total, uint32_t &maximum, uint32_t elapsed) {
  total += elapsed;
  if (elapsed > maximum) {
    maximum = elapsed;
  }
}

void resetBenchmarkCounters() {
  benchmarkStartedAt = millis();
  benchmarkFrames = 0;
  benchmarkDrawMicros = 0;
  benchmarkShowMicros = 0;
  benchmarkLoopMicros = 0;
  benchmarkDrawMaxMicros = 0;
  benchmarkShowMaxMicros = 0;
  benchmarkLoopMaxMicros = 0;
}

uint16_t benchmarkColor(uint16_t x, uint16_t y, uint32_t frame) {
  uint16_t red = (x + frame) & 7;
  uint16_t green = ((y << 1) + frame) & 15;
  uint16_t blue = (x + y + frame * 3) & 7;
  return (red << 11) | (green << 5) | blue;
}

void drawBenchmarkFrame(uint32_t frame) {
  uint16_t width = matrix.width();
  uint16_t height = matrix.height();

  for (uint16_t y = 0; y < height; y++) {
    for (uint16_t x = 0; x < width; x++) {
      matrix.drawPixel(x, y, benchmarkColor(x, y, frame));
    }
  }
}

void reportBenchmark() {
  uint32_t now = millis();
  uint32_t elapsed = now - benchmarkStartedAt;
  if (elapsed < 1000 || benchmarkFrames == 0) {
    return;
  }

  uint32_t refreshCount = matrix.getFrameCount();
  uint32_t appFps = (benchmarkFrames * 1000UL) / elapsed;
  uint32_t refreshFps = (refreshCount * 1000UL) / elapsed;

  Serial.print("Benchmark ");
  Serial.print(matrix.width());
  Serial.print('x');
  Serial.print(matrix.height());
  Serial.print(" @ ");
  Serial.print(kMatrixBitDepth);
  Serial.print(" bit | app FPS: ");
  Serial.print(appFps);
  Serial.print(" | Refresh FPS: ");
  Serial.print(refreshFps);
  Serial.print(" | avg us draw/show/loop: ");
  Serial.print(benchmarkDrawMicros / benchmarkFrames);
  Serial.print('/');
  Serial.print(benchmarkShowMicros / benchmarkFrames);
  Serial.print('/');
  Serial.print(benchmarkLoopMicros / benchmarkFrames);
  Serial.print(" | max us draw/show/loop: ");
  Serial.print(benchmarkDrawMaxMicros);
  Serial.print('/');
  Serial.print(benchmarkShowMaxMicros);
  Serial.print('/');
  Serial.println(benchmarkLoopMaxMicros);

  resetBenchmarkCounters();
}

void setup() {
  Serial.begin(115200);
  uint32_t serialStartedAt = millis();
  while (!Serial && millis() - serialStartedAt < 5000) {
    delay(10);
  }

  ProtomatterStatus status = matrix.begin();
  Serial.print("Protomatter begin status: ");
  Serial.println(static_cast<int>(status));

  if (status != PROTOMATTER_OK) {
    while (true) {
      delay(1000);
    }
  }

  matrix.fillScreen(0);
  matrix.show();
  matrix.getFrameCount();
  resetBenchmarkCounters();

  Serial.print("Matrix benchmark: ");
  Serial.print(matrix.width());
  Serial.print('x');
  Serial.print(matrix.height());
  Serial.print(" @ ");
  Serial.print(kMatrixBitDepth);
  Serial.println(" bit");
}

void loop() {
  uint32_t loopStartedAt = micros();
  drawBenchmarkFrame(benchmarkFrames);
  uint32_t showStartedAt = micros();
  matrix.show();
  uint32_t loopEndedAt = micros();

  addBenchmarkTiming(benchmarkDrawMicros, benchmarkDrawMaxMicros,
                     showStartedAt - loopStartedAt);
  addBenchmarkTiming(benchmarkShowMicros, benchmarkShowMaxMicros,
                     loopEndedAt - showStartedAt);
  addBenchmarkTiming(benchmarkLoopMicros, benchmarkLoopMaxMicros,
                     loopEndedAt - loopStartedAt);
  benchmarkFrames++;
  reportBenchmark();
}
