// Pure, host-testable core of the Game of Life simulation.
//
// A row of the grid is stored as two uint64_t (`RowBits`), giving up to 128
// columns. The grid is toroidal: column -1 wraps to column width-1, and the
// active width can be anything <= 128, so a "row" may span both words.
//
// This header has no Arduino dependencies so it can be unit-tested on the host
// (see tests/test_life_bits.cpp). main.cpp includes it for the firmware build.
#pragma once

#include <cstdint>

struct RowBits {
  uint64_t low;
  uint64_t high;

  RowBits(uint64_t lowValue = 0, uint64_t highValue = 0)
      : low(lowValue), high(highValue) {}

  operator bool() const { return low || high; }
};

inline RowBits operator~(RowBits value) { return RowBits(~value.low, ~value.high); }

inline RowBits operator&(RowBits a, RowBits b) {
  return RowBits(a.low & b.low, a.high & b.high);
}

inline RowBits operator|(RowBits a, RowBits b) {
  return RowBits(a.low | b.low, a.high | b.high);
}

inline RowBits operator^(RowBits a, RowBits b) {
  return RowBits(a.low ^ b.low, a.high ^ b.high);
}

inline RowBits &operator|=(RowBits &a, RowBits b) {
  a.low |= b.low;
  a.high |= b.high;
  return a;
}

inline RowBits &operator&=(RowBits &a, RowBits b) {
  a.low &= b.low;
  a.high &= b.high;
  return a;
}

// --- Single-bit helpers (column index 0..127) ------------------------------

inline bool rowBitGet(RowBits row, uint8_t index) {
  return index < 64 ? ((row.low >> index) & 1ULL)
                    : ((row.high >> (index - 64)) & 1ULL);
}

inline void rowBitSet(RowBits &row, uint8_t index) {
  if (index < 64) {
    row.low |= (1ULL << index);
  } else {
    row.high |= (1ULL << (index - 64));
  }
}

// Mask of the `width` valid columns (bits >= width are zero).
inline RowBits activeMaskFor(uint8_t width) {
  if (width >= 128) {
    return RowBits(UINT64_MAX, UINT64_MAX);
  }
  if (width == 64) {
    return RowBits(UINT64_MAX, 0);
  }
  if (width < 64) {
    return RowBits((1ULL << width) - 1ULL, 0);
  }
  return RowBits(UINT64_MAX, (1ULL << (width - 64)) - 1ULL);
}

// --- Bitwise next-generation core ------------------------------------------

// Each column's left neighbour: out[x] = row[x-1], wrapping row[width-1] -> 0.
// Implemented as a 128-bit left shift, clipped to `width`, with the top column
// wrapped into bit 0.
inline RowBits rotateTowardHigher(RowBits row, uint8_t width, RowBits mask) {
  RowBits shifted((row.low << 1),
                  (row.high << 1) | (row.low >> 63));
  shifted &= mask;
  if (rowBitGet(row, width - 1)) {
    shifted.low |= 1ULL;
  }
  return shifted;
}

// Each column's right neighbour: out[x] = row[x+1], wrapping row[0] -> width-1.
inline RowBits rotateTowardLower(RowBits row, uint8_t width, RowBits mask) {
  RowBits shifted((row.low >> 1) | (row.high << 63),
                  (row.high >> 1));
  shifted &= mask;
  if (row.low & 1ULL) {
    rowBitSet(shifted, width - 1);
  }
  return shifted;
}

// Conway's Life rule (B3/S23) expressed on the per-column neighbour-count
// bit-planes. s0..s3 are bit0..bit3 of the 0..8 neighbour count for every
// column at once. This is the lever for rule variants: e.g. HighLife (B36/S23)
// would additionally birth dead cells whose count == 6.
inline RowBits aliveNext(RowBits alive, RowBits s0, RowBits s1, RowBits s2,
                         RowBits s3) {
  RowBits isTwo = (~s3) & (~s2) & s1 & (~s0);   // count == 2
  RowBits isThree = (~s3) & (~s2) & s1 & s0;    // count == 3
  return isThree | (alive & isTwo);             // born on 3, survive on 2 or 3
}

inline RowBits conwayNextRow(RowBits above, RowBits mid, RowBits below,
                             uint8_t width, RowBits mask) {
  // The eight neighbours of every column, as eight bitmasks.
  RowBits neighbours[8] = {
      rotateTowardHigher(above, width, mask), above,
      rotateTowardLower(above, width, mask),  rotateTowardHigher(mid, width, mask),
      rotateTowardLower(mid, width, mask),    rotateTowardHigher(below, width, mask),
      below,                                  rotateTowardLower(below, width, mask)};

  // Sum the eight masks per column into a 4-bit count (planes s0..s3) using
  // bitwise full-adders -- every column is counted in parallel.
  RowBits s0, s1, s2, s3;
  for (int i = 0; i < 8; i++) {
    RowBits m = neighbours[i];
    RowBits carry0 = s0 & m;
    s0 = s0 ^ m;
    RowBits carry1 = s1 & carry0;
    s1 = s1 ^ carry0;
    RowBits carry2 = s2 & carry1;
    s2 = s2 ^ carry1;
    s3 = s3 ^ carry2;
  }

  return aliveNext(mid, s0, s1, s2, s3) & mask;
}

// Fills out[0..height-1] with the next Conway generation of current[].
inline void computeConwayNext(const RowBits *current, RowBits *out,
                              uint8_t width, uint8_t height, RowBits mask) {
  for (uint8_t y = 0; y < height; y++) {
    uint8_t aboveY = y == 0 ? height - 1 : y - 1;
    uint8_t belowY = y + 1 == height ? 0 : y + 1;
    out[y] = conwayNextRow(current[aboveY] & mask, current[y] & mask,
                           current[belowY] & mask, width, mask);
  }
}
