#!/bin/bash
# 07_badge_audit_dedupe — build and run.
#
#   ./main.sh        main.c + audit_log.c            (your code)
#   ./main.sh sol    main.c + audit_log_solution.c   (reference)
#
# TSan run (no warnings expected):
#   cc -std=c11 -O2 -Wall -Wextra -pthread -fsanitize=thread \
#      -o build/tsan main.c audit_log_solution.c -lm && ./build/tsan
set -e
cd "$(dirname "$0")"
mkdir -p build

case "${1:-mine}" in
  mine) SRC=audit_log.c ;;
  sol)  SRC=audit_log_solution.c ;;
  *) echo "usage: $0 [mine|sol]"; exit 2 ;;
esac

echo "# building main.c + $SRC"
cc -std=c11 -O2 -Wall -Wextra -pthread -o build/a.out main.c "$SRC" -lm
./build/a.out
