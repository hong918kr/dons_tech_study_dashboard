#!/usr/bin/env bash
# TOOL-M08 checker — bash check.sh <starter|solution|practice>
# Generates the 100-commit repo (gen_repo.sh) in /tmp/TOOL-M08_work/<variant>/repo, copies the variant's
# bisect script OUTSIDE the repo, then asserts (real git + make + cc on this Mac):
#   - the script's exit code at hand-picked commits (unit checks of the bisect-run contract)
#   - a real `git bisect run` lands on the planted commit, with ~log2(N) steps + skips
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;; esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 LC_ALL=C TZ=UTC
LESSON=$(pwd)
W=/tmp/TOOL-M08_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2
if ! bash gen_repo.sh "$W/repo" >"$W/gen.log" 2>&1; then echo "FAIL gen_repo.sh"; cat "$W/gen.log"; echo "0/1 passed"; exit 1; fi
cp "${V}_test.sh" "$W/test.sh" && chmod +x "$W/test.sh" || exit 2
cd "$W/repo" || exit 2

pass=0; total=0
ok()  { total=$((total + 1)); pass=$((pass + 1)); echo "PASS $1"; }
bad() { total=$((total + 1)); echo "FAIL $1"; [ -n "${2:-}" ] && echo "     $2"; return 0; }
rev() { git rev-list -1 --grep "^\[c$1\]" main; }                       # commit by subject tag
tag() { git log -1 --format=%s "$1" 2>/dev/null | sed -n 's/^\[\(c[0-9]*\)\].*/\1/p'; }
run_at() {  # <index> [noclean] -> prints the script's exit code
	git checkout -q -f "$(rev "$1")" || { echo 99; return; }
	[ "${2:-}" = noclean ] || git clean -fdxq
	"$W/test.sh" >/dev/null 2>"$W/err_c$1.txt"; echo $?
}
is_bad() { [ "$1" -ge 1 ] && [ "$1" -le 127 ] && [ "$1" -ne 125 ]; }      # bisect's "bad" range
why() {  # human-readable meaning of an exit code for bisect run
	if [ "$1" = 0 ]; then echo good; elif [ "$1" = 125 ]; then echo skip
	elif [ "$1" -ge 128 ]; then echo "ABORTS the bisect"; else echo bad; fi
}
bisect_run() {  # <logfile> [start args...] -> exit code of git bisect run
	local log=$1; shift
	git checkout -q -f main && git clean -fdxq
	git bisect start "$@" >/dev/null 2>&1 || return 98
	git bisect run "$W/test.sh" >"$log" 2>&1
}
first_new() { git rev-parse -q --verify "refs/bisect/$1" 2>/dev/null; }

echo "== $V: $(git --version), $(/usr/bin/make --version | head -1) =="
if [ "$V" != practice ]; then
	# ---------------------------------------------------------------- unit checks: exit-code contract
	rc=$(run_at 049)
	if [ "$rc" = 0 ]; then ok "c049 (healthy) -> 0 good"; else bad "c049 (healthy) -> want 0, got $rc ($(why "$rc"))"; fi
	rc=$(run_at 060)
	if [ "$rc" = 125 ]; then ok "c060 (does not compile) -> 125 skip"
	else bad "c060 (does not compile) -> want 125 skip, got $rc ($(why "$rc"))" "an unbuildable commit says nothing about the regression"; fi
	rc=$(run_at 074)
	if is_bad "$rc"; then ok "c074 (app aborts in assert) -> $rc bad"
	else bad "c074 (app aborts in assert) -> want 1..124, got $rc ($(why "$rc"))" "a crash is a bad commit; rc >= 128 stops git bisect run"; fi
	rc=$(run_at 090)
	if is_bad "$rc"; then ok "c090 (bug present, repo test weakened at c085) -> $rc bad"
	else bad "c090 (bug present, repo test weakened at c085) -> want bad, got $rc ($(why "$rc"))" "oracle taken from the checkout changes with the checkout"; fi
	run_at 062 >/dev/null; rc=$(run_at 063 noclean)
	if is_bad "$rc"; then ok "c062 -> c063 without cleaning (header-only change) -> $rc bad"
	else bad "c062 -> c063 without cleaning (header-only change) -> want bad, got $rc ($(why "$rc"))" "stale build/ from the previous step was tested"; fi
	run_at 049 >/dev/null; st=$(git status --porcelain)
	if [ -z "$st" ]; then ok "work tree clean after a step (no tracked edits, no untracked files)"
	else bad "work tree dirty after a step" "$(echo "$st" | head -2)"; fi

	# ---------------------------------------------------------------- the real bisect
	bisect_run "$W/bisect.txt" main v1.0; brc=$?
	fb=$(first_new bad); want=$(rev 063)
	if [ "$brc" = 0 ] && grep -q 'is the first bad commit' "$W/bisect.txt" && [ "$fb" = "$want" ]; then
		ok "git bisect run main v1.0 -> first bad = $(tag "$fb") $(git log -1 --format=%s "$fb" | cut -d' ' -f2-)"
	else
		bad "git bisect run main v1.0 -> want [c063] (rc=$brc)" \
		    "$(grep -m1 -E 'bisect run failed|first bad commit|could be any' "$W/bisect.txt")$( [ -n "$fb" ] && echo " -> landed on $(tag "$fb")")"
	fi
	git bisect log >"$W/bisect_log.txt" 2>/dev/null
	ng=$(grep -c '^git bisect good' "$W/bisect_log.txt"); nb=$(grep -c '^git bisect bad' "$W/bisect_log.txt")
	ns=$(grep -c '^git bisect skip' "$W/bisect_log.txt")
	skipped=$(grep '^# skip:' "$W/bisect_log.txt" | sed -n 's/.*\] \[\(c[0-9]*\)\].*/\1/p' | tr '\n' ' ' | sed 's/ $//')
	if [ "$fb" = "$want" ] && [ $((ng + nb)) -le 8 ] && [ "$ns" -ge 1 ] && [ -z "$(echo "$skipped" | tr ' ' '\n' | grep -v -E '^c06[01]$')" ]; then
		ok "steps: $((ng + nb)) good/bad + $ns skip {$skipped}  (bound <= 8 = ceil(log2 99) + 1)"
	else bad "steps: want <= 8 good/bad + skips only on c060/c061" "good=$ng bad=$nb skip=$ns {$skipped}"; fi
	git bisect reset -q >/dev/null 2>&1 || git bisect reset >/dev/null 2>&1
	if [ "$(git symbolic-ref -q --short HEAD)" = main ] && [ -z "$(git status --porcelain)" ]; then
		ok "git bisect reset -> back on main, tree clean"
	else bad "after git bisect reset" "HEAD=$(git symbolic-ref -q --short HEAD || git rev-parse --short HEAD) $(git status --porcelain | head -1)"; fi
else
	# ---------------------------------------------------------------- practice: perf regression
	rc=$(run_at 049)
	if [ "$rc" = 0 ]; then ok "P c049 (table CRC) -> 0 fast  [$(tail -1 "$W/err_c049.txt")]"
	else bad "P c049 -> want 0 fast, got $rc" "$(tail -1 "$W/err_c049.txt")"; fi
	rc=$(run_at 090)
	if [ "$rc" = 1 ]; then ok "P c090 (bitwise CRC) -> 1 slow  [$(tail -1 "$W/err_c090.txt")]"
	else bad "P c090 -> want 1 slow, got $rc" "$(tail -1 "$W/err_c090.txt")"; fi
	rc=$(run_at 061)
	if [ "$rc" = 125 ]; then ok "P c061 (does not compile) -> 125 skip"; else bad "P c061 -> want 125, got $rc"; fi
	bisect_run "$W/bisect.txt" --term-old=fast --term-new=slow main v1.0; brc=$?
	fs=$(first_new slow); want=$(rev 088)
	if [ "$brc" = 0 ] && grep -q 'is the first slow commit' "$W/bisect.txt" && [ "$fs" = "$want" ]; then
		ok "git bisect run (terms fast/slow) -> first slow = $(tag "$fs") $(git log -1 --format=%s "$fs" | cut -d' ' -f2-)"
	else bad "bisect fast/slow -> want [c088] (rc=$brc)" "$(grep -m1 -E 'bisect run failed|first slow commit|could be any' "$W/bisect.txt")"; fi
	git bisect log >"$W/bisect_log.txt" 2>/dev/null
	nf=$(grep -c '^git bisect fast' "$W/bisect_log.txt"); nsl=$(grep -c '^git bisect slow' "$W/bisect_log.txt")
	if [ "$nf" -ge 1 ] && [ "$nsl" -ge 1 ] && [ $((nf + nsl)) -le 8 ]; then ok "log uses the terms: $nf fast + $nsl slow decisions (<= 8)"
	else bad "bisect log terms/steps" "fast=$nf slow=$nsl"; fi
	git bisect reset >/dev/null 2>&1
	if ls -d .git/perf-base-* >/dev/null 2>&1 && [ -z "$(git status --porcelain)" ]; then
		ok "baseline build cached in $(basename "$(ls -d .git/perf-base-*)") (outside the work tree), tree clean"
	else bad "baseline cache / clean tree" "$(git status --porcelain | head -1)"; fi
fi

echo "$pass/$total passed"
[ "$pass" = "$total" ] && exit 0 || exit 1
