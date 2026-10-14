#!/usr/bin/env bash
# Builds and runs every solution.c / fixed.c; each must exit 0 (ALL PASS).
cd "$(dirname "$0")"
fail=0
for f in problems/*/solution.c problems/*/fixed.c; do
  [ -f "$f" ] || continue
  bin="${TMPDIR:-/tmp}/scout_check_$(basename "$(dirname "$f")")"
  if cc -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o "$bin" "$f" -lpthread 2>"$bin.log" \
     && "$bin" >"$bin.out" 2>&1; then echo "PASS  $f"
  else echo "FAIL  $f"; cat "$bin.log" "$bin.out" 2>/dev/null | head -20; fail=1; fi
done
exit $fail
