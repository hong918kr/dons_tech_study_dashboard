#!/usr/bin/env bash
# TOOL-B03 checker: "the same, correct answer at every optimization level".
#   bash check.sh starter|solution|practice
# For the variant file <variant>.c:
#   1. build at -O0 -O1 -O2 -O3 -Os -Oz with -std=c11 -Wall -Wextra -Werror
#   2. run each binary: its own CHECK harness must print "N/N passed", exit 0
#   3. the six stdout transcripts must be byte-identical
#   4. -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all: clean, exit 0
#   practice only: 5. at -O2 and -Oz, delay_spin must still contain a loop
#      (a backward branch in objdump -d) - the busy-wait was not deleted.
# Works from any cwd. Writes only to /tmp/TOOL-B03_work/<variant>.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk # 27.2 SDK link is broken

V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh starter|solution|practice"; exit 2 ;;
esac

W=/tmp/TOOL-B03_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2
CFLAGS="-std=c11 -Wall -Wextra -Werror"
LEVELS="O0 O1 O2 O3 Os Oz"
OBJDUMP=/usr/bin/objdump # Apple LLVM objdump (/usr/local/bin one is LLVM 3.2)
PASS=0
TOTAL=0

ok() { PASS=$((PASS + 1)); TOTAL=$((TOTAL + 1)); echo "PASS $*"; }
bad() { TOTAL=$((TOTAL + 1)); echo "FAIL $*"; }

# ---- 1+2: build and run at every level ----------------------------------
for O in $LEVELS; do
	if cc $CFLAGS -$O "$V.c" -o "$W/$V.$O" 2>"$W/build.$O.log"; then
		ok "build -$O"
	else
		bad "build -$O"; sed 's/^/     /' "$W/build.$O.log" | head -8
		continue
	fi
	"$W/$V.$O" >"$W/out.$O.txt" 2>"$W/err.$O.txt"
	rc=$?
	last=$(tail -1 "$W/out.$O.txt")
	if [ $rc -eq 0 ]; then
		ok "run   -$O  ($last)"
	else
		bad "run   -$O  ($last, exit $rc)"
		grep '^FAIL' "$W/out.$O.txt" | sed 's/^/     /'
	fi
done

# ---- 3: identical output across levels ----------------------------------
diff_levels=""
for O in $LEVELS; do
	[ "$O" = O0 ] && continue
	if [ ! -f "$W/out.$O.txt" ] || ! cmp -s "$W/out.O0.txt" "$W/out.$O.txt"; then
		diff_levels="$diff_levels -$O"
	fi
done
if [ -f "$W/out.O0.txt" ] && [ -z "$diff_levels" ]; then
	ok "stdout identical at all 6 levels"
else
	bad "stdout differs from -O0 at:$diff_levels"
fi

# ---- 4: sanitizers ------------------------------------------------------
if cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	"$V.c" -o "$W/$V.san" 2>"$W/build.san.log"; then
	("$W/$V.san" >"$W/out.san.txt" 2>"$W/err.san.txt"; exit $?) 2>/dev/null # abort: rc 134
	rc=$?
	if [ $rc -eq 0 ] && ! grep -qE 'runtime error|AddressSanitizer' "$W/err.san.txt"; then
		ok "ASan+UBSan clean (-O1)"
	else
		bad "ASan+UBSan (exit $rc)"
		grep -E 'runtime error|ERROR: AddressSanitizer' "$W/err.san.txt" | head -3 |
			sed -e 's/^/     /' -e "s#$PWD/##"
	fi
else
	bad "ASan+UBSan build"
fi

# ---- 5 (practice): the busy-wait loop survived optimization -------------
# Number of backward branches (loops) in function $2 of linked binary $1.
backward_branches() {
	"$OBJDUMP" -d --no-show-raw-insn --disassemble-symbols="_$2" "$1" | awk '
		function hex(s,   v, k) {
			s = tolower(s); sub(/^0x/, "", s); v = 0
			for (k = 1; k <= length(s); k++)
				v = v * 16 + index("0123456789abcdef", substr(s, k, 1)) - 1
			return v
		}
		$2 ~ /^(b|b\..*|cbz|cbnz|tbz|tbnz)$/ {
			here = $1; sub(":", "", here); tgt = ""
			for (i = 3; i <= NF; i++) if ($i ~ /^0x[0-9a-f]+$/) tgt = $i
			if (tgt != "" && hex(tgt) <= hex(here)) n++
		}
		END { print n + 0 }'
}
if [ "$V" = practice ]; then
	for O in O2 Oz; do
		n=0
		[ -x "$W/practice.$O" ] && n=$(backward_branches "$W/practice.$O" delay_spin)
		if [ "$n" -ge 1 ]; then
			ok "delay_spin keeps its loop at -$O ($n backward branch)"
		else
			bad "delay_spin has no loop at -$O (optimized away)"
		fi
	done
fi

echo "$PASS/$TOTAL passed"
[ "$PASS" -eq "$TOTAL" ]
