#!/bin/bash
# 10_heartbeat_watchdog
#   ./main.sh        main.c + watchdog.c           (your code)
#   ./main.sh sol    main.c + watchdog_solution.c  (reference)
set -e
cd "$(dirname "$0")"
mkdir -p build
case "${1:-mine}" in
  mine) SRC=watchdog.c ;;
  sol)  SRC=watchdog_solution.c ;;
  *) echo "usage: $0 [mine|sol]"; exit 2 ;;
esac
echo "# building main.c + $SRC"
cc -std=c11 -O2 -Wall -Wextra -pthread -o build/a.out main.c "$SRC" -lm
./build/a.out
