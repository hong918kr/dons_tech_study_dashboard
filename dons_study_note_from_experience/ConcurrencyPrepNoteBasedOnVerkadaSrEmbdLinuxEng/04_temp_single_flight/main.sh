#!/bin/bash
#   ./main.sh        main.c + temp_cache.c            (your code)
#   ./main.sh sol    main.c + temp_cache_solution.c   (reference)
#   ./main.sh tsan   reference under -fsanitize=thread
set -e
cd "$(dirname "$0")"
mkdir -p build

CC="${CC:-cc}"
EXTRA=""
case "${1:-mine}" in
  mine) SRC=temp_cache.c ;;
  sol)  SRC=temp_cache_solution.c ;;
  tsan) SRC=temp_cache_solution.c; EXTRA="-fsanitize=thread -g" ;;
  *) echo "usage: $0 [mine|sol|tsan]"; exit 2 ;;
esac

echo "# building main.c + $SRC ${EXTRA:+($EXTRA)}"
$CC -std=c11 -O2 -Wall -Wextra -pthread $EXTRA -o build/a.out main.c "$SRC" -lm
./build/a.out
