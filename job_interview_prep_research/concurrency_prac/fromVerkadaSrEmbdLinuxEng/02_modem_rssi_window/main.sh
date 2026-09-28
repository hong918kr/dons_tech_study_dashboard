#!/bin/bash
#   ./main.sh          main.c + rssi_window.c            (your code)
#   ./main.sh sol      main.c + rssi_window_solution.c   (reference)
set -e
cd "$(dirname "$0")"
mkdir -p build

case "${1:-mine}" in
  mine) SRC=rssi_window.c ;;
  sol)  SRC=rssi_window_solution.c ;;
  *) echo "usage: $0 [mine|sol]"; exit 2 ;;
esac

echo "# building main.c + $SRC"
cc -std=c11 -O2 -Wall -Wextra -pthread -o build/a.out main.c "$SRC" -lm
./build/a.out
