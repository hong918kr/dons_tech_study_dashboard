#!/usr/bin/env bash
# TOOL-B02 checker: warning-flag baseline.
#   bash check.sh <starter|solution|practice>
# Reads <variant>.flags and, with Mac clang:
#   1. compiles bugs12.c  -> every // BUGnn line must get a warning/error,
#      and no warning may land on an untagged line (no noise);
#   2. compiles good.c (+ third_party/vendor_hal.h) with the flags + -Werror
#      -> must be silent, then runs it -> "good.c: 14/14 self-checks OK";
#   3. practice only: fw_bugs.c (// FWnn) and practice_pragma.c (scoped pragma).
# The GCC / arm-none-eabi-gcc side is shown by demo_matrix.sh / demo_extra.sh.
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
VENDOR_SHA=89154774e1dc15e6700917846c60e2b0238650b4a8438dca2e7de0972c6ea7f1

W=/tmp/TOOL-B02_work/$V
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

[ -f "$V.flags" ] || { fail "missing $V.flags"; finish; }
FLAGS=$(sed -e 's/#.*//' "$V.flags" | tr '\n' ' ' | tr -s ' ')
echo "flags ($V.flags):$FLAGS"
CC="cc -std=c11 -O2"

# ---- 0. rules -----------------------------------------------------------
bad=""
for f in $FLAGS; do
	case "$f" in
	-w | -Weverything | -Wno-everything | -Wno-error | -Wno-error=*) bad="$bad $f" ;;
	esac
done
if [ -z "$bad" ]; then pass "rules: no -w / -Weverything / -Wno-error"; else fail "rules: forbidden flag(s):$bad"; fi

sum=$(shasum -a 256 third_party/vendor_hal.h | cut -d' ' -f1)
if [ "$sum" = "$VENDOR_SHA" ]; then pass "rules: third_party/vendor_hal.h untouched"; else fail "rules: third_party/vendor_hal.h was edited"; fi

# ---- helpers ------------------------------------------------------------
# tag_check <src.c> <BUG|FW|PROBE> <diag file> [digits regex]: one PASS/FAIL per tag
tag_check() {
	local src=$1 pre=$2 diag=$3 dig=${4-[0-9][0-9]} t l d hit name lines
	for t in $(grep -oE "// ${pre}${dig}" "$src" | sed 's|// ||' | sort -u); do
		hit="" lines="" name=""
		for l in $(grep -n "// $t" "$src" | cut -d: -f1); do
			lines="$lines $l"
			d=$(grep -E "^$src:$l:[0-9]+: (warning|error):" "$diag" | head -1)
			if [ -n "$d" ]; then
				hit=$l
				name=$(echo "$d" | grep -oE '\[-W[^]]*\]' | tail -1)
				break
			fi
		done
		if [ -n "$hit" ]; then
			pass "$src $t line $hit $name"
		else
			fail "$src $t line(s)$lines: no diagnostic with these flags"
		fi
	done
}
# noise_check <src.c> <BUG|FW> <diag file>: warnings on untagged lines
noise_check() {
	local src=$1 pre=$2 diag=$3 tagged n=0 where="" l
	tagged=" $(grep -n "// ${pre}[0-9]" "$src" | cut -d: -f1 | tr '\n' ' ') "
	for l in $(grep -oE "^$src:[0-9]+:[0-9]+: (warning|error):" "$diag" | cut -d: -f2); do
		case "$tagged" in
		*" $l "*) ;;
		*) n=$((n + 1)) where="$where $l" ;;
		esac
	done
	if [ "$n" -eq 0 ]; then pass "$src no warnings on untagged lines"; else fail "$src $n warning(s) on untagged line(s):$where"; fi
}

# ---- 1. bugs12.c: every planted bug must be flagged ---------------------
# shellcheck disable=SC2086
$CC $FLAGS -c bugs12.c -o "$W/bugs12.o" 2>"$W/bugs12.diag"
tag_check bugs12.c BUG "$W/bugs12.diag"
noise_check bugs12.c BUG "$W/bugs12.diag"

# ---- 2. good.c: clean under -Werror, then it must run correctly ----------
# shellcheck disable=SC2086
if $CC $FLAGS -Werror good.c -o "$W/good" 2>"$W/good.diag" && [ ! -s "$W/good.diag" ]; then
	pass "good.c builds with -Werror and 0 diagnostics"
	out=$("$W/good" 2>&1)
	rc=$?
	if [ $rc -eq 0 ] && echo "$out" | grep -q '^good.c: 14/14 self-checks OK$'; then
		pass "good.c runs: 14/14 self-checks OK"
	else
		fail "good.c runs: rc=$rc, output: $(echo "$out" | tail -1)"
	fi
else
	fail "good.c builds with -Werror and 0 diagnostics: $(grep -cE '(warning|error):' "$W/good.diag") diagnostic(s), first ones:"
	grep -E '(warning|error):' "$W/good.diag" | head -3 | sed 's/^/     /'
	fail "good.c runs: not built"
fi

# ---- 3. practice: firmware bugs + scoped pragma -------------------------
if [ "$V" = practice ]; then
	# shellcheck disable=SC2086
	$CC $FLAGS -c fw_bugs.c -o "$W/fw_bugs.o" 2>"$W/fw_bugs.diag"
	tag_check fw_bugs.c FW "$W/fw_bugs.diag"
	noise_check fw_bugs.c FW "$W/fw_bugs.diag"

	# shellcheck disable=SC2086
	if $CC $FLAGS -Werror -c practice_pragma.c -o "$W/pp.o" 2>"$W/pp.diag" && [ ! -s "$W/pp.diag" ]; then
		pass "practice_pragma.c: legacy block silenced, -Werror build clean"
	else
		fail "practice_pragma.c: -Werror build: $(grep -E '(warning|error):' "$W/pp.diag" | head -1)"
	fi
	# shellcheck disable=SC2086
	$CC $FLAGS -DSCOPE_PROBE -c practice_pragma.c -o "$W/pp_probe.o" 2>"$W/pp_probe.diag"
	tag_check practice_pragma.c PROBE "$W/pp_probe.diag" ""
	npush=$(grep -c '^#pragma GCC diagnostic push' practice_pragma.c)
	npop=$(grep -c '^#pragma GCC diagnostic pop' practice_pragma.c)
	if [ "$npush" -ge 1 ] && [ "$npush" -eq "$npop" ]; then
		pass "practice_pragma.c: push/pop balanced ($npush/$npop)"
	else
		fail "practice_pragma.c: push/pop unbalanced ($npush/$npop)"
	fi
fi

finish
