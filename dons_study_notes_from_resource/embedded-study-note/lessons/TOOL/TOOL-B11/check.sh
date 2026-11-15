#!/usr/bin/env bash
# TOOL-B11 checker (Form S):  bash check.sh <starter|solution|practice>
#   starter|solution : <variant>.c + driver.c + buf.h
#       1. -Wall -Wextra -Werror build (no sanitizer)
#       2..7. ASan build (-fsanitize=address -g -O1), one process per case:
#             exit 0, no "ERROR: AddressSanitizer", exact result line
#       8. leak case under macOS `leaks --atExit` (Mac ASan has no LeakSanitizer)
#   practice : practice.c (fixed code) clean under ASan, the deliberately bad pool
#              access IS caught, and the -DBUGGY build is caught with the right kind.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
# Darwin ASan defaults to abort_on_error=1 (SIGABRT, rc 134 + a crash report).
# Exit with rc 1 instead; keep everything else at defaults.
export ASAN_OPTIONS=abort_on_error=0

V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
W=/tmp/TOOL-B11_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2

PASS=0 TOTAL=0
ok()  { TOTAL=$((TOTAL + 1)); PASS=$((PASS + 1)); echo "PASS $*"; }
bad() { TOTAL=$((TOTAL + 1)); echo "FAIL $*"; }

CFLAGS_STRICT="-std=c11 -Wall -Wextra -Werror"
CFLAGS_ASAN="-std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address"

# run_clean <bin> <case> <expected line> [env...]: ASan-clean + exact output
run_clean() {
	local bin=$1 c=$2 want=$3; shift 3
	env "$@" "$bin" "$c" >"$W/$c.out" 2>"$W/$c.err"
	local rc=$? got lab=${LABEL:-$c}
	got=$(head -n 1 "$W/$c.out")
	if grep -q "ERROR: AddressSanitizer" "$W/$c.err"; then
		bad "$lab  rc=$rc  $(grep -m1 '^SUMMARY:' "$W/$c.err")"
	elif [ $rc -ne 0 ]; then
		bad "$lab  rc=$rc  $(head -n 1 "$W/$c.err")"
	elif [ "$got" != "$want" ]; then
		bad "$lab  output '$got'  want '$want'"
	else
		ok "$lab  ($got)"
	fi
}

# run_caught <bin> <case> <asan kind> [env...]: ASan MUST report this kind
run_caught() {
	local bin=$1 c=$2 kind=$3; shift 3
	env "$@" "$bin" "$c" >"$W/$c.bug.out" 2>"$W/$c.bug.err"
	local rc=$? lab=${LABEL:-$c}
	if [ $rc -ne 0 ] && grep -q "ERROR: AddressSanitizer: $kind" "$W/$c.bug.err"; then
		ok "$lab  caught: $(grep -m1 '^SUMMARY:' "$W/$c.bug.err" | sed 's/^SUMMARY: //')"
	else
		bad "$lab  expected ASan '$kind' (rc=$rc): $(grep -m1 '^SUMMARY:' "$W/$c.bug.err")"
	fi
}

if [ "$V" = practice ]; then
	if cc $CFLAGS_STRICT -O2 practice.c -o "$W/p_plain" 2>"$W/build.log"; then
		ok "build -Werror (no sanitizer; POISON/UNPOISON compile to nothing)"
	else
		bad "build -Werror"; cat "$W/build.log"
	fi
	cc $CFLAGS_ASAN practice.c -o "$W/p_asan" 2>"$W/asan.log" &&
		cc $CFLAGS_ASAN -DBUGGY practice.c -o "$W/p_bug" 2>>"$W/asan.log" ||
		{ cat "$W/asan.log"; echo "FAIL ASan build"; echo "0/1 passed"; exit 1; }
	UAR=ASAN_OPTIONS=abort_on_error=0:detect_stack_use_after_return=1
	run_clean "$W/p_asan" scope   "scope: tag=kind=2 n=6"
	run_clean "$W/p_asan" ret     "ret: err=E042" "$UAR"
	run_clean "$W/p_asan" overlap "overlap: left=9 next=AT+GMR"
	run_clean "$W/p_asan" pool    "pool: sum=340 reuse=blk0"
	run_caught "$W/p_asan" pool_bad use-after-poison
	LABEL="scope/BUGGY"   run_caught "$W/p_bug" scope   stack-use-after-scope
	LABEL="ret/BUGGY"     run_caught "$W/p_bug" ret     stack-use-after-return "$UAR"
	LABEL="overlap/BUGGY" run_caught "$W/p_bug" overlap memcpy-param-overlap
else
	if cc $CFLAGS_STRICT -O2 "$V.c" driver.c -o "$W/t_strict" 2>"$W/build.log"; then
		ok "build -Werror ($V.c + driver.c)"
	else
		bad "build -Werror"; cat "$W/build.log"
	fi
	cc $CFLAGS_ASAN "$V.c" driver.c -o "$W/t_asan" 2>"$W/asan.log" ||
		{ cat "$W/asan.log"; echo "FAIL ASan build"; echo "$PASS/$((TOTAL + 1)) passed"; exit 1; }
	run_clean "$W/t_asan" heap   'heap: str="TEMP=23.5C" strlen=10'
	run_clean "$W/t_asan" stack  "stack: hex=A502050001020304"
	run_clean "$W/t_asan" global "global: counts=1,1,1,2 rejected=2"
	run_clean "$W/t_asan" uaf    "uaf: drained=3 bytes=31 left=0"
	run_clean "$W/t_asan" dfree  "dfree: len=0 frame=null"
	LABEL="leak/asan" run_clean "$W/t_asan" leak "leak: good=3 bad_rejected=3" # Mac: no LSan

	# leak: plain -g build under macOS leaks(1). `leaks` exits 1 when it finds leaks.
	cc -std=c11 -g -O1 "$V.c" driver.c -o "$W/t_plain" 2>>"$W/build.log"
	MallocStackLogging=1 leaks --atExit -- "$W/t_plain" leak >"$W/leaks.txt" 2>&1
	lrc=$?
	summary=$(grep -m1 -E 'Process [0-9]+: [0-9]+ leaks? for' "$W/leaks.txt" | sed -E 's/^Process [0-9]+: //')
	if [ $lrc -eq 0 ] && grep -q ' 0 leaks for 0 total leaked bytes' "$W/leaks.txt"; then
		ok "leak/leaks  (leaks --atExit: $summary)"
	else
		bad "leak/leaks  (leaks --atExit rc=$lrc: ${summary:-no summary, see $W/leaks.txt})"
	fi
fi

echo "$PASS/$TOTAL passed"
[ "$PASS" -eq "$TOTAL" ]
