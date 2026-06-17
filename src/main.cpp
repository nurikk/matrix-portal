#include <Arduino.h>
#include <Adafruit_Protomatter.h>

// MatrixPortal M4 HUB75 pinout from Adafruit Protomatter's official example.
uint8_t rgbPins[] = {7, 8, 9, 10, 11, 12};
uint8_t addrPins[] = {17, 18, 19, 20, 21};
uint8_t clockPin = 14;
uint8_t latchPin = 15;
uint8_t oePin = 16;

Adafruit_Protomatter matrix(
    64, 4,
    1, rgbPins,
    5, addrPins,
    clockPin, latchPin, oePin,
    false);

uint16_t wheel(uint8_t pos) {
  if (pos < 85) {
    return matrix.color565(pos * 3, 255 - pos * 3, 0);
  }
  if (pos < 170) {
    pos -= 85;
    return matrix.color565(255 - pos * 3, 0, pos * 3);
  }
  pos -= 170;
  return matrix.color565(0, pos * 3, 255 - pos * 3);
}

void drawTestPattern(uint8_t phase) {
  matrix.fillScreen(0);

  for (int16_t x = 0; x < matrix.width(); x++) {
    uint16_t color = wheel((x * 4 + phase) & 255);
    matrix.drawFastVLine(x, 0, matrix.height(), color);
  }

  matrix.fillRect(0, 0, 64, 9, 0);
  matrix.setCursor(1, 1);
  matrix.setTextColor(matrix.color565(255, 255, 255));
  matrix.print("MatrixPortal");

  matrix.drawRect(0, 10, 18, 18, matrix.color565(255, 0, 0));
  matrix.drawCircle(31, 19, 9, matrix.color565(0, 255, 0));
  matrix.drawTriangle(46, 28, 55, 10, 63, 28, matrix.color565(0, 0, 255));

  matrix.show();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  ProtomatterStatus status = matrix.begin();
  Serial.print("Protomatter begin status: ");
  Serial.println(static_cast<int>(status));

  if (status != PROTOMATTER_OK) {
    while (true) {
      delay(1000);
    }
  }
}

void loop() {
  static uint8_t phase = 0;

  drawTestPattern(phase++);
  Serial.print("Refresh FPS: ");
  Serial.println(matrix.getFrameCount());
  delay(30);
}
