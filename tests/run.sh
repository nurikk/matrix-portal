#!/usr/bin/env sh
# Host unit tests for the pure simulation cores (no board required).
# Usage: sh tests/run.sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-clang++}"

OUT_BITS="$(mktemp -t test_life.XXXXXX)"
"$CXX" -std=c++17 -O2 -Wall -Wextra "$DIR/test_life_bits.cpp" -o "$OUT_BITS"
"$OUT_BITS"

OUT_SET="$(mktemp -t test_settings.XXXXXX)"
"$CXX" -std=c++17 -O2 -Wall -Wextra "$DIR/test_life_settings.cpp" -o "$OUT_SET"
"$OUT_SET"

OUT_CLOCK="$(mktemp -t test_clock.XXXXXX)"
"$CXX" -std=c++17 -O2 -Wall -Wextra "$DIR/test_clock_minute_trace.cpp" -o "$OUT_CLOCK"
"$OUT_CLOCK"

OUT_HOUR="$(mktemp -t test_hour.XXXXXX)"
"$CXX" -std=c++17 -O2 -Wall -Wextra "$DIR/test_clock_hour_degreen.cpp" -o "$OUT_HOUR"
"$OUT_HOUR"
