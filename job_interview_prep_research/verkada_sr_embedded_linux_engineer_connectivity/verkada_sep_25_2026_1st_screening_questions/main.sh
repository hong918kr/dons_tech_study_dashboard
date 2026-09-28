#!/bin/bash
# On the pad:  gcc -O2 -pthread -o /usercode/a.out main.c recent_lux.c && /usercode/a.out
# (the pad showed "-02" — that's a zero, gcc rejects it; the flag is capital O)
#
#   ./main.sh                 main.c + recent_lux.c                   (your code)
#   ./main.sh sol             Part 2 = circular queue + binary search (recent_lux_solution.c)
#   ./main.sh list            Part 2 = linked list                    (recent_lux_solution_list.c)
#   ./main.sh rbtree          Part 2 = red-black tree                 (recent_lux_solution_rbtree.c)
#   ./main.sh window [which]  same, with a 2-second window + window-edge tests (default: sol)
set -e
cd "$(dirname "$0")"
mkdir -p build

MODE="${1:-mine}"
EXTRA=""
if [ "$MODE" = window ]; then
  EXTRA="-DLUX_WINDOW_US=2000000ull -DWINDOW_TEST"
  MODE="${2:-sol}"
fi

case "$MODE" in
  mine)       SRC=recent_lux.c ;;
  sol|ring)   SRC=recent_lux_solution.c ;;
  list)       SRC=recent_lux_solution_list.c ;;
  rbtree|rb)  SRC=recent_lux_solution_rbtree.c ;;
  *) echo "usage: $0 [mine|sol|list|rbtree] | $0 window [mine|sol|list|rbtree]"; exit 2 ;;
esac

echo "# building main.c + $SRC ${EXTRA:+($EXTRA)}"
gcc -std=c11 -O2 -Wall -Wextra -pthread $EXTRA -o build/a.out main.c "$SRC" -lm
./build/a.out
