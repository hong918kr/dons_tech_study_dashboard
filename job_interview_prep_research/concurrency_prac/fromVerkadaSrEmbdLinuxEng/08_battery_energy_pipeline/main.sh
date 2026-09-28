#!/bin/bash
# 08_battery_energy_pipeline
#
#   ./main.sh          main.c + energy_store.c            (your code)
#   ./main.sh sol      main.c + energy_store_solution.c   (reference)
#   ./main.sh tsan     reference under ThreadSanitizer
#
# Each run takes about 6 seconds.
set -e
cd "$(dirname "$0")"
mkdir -p build

case "${1:-mine}" in
  mine) SRC=energy_store.c ;;
  sol)  SRC=energy_store_solution.c ;;
  tsan) SRC=energy_store_solution.c
        echo "# building main.c + $SRC (-fsanitize=thread)"
        cc -std=c11 -O1 -g -Wall -Wextra -pthread -fsanitize=thread \
           -o build/a.tsan main.c "$SRC" -lm
        exec ./build/a.tsan ;;
  *) echo "usage: $0 [mine|sol|tsan]"; exit 2 ;;
esac

echo "# building main.c + $SRC"
cc -std=c11 -O2 -Wall -Wextra -pthread -o build/a.out main.c "$SRC" -lm
./build/a.out
