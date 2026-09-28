#!/bin/bash
# 03_event_tailer_shutdown
#
#   ./main.sh                 main.c + event_queue.c            (your code)
#   ./main.sh sol             main.c + event_queue_solution.c   (reference)
#   ./main.sh fast [which]    20 ms x 32 buckets (640 ms window) so that bucket
#                             eviction and the lazy advance really run
#                             (default: sol)
set -e
cd "$(dirname "$0")"
mkdir -p build

MODE="${1:-mine}"
EXTRA=""
if [ "$MODE" = fast ]; then
  EXTRA="-DEVQ_BUCKET_US=20000ull -DEVQ_NBUCKETS=32u"
  MODE="${2:-sol}"
fi

case "$MODE" in
  mine) SRC=event_queue.c ;;
  sol)  SRC=event_queue_solution.c ;;
  *) echo "usage: $0 [mine|sol] | $0 fast [mine|sol]"; exit 2 ;;
esac

echo "# building main.c + $SRC ${EXTRA:+($EXTRA)}"
cc -std=c11 -O2 -Wall -Wextra -pthread $EXTRA -o build/a.out main.c "$SRC" -lm
./build/a.out
