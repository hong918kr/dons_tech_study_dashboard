#!/usr/bin/env bash
# TOOL-M12 checker: static-analysis CI baseline (clang-tidy 23 + cppcheck 2.22, Mac).
#   bash check.sh <starter|solution|practice>
# Reads <variant>.clang-tidy and <variant>.cppcheck (one option per line) and:
#   0. rules  - config verifies, inputs untouched, every suppression is scoped
#   1. fw_bugs.c  - each of the 10 "// BUGnn" lines must get a finding from
#                   clang-tidy or cppcheck, and both tools must FAIL the job
#   2. canary     - findings in include/ are reported, in third_party/ are not
#   3. fw_clean.c - 0 findings from both tools (the CI baseline is green),
#                   and selftest.c proves the fixed code still works
#   4. practice   - clang-tidy alone >= 9/10, and a diff-only "ratchet" run
# The VM side (scan-build-21, cppcheck 2.19, gcc -fanalyzer) is demo_vm.sh.
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=$(pwd)
CT=$HOME/.venvs/clangtidy/bin/clang-tidy
CTD=$HOME/.venvs/clangtidy/bin/clang-tidy-diff.py
PY=$HOME/.venvs/clangtidy/bin/python3
# project flags; -D_FORTIFY_SOURCE=0 = analyze like the target (newlib has no
# fortify macros; the macOS SDK would hide memcpy/snprintf behind __builtin___*_chk)
CFLAGS_DB='"-std=c11", "-Wall", "-Wextra", "-D_FORTIFY_SOURCE=0", "-Iinclude", "-Ithird_party"'
# sha256 of the analyzed inputs (fw_bugs.c fw_clean.c include/fw_util.h third_party/vendor_hal.h)
INPUT_SHA=fa2031aedcf1a419a20fbe2a6ff5e015c6d2e294380e552a508aee6165778ae2

W=/tmp/TOOL-M12_work/$V
rm -rf "$W"
mkdir -p "$W"
P=0
F=0
pass() { echo "PASS $*"; P=$((P + 1)); }
fail() { echo "FAIL $*"; F=$((F + 1)); }
finish() {
	echo "$P/$((P + F)) passed"
	[ "$F" -eq 0 ] && exit 0
	exit 1
}
[ -x "$CT" ] || { fail "clang-tidy not found at $CT"; finish; }
command -v cppcheck >/dev/null || { fail "cppcheck not on PATH"; finish; }
[ -f "$V.clang-tidy" ] && [ -f "$V.cppcheck" ] || { fail "missing $V.clang-tidy or $V.cppcheck"; finish; }
CPPARGS=$(sed -e 's/#.*//' "$V.cppcheck" | tr '\n' ' ' | tr -s ' ')
echo "cppcheck options ($V.cppcheck):$CPPARGS"

# finding lines only:  file:line:col: severity: message [id]
FIND='^[^ :]+:[0-9]+:[0-9]+: (error|warning|style|performance|portability|information): '
# a BUG line counts as found only by a bug-finding check, not by a style check
# that happens to land on the same line (readability-identifier-length, ...)
BUGIDS='\[(clang-analyzer|clang-diagnostic|bugprone|cert|concurrency)-'
# tidy <file.c> <out>: run clang-tidy with the variant config via a compile DB
tidy() {
	"$CT" -p "$W" --config-file="$D/$V.clang-tidy" "$1" >"$2.raw" 2>"$2.err"
	local rc=$?
	sed "s|^$D/||" "$2.raw" >"$2"
	return $rc
}
# cpp <file.c> <out>: run cppcheck with the variant options + project includes
cpp() {
	# shellcheck disable=SC2086
	cppcheck $CPPARGS -Iinclude -Ithird_party \
		--template='{file}:{line}:{column}: {severity}: {message} [{id}]' "$1" >"$2" 2>&1
}

# ---- 0. rules -------------------------------------------------------------
if "$CT" --verify-config --config-file="$V.clang-tidy" >"$W/verify.txt" 2>&1; then
	pass "rules: $V.clang-tidy passes clang-tidy --verify-config"
else
	fail "rules: $V.clang-tidy: $(grep -m1 -iE 'error|unknown|warning' "$W/verify.txt")"
fi
sum=$(cat fw_bugs.c fw_clean.c include/fw_util.h third_party/vendor_hal.h | shasum -a 256 | cut -d' ' -f1)
if [ "$sum" = "$INPUT_SHA" ]; then
	pass "rules: fw_bugs.c fw_clean.c include/ third_party/ untouched"
else
	fail "rules: analyzed inputs were edited (fix the config, not the code)"
fi
bad=""
for a in $CPPARGS; do
	case "$a" in
	--suppress=*)
		IFS=: read -r sid sfile sline <<<"${a#--suppress=}"
		if [ "$sid" = '*' ] || [ -z "${sfile:-}" ] || ! [[ "${sline:-}" =~ ^[0-9]+$ ]]; then bad="$bad $a"; fi
		;;
	--suppressions-list=* | --suppress-xml=*) bad="$bad $a" ;;
	esac
done
inline_bad=$(grep -nE 'NOLINT|cppcheck-suppress' fw_clean.c |
	grep -vE '// NOLINT\([A-Za-z0-9_.-]+\)$|// cppcheck-suppress [A-Za-z0-9_]+$' | cut -d: -f1 | tr '\n' ' ')
n_inline=$(grep -cE 'NOLINT|cppcheck-suppress' fw_clean.c)
if [ -z "$bad" ] && [ -z "$inline_bad" ]; then
	pass "rules: suppressions scoped (cppcheck options: none global; fw_clean.c: $n_inline inline, one id on one line each)"
else
	fail "rules: unscoped suppression:${bad}${inline_bad:+ fw_clean.c lines $inline_bad}"
fi

# compile database = what CMake would export (TOOL-M04): the real project flags
cat >"$W/compile_commands.json" <<EOF
[
  {"directory": "$D", "file": "$D/fw_bugs.c",
   "arguments": ["cc", $CFLAGS_DB, "-c", "fw_bugs.c", "-o", "/dev/null"]},
  {"directory": "$D", "file": "$D/fw_clean.c",
   "arguments": ["cc", $CFLAGS_DB, "-c", "fw_clean.c", "-o", "/dev/null"]}
]
EOF

# ---- 1. fw_bugs.c: every planted bug found, and the job fails --------------
tidy fw_bugs.c "$W/tidy_bugs.txt"
rc_tb=$?
cpp fw_bugs.c "$W/cpp_bugs.txt"
rc_cb=$?
grep -E "$FIND" "$W/tidy_bugs.txt" >"$W/tidy_bugs.find"
grep -E "$FIND" "$W/cpp_bugs.txt" >"$W/cpp_bugs.find"
tidy_only=0
for t in $(grep -oE '// BUG[0-9][0-9]' fw_bugs.c | sed 's|// ||' | sort -u); do
	lines=$(grep -n "// $t" fw_bugs.c | cut -d: -f1 | tr '\n' ' ')
	who="" by_tidy=0
	for l in $lines; do
		id=$(grep "^fw_bugs.c:$l:" "$W/tidy_bugs.find" | grep -oE '\[[^]]+\]$' | grep -m1 -E "$BUGIDS" | sed 's/,-warnings-as-errors//')
		[ -n "$id" ] && { who="$who tidy:$l$id"; by_tidy=1; }
		id=$(grep "^fw_bugs.c:$l:" "$W/cpp_bugs.find" | grep -v ': information: ' | grep -m1 -oE '\[[^]]+\]$')
		[ -n "$id" ] && who="$who cppcheck:$l$id"
	done
	tidy_only=$((tidy_only + by_tidy))
	if [ -n "$who" ]; then
		pass "fw_bugs.c $t found by$(echo "$who" | cut -d' ' -f1-3)"
	else
		fail "fw_bugs.c $t line(s) ${lines}: no finding from clang-tidy or cppcheck"
	fi
done
nt=$(wc -l <"$W/tidy_bugs.find" | tr -d ' ')
nc=$(wc -l <"$W/cpp_bugs.find" | tr -d ' ')
if [ "$rc_tb" -ne 0 ]; then
	pass "CI gate: clang-tidy exits $rc_tb on fw_bugs.c ($nt findings)"
else
	fail "CI gate: clang-tidy exits 0 on fw_bugs.c despite $nt findings (job stays green)"
fi
if [ "$rc_cb" -ne 0 ]; then
	pass "CI gate: cppcheck exits $rc_cb on fw_bugs.c ($nc findings)"
else
	fail "CI gate: cppcheck exits 0 on fw_bugs.c despite $nc findings (job stays green)"
fi

# ---- 2. canary: header filter must keep ours and drop the vendor's ---------
C=$W/canary
mkdir -p "$C/include" "$C/third_party"
printf '#define CANARY_TWICE(x) x * 2\n' >"$C/include/canary.h"
printf '#define VENDOR_TWICE(x) x * 2\n' >"$C/third_party/vendor_canary.h"
printf '#include "canary.h"\n#include "vendor_canary.h"\nint canary(int a);\nint canary(int a) { return CANARY_TWICE(a) + VENDOR_TWICE(a); }\n' >"$C/canary.c"
(cd "$C" && "$CT" --config-file="$D/$V.clang-tidy" canary.c -- -std=c11 -Iinclude -Ithird_party 2>/dev/null) |
	sed "s|^$C/||" >"$W/canary.txt"
if grep -qE '^include/canary.h:1:[0-9]+: (warning|error): .*macro-parentheses' "$W/canary.txt"; then
	pass "header filter: finding in our include/canary.h is reported"
else
	fail "header filter: finding in our include/canary.h NOT reported (own headers silently skipped)"
fi
if grep -qE '^third_party/[^ ]+: (warning|error): ' "$W/canary.txt"; then
	fail "header filter: third_party/vendor_canary.h finding reported (vendor noise)"
else
	pass "header filter: third_party/ header not reported"
fi

# ---- 3. fw_clean.c: zero findings, and it still works ----------------------
tidy fw_clean.c "$W/tidy_clean.txt"
rc_tc=$?
cpp fw_clean.c "$W/cpp_clean.txt"
rc_cc=$?
grep -E "$FIND" "$W/tidy_clean.txt" >"$W/tidy_clean.find"
grep -E "$FIND" "$W/cpp_clean.txt" >"$W/cpp_clean.find"
top() { grep -oE '\[[^]]+\]$' "$1" | sed 's/,-warnings-as-errors//' | sort | uniq -c | sort -rn | head -3 | awk '{printf " %s x%s", $2, $1}'; }
n=$(wc -l <"$W/tidy_clean.find" | tr -d ' ')
if [ "$n" -eq 0 ] && [ "$rc_tc" -eq 0 ]; then
	pass "fw_clean.c: clang-tidy 0 findings, exit 0"
else
	fail "fw_clean.c: clang-tidy $n findings (exit $rc_tc), top:$(top "$W/tidy_clean.find")"
fi
n=$(wc -l <"$W/cpp_clean.find" | tr -d ' ')
if [ "$n" -eq 0 ] && [ "$rc_cc" -eq 0 ]; then
	pass "fw_clean.c: cppcheck 0 findings, exit 0"
else
	fail "fw_clean.c: cppcheck $n findings (exit $rc_cc), top:$(top "$W/cpp_clean.find")"
fi
if cc -std=c11 -Wall -Wextra -Werror -Wthread-safety -Iinclude -Ithird_party selftest.c \
	-o "$W/selftest" 2>"$W/selftest.err"; then
	out=$("$W/selftest" 2>&1 | tail -1)
	if [ "$out" = "selftest(fw_clean.c): 16/16 OK" ]; then pass "fw_clean.c runs: $out"; else fail "fw_clean.c runs: $out"; fi
else
	fail "selftest.c does not build: $(head -1 "$W/selftest.err")"
fi

# ---- 4. practice: clang-tidy alone, and a diff-only ratchet ----------------
if [ "$V" = practice ]; then
	if [ "$tidy_only" -ge 9 ]; then
		pass "clang-tidy alone finds $tidy_only/10 planted bugs (>= 9)"
	else
		fail "clang-tidy alone finds $tidy_only/10 planted bugs (need >= 9)"
	fi
	R=$W/ratchet
	mkdir -p "$R"
	cp -R fw_bugs.c include third_party "$R/"
	export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_AUTHOR_NAME=ci GIT_COMMITTER_NAME=ci \
		GIT_AUTHOR_EMAIL=ci@example.invalid GIT_COMMITTER_EMAIL=ci@example.invalid \
		GIT_AUTHOR_DATE=2026-11-15T00:00:00Z GIT_COMMITTER_DATE=2026-11-15T00:00:00Z
	git -C "$R" -c init.defaultBranch=main init -q && git -C "$R" add -A && git -C "$R" commit -qm "legacy module"
	legacy=$(wc -l <"$R/fw_bugs.c" | tr -d ' ')
	cat >>"$R/fw_bugs.c" <<'EOF'

/* merge request: moving average of the last n RSSI samples */
int rssi_avg(const int16_t *s, int n)
{
    int sum;
    for (int i = 0; i < n; i++) {
        sum += s[i];
    }
    return n > 0 ? sum / n : 0;
}
EOF
	sed "s|$D|$R|g" "$W/compile_commands.json" >"$R/compile_commands.json"
	(cd "$R" && git diff -U0 HEAD | "$PY" -I "$CTD" -p1 -quiet -clang-tidy-binary "$CT" \
		-config-file "$D/$V.clang-tidy" -path "$R" 2>/dev/null) | sed "s|^$R/||" >"$W/ratchet.txt"
	grep -E "$FIND" "$W/ratchet.txt" >"$W/ratchet.find"
	new=$(awk -F: -v L="$legacy" '$2 > L' "$W/ratchet.find" | wc -l | tr -d ' ')
	old=$(awk -F: -v L="$legacy" '$2 <= L' "$W/ratchet.find" | wc -l | tr -d ' ')
	if [ "$new" -ge 1 ] && [ "$old" -eq 0 ]; then
		pass "ratchet: clang-tidy-diff reports $new finding(s) on new lines, 0 on the $legacy legacy lines"
	else
		fail "ratchet: new-line findings=$new, legacy findings=$old (want >=1 and 0)"
	fi
fi

finish
