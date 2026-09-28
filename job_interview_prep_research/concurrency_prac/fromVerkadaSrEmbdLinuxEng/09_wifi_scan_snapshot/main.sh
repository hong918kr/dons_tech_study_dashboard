#!/bin/bash
# 09_wifi_scan_snapshot
#
#   ./main.sh          main.c + ap_table.c            (your code)
#   ./main.sh sol      main.c + ap_table_solution.c   (reference)
#   ./main.sh tsan     reference under -fsanitize=thread
#
# Whole run is under 10 s.
set -e
cd "$(dirname "$0")"
mkdir -p build

EXTRA=""
case "${1:-mine}" in
  mine) SRC=ap_table.c ;;
  sol)  SRC=ap_table_solution.c ;;
  tsan) SRC=ap_table_solution.c; EXTRA="-fsanitize=thread" ;;
  *) echo "usage: $0 [mine|sol|tsan]"; exit 2 ;;
esac

echo "# building main.c + $SRC ${EXTRA:+($EXTRA)}"
cc -std=c11 -O2 -Wall -Wextra -pthread $EXTRA -o build/a.out main.c "$SRC" -lm
./build/a.out
