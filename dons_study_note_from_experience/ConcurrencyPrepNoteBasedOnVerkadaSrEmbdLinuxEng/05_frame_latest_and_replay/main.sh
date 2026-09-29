#!/bin/bash
#   ./main.sh                 main.c + frame_store.c            (your code)
#   ./main.sh sol             main.c + frame_store_solution.c   (reference)
#   ./main.sh tsan [mine|sol] same, built with -fsanitize=thread
set -e
cd "$(dirname "$0")"
mkdir -p build

MODE="${1:-mine}"
EXTRA=""
if [ "$MODE" = tsan ]; then
  EXTRA="-fsanitize=thread -g"
  MODE="${2:-sol}"
fi

case "$MODE" in
  mine) SRC=frame_store.c ;;
  sol)  SRC=frame_store_solution.c ;;
  *) echo "usage: $0 [mine|sol] | $0 tsan [mine|sol]"; exit 2 ;;
esac

echo "# building main.c + $SRC ${EXTRA:+($EXTRA)}"
cc -std=c11 -O2 -Wall -Wextra -pthread $EXTRA -o build/a.out main.c "$SRC" -lm
./build/a.out
