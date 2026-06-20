#!/usr/bin/env sh
# Host unit tests for the bitwise Game of Life core (src/life_bits.h).
# No board required. Usage: sh tests/run.sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-clang++}"
OUT="$(mktemp -t test_life.XXXXXX)"
"$CXX" -std=c++17 -O2 -Wall -Wextra "$DIR/test_life_bits.cpp" -o "$OUT"
"$OUT"
