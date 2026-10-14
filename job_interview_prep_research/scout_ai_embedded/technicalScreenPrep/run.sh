#!/usr/bin/env bash
# Usage: ./run.sh T1-1        compile + run problems/T1-1_*/stub.c
#        ./run.sh T1-1 sol    same for solution.c
#        ./run.sh D buggy     problems/D_*/buggy.c   (any .c basename works)
set -euo pipefail
cd "$(dirname "$0")"
id="${1:?usage: ./run.sh <ID> [stub|sol|<file>]}"; which="${2:-stub}"
[ "$which" = sol ] && which=solution
dir=$(ls -d problems/"$id"_* 2>/dev/null | head -1)
[ -n "$dir" ] || { echo "no problem dir for $id"; exit 1; }
src="$dir/$which.c"; bin="${TMPDIR:-/tmp}/scout_$(basename "$dir")_$which"
cc -std=c11 -g -O0 -Wall -Wextra -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer \
   -o "$bin" "$src" -lpthread
echo "== $src"; "$bin"
