// Host tests for the bitwise Game of Life core (src/life_bits.h).
//
// Build & run:
//   clang++ -std=c++17 -O2 -Wall -Wextra tests/test_life_bits.cpp -o /tmp/test_life && /tmp/test_life
//
// The bitwise core (computeConwayNext) is checked against an independent
// cell-by-cell Conway oracle on random boards at widths that straddle the
// 64-bit word boundary, plus canonical patterns (blinker, block, glider).
#include "../src/life_bits.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::printf("FAIL: %s\n", (msg));                                         \
      g_failures++;                                                             \
    }                                                                           \
  } while (0)

// Independent oracle: plain cell-by-cell Conway with toroidal wrap.
static void oracleNext(const std::vector<RowBits> &cur, std::vector<RowBits> &out,
                       int width, int height) {
  for (int y = 0; y < height; y++) {
    out[y] = RowBits(0, 0);
    for (int x = 0; x < width; x++) {
      int neighbors = 0;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          int nx = (x + dx + width) % width;
          int ny = (y + dy + height) % height;
          if (rowBitGet(cur[ny], static_cast<uint8_t>(nx))) {
            neighbors++;
          }
        }
      }
      bool alive = rowBitGet(cur[y], static_cast<uint8_t>(x));
      if (neighbors == 3 || (alive && neighbors == 2)) {
        rowBitSet(out[y], static_cast<uint8_t>(x));
      }
    }
  }
}

static bool rowsEqual(RowBits a, RowBits b, RowBits mask) {
  a &= mask;
  b &= mask;
  return a.low == b.low && a.high == b.high;
}

// Compare bitwise core vs oracle for one board; returns true if identical.
static bool matchesOracle(const std::vector<RowBits> &board, int width,
                          int height) {
  RowBits mask = activeMaskFor(static_cast<uint8_t>(width));
  std::vector<RowBits> expected(height), actual(height);
  oracleNext(board, expected, width, height);
  computeConwayNext(board.data(), actual.data(), static_cast<uint8_t>(width),
                    static_cast<uint8_t>(height), mask);
  for (int y = 0; y < height; y++) {
    if (!rowsEqual(expected[y], actual[y], mask)) {
      return false;
    }
  }
  return true;
}

static std::vector<RowBits> makeBoard(int height) {
  return std::vector<RowBits>(height, RowBits(0, 0));
}

static void setCell(std::vector<RowBits> &board, int x, int y) {
  rowBitSet(board[y], static_cast<uint8_t>(x));
}

// --- Random equivalence across word-boundary-straddling widths -------------
static void testRandomEquivalence() {
  std::mt19937_64 rng(0xC0FFEEULL);
  const int widths[] = {8, 32, 63, 64, 65, 96, 127, 128};
  const int heights[] = {16, 17, 64, 128};

  for (int width : widths) {
    for (int height : heights) {
      RowBits mask = activeMaskFor(static_cast<uint8_t>(width));
      for (int trial = 0; trial < 40; trial++) {
        std::vector<RowBits> board = makeBoard(height);
        for (int y = 0; y < height; y++) {
          board[y] = RowBits(rng(), rng()) & mask;
        }
        // Evolve several generations; a single-step bug often only shows up
        // after the pattern interacts with the toroidal seam.
        bool ok = true;
        for (int gen = 0; gen < 4 && ok; gen++) {
          ok = matchesOracle(board, width, height);
          std::vector<RowBits> next(height);
          computeConwayNext(board.data(), next.data(),
                            static_cast<uint8_t>(width),
                            static_cast<uint8_t>(height), mask);
          board = next;
        }
        if (!ok) {
          std::printf("  (mismatch at width=%d height=%d trial=%d)\n", width,
                      height, trial);
          CHECK(false, "random board diverged from oracle");
          break;
        }
      }
    }
  }
}

// --- Canonical patterns -----------------------------------------------------
static void testBlinkerOscillates() {
  const int W = 16, H = 16;
  RowBits mask = activeMaskFor(W);
  std::vector<RowBits> board = makeBoard(H);
  // Horizontal blinker centered at (5,5).
  setCell(board, 4, 5);
  setCell(board, 5, 5);
  setCell(board, 6, 5);

  std::vector<RowBits> gen1(H);
  computeConwayNext(board.data(), gen1.data(), W, H, mask);
  // After one step it should be vertical: (5,4),(5,5),(5,6) and nothing else.
  CHECK(rowBitGet(gen1[4], 5) && rowBitGet(gen1[5], 5) && rowBitGet(gen1[6], 5),
        "blinker did not rotate to vertical");
  CHECK(!rowBitGet(gen1[5], 4) && !rowBitGet(gen1[5], 6),
        "blinker kept stale horizontal cells");

  std::vector<RowBits> gen2(H);
  computeConwayNext(gen1.data(), gen2.data(), W, H, mask);
  for (int y = 0; y < H; y++) {
    CHECK(rowsEqual(gen2[y], board[y], mask),
          "blinker did not return to its start after 2 generations");
  }
}

static void testBlockIsStable() {
  const int W = 16, H = 16;
  RowBits mask = activeMaskFor(W);
  std::vector<RowBits> board = makeBoard(H);
  setCell(board, 5, 5);
  setCell(board, 6, 5);
  setCell(board, 5, 6);
  setCell(board, 6, 6);

  std::vector<RowBits> next(H);
  computeConwayNext(board.data(), next.data(), W, H, mask);
  for (int y = 0; y < H; y++) {
    CHECK(rowsEqual(next[y], board[y], mask), "2x2 block was not stable");
  }
}

static void testGliderTranslates() {
  const int W = 32, H = 32;
  RowBits mask = activeMaskFor(W);
  std::vector<RowBits> board = makeBoard(H);
  // Standard glider.
  setCell(board, 2, 1);
  setCell(board, 3, 2);
  setCell(board, 1, 3);
  setCell(board, 2, 3);
  setCell(board, 3, 3);

  // After 4 generations a glider shifts by (+1,+1).
  for (int gen = 0; gen < 4; gen++) {
    std::vector<RowBits> next(H);
    computeConwayNext(board.data(), next.data(), W, H, mask);
    board = next;
  }
  std::vector<RowBits> expected = makeBoard(H);
  setCell(expected, 3, 2);
  setCell(expected, 4, 3);
  setCell(expected, 2, 4);
  setCell(expected, 3, 4);
  setCell(expected, 4, 4);
  for (int y = 0; y < H; y++) {
    CHECK(rowsEqual(board[y], expected[y], mask),
          "glider did not translate by (1,1) after 4 generations");
  }
}

int main() {
  testRandomEquivalence();
  testBlinkerOscillates();
  testBlockIsStable();
  testGliderTranslates();

  if (g_failures == 0) {
    std::printf("All life_bits tests passed.\n");
    return 0;
  }
  std::printf("%d check(s) failed.\n", g_failures);
  return 1;
}
