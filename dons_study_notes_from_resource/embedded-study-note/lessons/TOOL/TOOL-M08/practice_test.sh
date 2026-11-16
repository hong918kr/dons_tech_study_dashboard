#!/bin/bash
# TOOL-M08 practice — bisect a PERFORMANCE regression of `./build/app bench` (CRC over 16 MiB).
# Use custom terms so the log reads naturally:
#   git bisect start --term-old=fast --term-new=slow main v1.0
#   git bisect run /path/to/practice_test.sh          (0 = fast/old, 1 = slow/new, 125 = skip)
# Noise handling: the baseline binary (BASE_REV, default v1.0) is built once, out of the work tree,
# and measured INTERLEAVED with the current binary (A B A B ...) -> machine-load drift hits both.
# Verdict on the ratio of medians: > SLOW -> slow (1), < FAST -> fast (0), in between -> skip (125).
set -u
N=${N:-5}; SLOW=${SLOW:-2.0}; FAST=${FAST:-1.4}; BASE_REV=${BASE_REV:-v1.0}

command -v make >/dev/null 2>&1 && command -v "${CC:-cc}" >/dev/null 2>&1 || exit 255
GITDIR=$(git rev-parse --absolute-git-dir) || exit 255

# baseline build: git archive of BASE_REV into .git/ (outside the work tree -> git clean can't touch it)
BASE=$GITDIR/perf-base-$(git rev-parse --short=12 "$BASE_REV^{commit}")
if [ ! -x "$BASE/build/app" ]; then
    rm -rf "$BASE" && mkdir -p "$BASE" || exit 255
    git archive "$BASE_REV" | tar -x -C "$BASE" || exit 255
    make -s -C "$BASE" app >/dev/null 2>&1 || exit 255     # baseline must build, else abort
fi

git clean -fdxq
make -s app >/dev/null 2>&1 || exit 125

ns() { "$1" bench 2>/dev/null | sed -n 's/.* ns=\([0-9]*\)$/\1/p'; }
median() { sort -n | awk '{ v[NR] = $1 } END { if (NR == 0) exit 1; print v[int((NR + 1) / 2)] }'; }
: > "$GITDIR/perf-a.txt"; : > "$GITDIR/perf-b.txt"
for _ in $(seq "$N"); do
    ns "$BASE/build/app" >> "$GITDIR/perf-a.txt"
    ns ./build/app       >> "$GITDIR/perf-b.txt"
done
base=$(median < "$GITDIR/perf-a.txt") || exit 125
cur=$(median < "$GITDIR/perf-b.txt") || exit 125
ratio=$(awk -v c="$cur" -v b="$base" 'BEGIN { printf "%.2f", c / b }')
echo "perf: median cur=$cur ns base=$base ns ratio=$ratio" >&2
awk -v r="$ratio" -v t="$SLOW" 'BEGIN { exit !(r > t) }' && exit 1
awk -v r="$ratio" -v t="$FAST" 'BEGIN { exit !(r < t) }' && exit 0
exit 125
