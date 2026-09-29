#!/bin/bash
#   ./main.sh          main.c + config_store.c            (your code)
#   ./main.sh sol      main.c + config_store_solution.c   (reference answer)
#   ./main.sh tsan     reference answer under ThreadSanitizer
#   ./main.sh asan     reference answer under AddressSanitizer (use-after-free)
#   ./main.sh tsan mine | ./main.sh asan mine   same, against your code
set -e
cd "$(dirname "$0")"
mkdir -p build

MODE="${1:-mine}"
SAN=""
OUT=build/a.out
if [ "$MODE" = tsan ]; then
  SAN="-fsanitize=thread -g"; OUT=build/a.tsan; MODE="${2:-sol}"
elif [ "$MODE" = asan ]; then
  SAN="-fsanitize=address -fno-omit-frame-pointer -g"; OUT=build/a.asan; MODE="${2:-sol}"
fi

case "$MODE" in
  mine) SRC=config_store.c ;;
  sol)  SRC=config_store_solution.c ;;
  *) echo "usage: $0 [mine|sol] | $0 [tsan|asan] [mine|sol]"; exit 2 ;;
esac

echo "# building main.c + $SRC ${SAN:+($SAN)}"
cc -std=c11 -O2 -Wall -Wextra -pthread $SAN -o "$OUT" main.c "$SRC" -lm
"./$OUT"
