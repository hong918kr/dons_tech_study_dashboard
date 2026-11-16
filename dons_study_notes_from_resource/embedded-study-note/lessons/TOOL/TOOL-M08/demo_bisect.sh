#!/usr/bin/env bash
# TOOL-M08 demo — manual bisect, bisect run with the solution / starter scripts, log + replay, timing.
#   bash demo_bisect.sh > demo_bisect_out.txt 2>&1
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 LC_ALL=C TZ=UTC
L=$(pwd); D=/tmp/TOOL-M08_work/demo_bisect
rm -rf "$D" && mkdir -p "$D" || exit 2
cp solution_test.sh starter_test.sh "$D/" && chmod +x "$D"/*.sh
bash gen_repo.sh "$D/repo"; cd "$D/repo" || exit 2
rev() { git rev-list -1 --grep "^\[c$1\]" main; }
sec() { echo; echo "### $*"; }

sec "history: newest 3, the planted commits, tags"
git log --oneline -3
git log --oneline --grep '^\[c0\(20\|60\|61\|62\|63\|85\|88\)\]'
git tag -n1 | sed 's/ \+/ /'
sec "the culprit is header-only"
git show --stat --format='%h %s' "$(rev 063)" | sed '/^$/d'
git show --format= "$(rev 063)" -- include/ringbuf.h | grep '^[-+] *static'

sec "manual session: start, mark bad/good by hand"
git bisect start
git bisect bad main
git bisect good v1.0
git bisect terms
sec "visualize the suspects (first 3 of them)"
git bisect visualize --oneline | head -3
echo "suspects: $(git bisect visualize --oneline | wc -l | tr -d ' ')"
sec "mark the HAL-rename window as untestable in one command (range A..B = B and its ancestors not in A)"
git bisect skip "$(rev 059)..$(rev 061)"
git bisect log | grep '^git bisect skip'
git bisect reset >/dev/null 2>&1

sec "bisect run with solution_test.sh (timed)"
git clean -fdxq
t0=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
git bisect start main v1.0 >/dev/null
git bisect run "$D/solution_test.sh" > "$D/run_solution.txt" 2>&1
t1=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
grep -E '^Bisecting|^\[|bisect-test|first bad commit$' "$D/run_solution.txt"
echo "wall time: $(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f s", b - a }')"
sec "git bisect log"
git bisect log | tee "$D/bisect.log"
git bisect reset >/dev/null 2>&1

sec "git bisect replay of that log (no script runs, no builds)"
git bisect replay "$D/bisect.log" | grep -E "first bad commit$|^ include"
git bisect reset >/dev/null 2>&1

sec "cost of one step vs a linear scan (clean build + run, 5 samples)"
for c in 010 030 050 070 095; do
	git checkout -q "$(rev $c)"; git clean -fdxq
	s0=$(perl -MTime::HiRes=time -e 'printf "%.3f", time'); make -s app >/dev/null 2>&1; ./build/app >/dev/null 2>&1
	s1=$(perl -MTime::HiRes=time -e 'printf "%.3f", time'); awk -v a="$s0" -v b="$s1" -v c="$c" 'BEGIN { printf "c%s %.2f s\n", c, b - a }'
done 2>/dev/null   # (hide the shell's "Abort trap: 6" notices for c070/c095)
git checkout -q main; git clean -fdxq

sec "bisect run with starter_test.sh (stale build/ + in-repo oracle)"
git bisect start main v1.0 >/dev/null
git bisect run "$D/starter_test.sh" > "$D/run_starter.txt" 2>&1
grep -E '^\[|first bad commit$' "$D/run_starter.txt"
git bisect log | sed -n 's/^# \([a-z]*\): \[[0-9a-f]*\] \[\(c[0-9]*\)\].*/\2=\1/p' | tr '\n' ' '; echo
git bisect reset >/dev/null 2>&1
sec "why (a): build c049, checkout c074, plain make in the SAME second (what the starter does)"
git checkout -q "$(rev 049)"; git clean -fdxq; make -s app >/dev/null 2>&1
git checkout -q "$(rev 074)"; make app 2>&1 | sed 's/^/  make: /'
./build/app | sed 's/^/  app: /'
stat -f '  mtime %m  %N' build/app build/version.o build/ringbuf.o include/ringbuf.h src/version.c
sec "why (b): same, but the c049 build is an hour old (normal when a step takes minutes)"
git checkout -q "$(rev 049)"; git clean -fdxq; make -s app >/dev/null 2>&1
git ls-files -z | xargs -0 touch -A -020000     # checkout of c049 happened 2 h ago
find build -exec touch -A -010000 {} +          # its build finished 1 h ago
git checkout -q "$(rev 074)"; make app 2>&1 | sed 's/^/  make: /'
./build/app | sed 's/^/  app: /'
git clean -fdxq; git checkout -q main

sec "pathspec-limited bisect: only commits touching include/ are candidates"
git log --oneline v1.0..main -- include
git bisect start main v1.0 -- include >/dev/null
git bisect run "$D/solution_test.sh" 2>&1 | grep -v '^running'
git bisect log | grep '^git bisect'
git bisect reset >/dev/null 2>&1
