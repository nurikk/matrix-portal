#pragma once
#include <stdint.h>

// Standalone 64x64 RGB565 scene renderer; the clock scales this canvas to the panel.
enum class RetroScene : uint8_t { Mario, PacMan, Kirby, MegaMan, DuckHunt, Count };
constexpr uint16_t kRetroSceneDurationMs = 7000;
constexpr uint8_t kRetroCanvasSize = 64;
constexpr uint16_t kRetroPalette[] = {
    0x0000, 0xF800, 0xFDCA, 0x9A43, 0xFFFF, 0xFFE0, 0x049F, 0x2EFF,
    0xFC9B, 0xF98B, 0x07E0, 0x02A0, 0xFD20, 0xA81F, 0x39E7, 0xFFE9};

struct RetroCanvas {
  uint16_t pixels[kRetroCanvasSize * kRetroCanvasSize] = {};

  void clear() {
    for (uint16_t &pixel : pixels) pixel = 0;
  }
  void pixel(int x, int y, uint8_t color) {
    if (x >= 0 && x < 64 && y >= 0 && y < 64) pixels[y * 64 + x] = kRetroPalette[color];
  }
  void rect(int x, int y, int w, int h, uint8_t color) {
    for (int dy = 0; dy < h; ++dy)
      for (int dx = 0; dx < w; ++dx) pixel(x + dx, y + dy, color);
  }
  template <unsigned H, unsigned W>
  void sprite(int x, int y, const char (&rows)[H][W], bool flip = false, uint8_t ink = 0) {
    for (unsigned dy = 0; dy < H; ++dy) {
      for (unsigned dx = 0; dx < W - 1; ++dx) {
        char c = rows[dy][dx];
        if (c == '.') continue;
        uint8_t color = c <= '9' ? c - '0' : c - 'A' + 10;
        pixel(x + (flip ? W - 2 - dx : dx), y + dy, ink && c == '1' ? ink : color);
      }
    }
  }
  uint16_t panelPixel(uint8_t x, uint8_t y, uint8_t width, uint8_t height) const {
    uint16_t size = width < height ? width : height;
    int px = x - (width - size) / 2;
    int py = y - (height - size) / 2;
    if (px < 0 || py < 0 || px >= size || py >= size) return 0;
    return pixels[(py * 64 / size) * 64 + px * 64 / size];
  }
};

constexpr char kRetroMario[][17] = {
    ".....11111......", "....111111111...", "....3332202.....", "...3232220222...",
    "...32332220222..", "...3322220000...", ".....2222222....", "....116116......",
    "...1116116111...", "..111166661111..", "..221656656122..", "..222666666222..",
    "....66666666....", "....666..666....", "...333....333...", "..3333....3333.."};
constexpr char kRetroMarioRun[][17] = {
    ".....11111......", "....111111111...", "....3332202.....", "...3232220222...",
    "...32332220222..", "...3322220000...", ".....2222222....", "....116116..22..",
    "..221161161222..", "..22216666111...", "...11656656.....", ".....6666666....",
    "....6666.66633..", "...666....3333..", "...333..........", "....333........."};
constexpr char kRetroBlock[][13] = {
    "333333333333", "3CCCCCCCCCC3", "3C55555555C3", "3C55333555C3",
    "3C53225355C3", "3C55523555C3", "3C55235555C3", "3C55225555C3",
    "3C55555555C3", "3C55225555C3", "3CCCCCCCCCC3", "333333333333"};
constexpr char kRetroStar[][10] = {
    "....5....", "...555...", "...555...", "555555555", ".5550555.",
    "..55555..", "..55555..", ".555.555.", ".55...55."};
constexpr char kRetroGhost[][11] = {
    "...1111...", "..111111..", ".11111111.", "1114414411", "1114014011",
    "1114414411", "1111111111", "1111111111", "11.1111.11", "1...11...1"};
constexpr char kRetroScaredGhost[][11] = {
    "...6666...", "..666666..", ".66666666.", "6664664666", "6666666666",
    "6646464666", "6464646466", "6666666666", "66.6666.66", "6...66...6"};
constexpr char kRetroKirby[][23] = {
    "........999999........", "......9988888899......", ".....988888888889.....",
    "....98888888888889....", "...9888888888888889...", "...9888888888888889...",
    "..988888884884888889..", ".99888888048048888899.", "9888888806806888888889",
    "9888888800800888888889", "9888888888888888888889", ".99899888888888998999.",
    "..988998880088899889..", "...9888888998888889...", "....99888888888899....",
    "....11998888889911....", "...1111199999911111...", "..1111111....1111111..",
    "..1111111....1111111..", "...11111......11111..."};
constexpr char kRetroMegaMan[][19] = {
    "......666666......", ".....66777766.....", "....6677777766....", "....6776677776....",
    "....6776677776....", "....6622244226....", ".....622204226....", ".....66222266.....",
    "...666677776666...", "..67766777766776..", ".6777667777667776.", ".6777666666667776.",
    "..66666777766666..", ".....66777766.....", "....6677..7766....", "...66776..67766...",
    "..677776..677776..", "..666666..666666.."};
constexpr char kRetroDuckUp[][21] = {
    "..33................", ".3333...............", ".34433..............", "..34433......AAAA...",
    "...34433....A0AAA55.", "....34433...AAAA5555", ".....34433444AA.....", "......334444433.....",
    "...3333344443333....", "..33333333333333....", "...333333333333.....", ".....33333333.......",
    ".......55.55........"};
constexpr char kRetroDuckDown[][21] = {
    "....................", "....................", "....................", ".............AAAA...",
    "............A0AAA55.", "............AAAA5555", ".........3444AA.....", ".......33444433.....",
    "...3333344443333....", "..33333344433333....", "...333344433333.....", ".....34443333.......",
    "....34433.55........", "...34433............", "..33333.............", "...333.............."};
constexpr char kRetroDog[][25] = {
    "....3333........3333....", "...300033......330003...", "..30000333333333000003..",
    "..300033CCCCCC33000003..", "..3003CCCCCCCCCC300003..", "..3003CC00CC00CC300003..",
    "..3003C000CC000C300003..", "...333CCCCCCCCCC33333...", ".....3CCCCCCCCCC3.......",
    "....3CCCC0000CCCC3......", "....3CCFF0000FFCC3......", "....3CFFFFFFFFFFC3......",
    ".....3F00000000F3.......", ".....3FF444444FF3.......", "......3FFFFFFFF3........",
    ".....CC33333333CC.......", "....CCCCCCCCCCCCCC......", "...CCCCCCCCCCCCCCCC.....",
    "...CCCFFCCCCCCFFCCC.....", "....CFFFFCCCCFFFFC......"};

inline void renderRetroScene(RetroCanvas &canvas, RetroScene scene, uint32_t elapsedMs) {
  uint32_t t = elapsedMs < kRetroSceneDurationMs ? elapsedMs : kRetroSceneDurationMs - 1;
  canvas.clear();
  switch (scene) {
  case RetroScene::Mario: {
    canvas.rect(0, 56, 64, 2, 5);
    canvas.rect(0, 58, 64, 6, 3);
    for (int x = 0; x < 64; x += 8) {
      canvas.rect(x, 58, 1, 6, 12);
      canvas.rect(x, 61, 8, 1, 12);
    }
    int bump = t >= 2800 && t < 3100 ? 2 : 0;
    if (t < 2800) canvas.sprite(29, 17, kRetroBlock);
    else {
      canvas.rect(29, 17 - bump, 12, 12, 3);
      canvas.rect(30, 18 - bump, 10, 10, 12);
      for (int x = 31; x <= 38; x += 7)
        for (int y = 19; y <= 26; y += 7) canvas.pixel(x, y - bump, 3);
    }
    int x = t < 2100 ? 2 + t * 23 / 2100 : t < 3800 ? 25 : 25 + (t - 3800) * 22 / 3200;
    int jump = 0;
    if (t >= 2100 && t < 3500) {
      int u = t - 2100;
      jump = 4 * 11 * u * (1400 - u) / (1400 * 1400);
    }
    canvas.sprite(x, 40 - jump, (t / 140) % 2 ? kRetroMarioRun : kRetroMario);
    if (t >= 2800 && t < 4300) {
      int u = t - 2800;
      int rise = 4 * 10 * u * (1500 - u) / (1500 * 1500);
      int w = (t / 100) % 4 == 0 ? 1 : (t / 100) % 4 == 2 ? 5 : 3;
      canvas.rect(35 - w / 2, 12 - rise, w, 8, 12);
      canvas.rect(35 - w / 2, 13 - rise, w, 6, 5);
      canvas.rect(35, 14 - rise, 1, 4, 15);
    }
    break;
  }
  case RetroScene::PacMan: {
    canvas.rect(0, 20, 64, 1, 6); canvas.rect(0, 42, 64, 1, 6);
    canvas.rect(0, 18, 64, 1, 7); canvas.rect(0, 44, 64, 1, 7);
    bool powered = t >= 3000;
    int pacX = powered ? 56 - (t - 3000) * 16 / 4000 : 48 + t * 8 / 3000;
    if (!powered) {
      for (int x = pacX + 8; x < 56; x += 5) canvas.pixel(x, 31, 15);
      canvas.rect(57, 29, 4, 4, 15);
    }
    int mouth = (t / 100) % 3;
    for (int y = -5; y <= 5; ++y) for (int x = -5; x <= 5; ++x) {
      int forward = powered ? -x : x;
      int ay = y < 0 ? -y : y;
      if (x * x + y * y <= 28 && !(forward >= 0 && ay <= forward * mouth / 2))
        canvas.pixel(pacX + x, 31 + y, 5);
    }
    const uint8_t ghostColors[] = {1, 8, 7, 12};
    for (int i = 0; i < 4; ++i) {
      int gx = pacX - 12 - i * 11;
      int gy = 26 + ((t / 160 + i) % 2);
      if (powered) canvas.sprite(gx, gy, kRetroScaredGhost);
      else canvas.sprite(gx, gy, kRetroGhost, false, ghostColors[i]);
    }
    break;
  }
  case RetroScene::Kirby: {
    int beat = (t / 280) % 4;
    int lift = beat == 1 || beat == 3 ? 4 : 0;
    int sway = beat == 0 ? -3 : beat == 2 ? 3 : 0;
    canvas.sprite(21 + sway, 30 - lift, kRetroKirby, beat >= 2);
    if (beat & 1) {
      canvas.rect(18 + sway, 33 - lift, 3, 5, 8);
      canvas.rect(43 + sway, 31 - lift, 3, 5, 8);
    }
    canvas.sprite(6, 13 + (beat & 1) * 2, kRetroStar);
    canvas.sprite(46, 17 - (beat & 1) * 2, kRetroStar);
    canvas.sprite(27 + sway, 5, kRetroStar);
    canvas.rect(18, 53, 29, 1, 13);
    break;
  }
  case RetroScene::MegaMan: {
    canvas.rect(0, 55, 64, 2, 7);
    canvas.rect(0, 57, 64, 7, 6);
    for (int x = 0; x < 64; x += 8) canvas.rect(x, 59, 6, 2, 14);
    int x = t < 1700 ? 9 : t < 3700 ? 9 + (t - 1700) * 20 / 2000 : 29;
    if (t < 1000) {
      int bottom = 8 + t * 47 / 1000;
      canvas.rect(15, 0, 6, bottom, 6);
      canvas.rect(17, 0, 2, bottom, 4);
      canvas.rect(12, bottom - 2, 12, 2, 7);
    } else {
      int bounce = t >= 1700 && t < 3700 ? (t / 130) % 2 : 0;
      canvas.sprite(x, 37 - bounce, kRetroMegaMan);
      if (t < 1500) {
        for (int y = 0; y < 55; y += 5) canvas.rect(x + 8, y, 2, 2, 7);
      }
      if (t >= 1700 && t < 3700 && (t / 130) % 2) {
        canvas.rect(x + 2, 52, 6, 3, 0);
        canvas.rect(x, 50, 7, 3, 7);
      }
      if (t >= 3900 && t < 6000) {
        canvas.rect(x + 14, 45, 9, 5, 6);
        canvas.rect(x + 20, 46, 3, 3, 7);
        for (int i = 0; i < 3; ++i) {
          int px = x + 24 + ((t - 3900 + i * 230) % 700) * 40 / 700;
          canvas.rect(px, 46, 3, 2, 5);
        }
      }
    }
    break;
  }
  case RetroScene::DuckHunt: {
    if (t < 4200) {
      int x = 4 + t * 64 / 4200;
      int y = 25 - t * 17 / 4200 + ((t / 300) % 2);
      if ((t / 180) % 2) canvas.sprite(x, y, kRetroDuckUp);
      else canvas.sprite(x, y, kRetroDuckDown);
    }
    if (t >= 4400) {
      int rise = t < 5200 ? (t - 4400) * 20 / 800 : 20;
      int laugh = t >= 5200 ? (t / 160) % 2 : 0;
      canvas.sprite(20, 57 - rise + laugh, kRetroDog);
      if (laugh) canvas.rect(29, 50, 6, 2, 0);
    }
    canvas.rect(0, 57, 64, 7, 11);
    for (int x = 0; x < 64; x += 3) {
      canvas.rect(x, 54 + x % 3, 1, 7, 10);
      canvas.pixel(x + 1, 56, 10);
    }
    break;
  }
  case RetroScene::Count: break;
  }
}
