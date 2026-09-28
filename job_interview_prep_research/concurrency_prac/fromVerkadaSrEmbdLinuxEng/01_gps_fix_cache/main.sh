#!/bin/bash
#   ./main.sh                 main.c + gps_cache.c            (your code)
#   ./main.sh sol             main.c + gps_cache_solution.c   (reference)
#   ./main.sh window [which]  same, with a 1.5-second window and extra
#                             eviction / window-edge checks (default: sol)
#   ./main.sh tsan  [which]   build with ThreadSanitizer (default: sol)
set -e
cd "$(dirname "$0")"
mkdir -p build

MODE="${1:-mine}"
EXTRA=""
case "$MODE" in
  window) EXTRA="-DGPS_WINDOW_US=1500000ull -DWINDOW_TEST"; MODE="${2:-sol}" ;;
  tsan)   EXTRA="-fsanitize=thread -g";                     MODE="${2:-sol}" ;;
esac

case "$MODE" in
  mine)     SRC=gps_cache.c ;;
  sol|ring) SRC=gps_cache_solution.c ;;
  *) echo "usage: $0 [mine|sol] | $0 window [mine|sol] | $0 tsan [mine|sol]"; exit 2 ;;
esac

echo "# building main.c + $SRC ${EXTRA:+($EXTRA)}"
cc -std=c11 -O2 -Wall -Wextra -pthread $EXTRA -o build/a.out main.c "$SRC" -lm
./build/a.out
