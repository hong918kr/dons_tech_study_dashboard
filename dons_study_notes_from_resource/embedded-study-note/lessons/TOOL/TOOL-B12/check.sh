#!/usr/bin/env bash
# TOOL-B12 checker (Form S): UBSan-clean firmware helpers.
#   bash check.sh starter | solution | practice
# starter/solution: driver.c + <variant>.c
#   1. canary     : UBSan really reports on this toolchain
#   2. per case   : -fsanitize=undefined -fno-sanitize-recover=all, one process per case,
#                   PASS = exit 0, right value, no "runtime error:"
#   3. trap build : -O2 -fsanitize=undefined -fsanitize-trap=undefined (no runtime, firmware form),
#                   every case must exit 0 (SIGTRAP = shell rc 133)
#   4. Cortex-M4  : the module compiles freestanding for thumbv7em, -Werror, with and without traps
# practice: practice.c with a stricter set (+unsigned-integer-overflow,implicit-conversion),
#           its own CHECK lines, a trap build, and a negative control (-DWRAP_OK= must fire).
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in
starter|solution|practice) ;;
*) echo "usage: bash check.sh starter|solution|practice"; exit 2 ;;
esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B12_work/$V
rm -rf "$W" && mkdir -p "$W"
SIZE=/Library/Developer/CommandLineTools/usr/bin/llvm-size   # NOT /usr/local/bin/llvm-size (LLVM 3.2)
CFLAGS="-std=c11 -g -Wall -Wextra -Werror"
UB="-fsanitize=undefined -fno-sanitize-recover=all"
STRICT="-fsanitize=undefined,unsigned-integer-overflow,implicit-conversion -fno-sanitize-recover=all"
TRAP="-fsanitize=undefined -fsanitize-trap=undefined"
M4="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -ffreestanding -Os"

P=0; F=0
pass() { P=$((P + 1)); echo "PASS $*"; }
fail() { F=$((F + 1)); echo "FAIL $*"; }
# run_rc <log> <cmd...>: run with output in <log>, print the exit code.
# Called as $(run_rc ... 2>/dev/null) so bash's "Abort trap: 6" notice stays quiet.
run_rc() { local log=$1; shift; "$@" >"$log" 2>&1; echo $?; }
first_err() { grep -m1 'runtime error:' "$1"; }

# ---- 1. canary: a sanitizer that silently does nothing would make every case "PASS"
printf '#include <limits.h>\nint main(int c, char **v) { (void)v; return (INT_MAX - 1 + c + 1) > 0; }\n' >"$W/canary.c"
if cc -std=c11 -O1 $UB "$W/canary.c" -o "$W/canary" 2>"$W/canary_build.log"; then
    rc=$(run_rc "$W/canary.log" "$W/canary" 2>/dev/null)
    if [ "$rc" -ne 0 ] && grep -q 'runtime error: signed integer overflow' "$W/canary.log"; then
        pass "canary: planted INT_MAX + 1 reported, process stopped (rc $rc)"
    else
        fail "canary: UBSan did not report the planted overflow (rc $rc)"
    fi
else
    fail "canary does not build"; cat "$W/canary_build.log"
fi

if [ "$V" = practice ]; then
    # ---- 2p. practice.c under the stricter set
    if cc $CFLAGS -O1 $STRICT practice.c -o "$W/practice" 2>"$W/build.log"; then
        rc=$(run_rc "$W/run.log" "$W/practice" 2>/dev/null)
        while IFS= read -r line; do          # each CHECK line of practice.c counts
            case "$line" in
            "PASS "*) pass "practice.c: ${line#PASS }" ;;
            "FAIL "*) fail "practice.c: ${line#FAIL }" ;;
            esac
        done <"$W/run.log"
        if [ "$rc" -eq 0 ] && [ -z "$(first_err "$W/run.log")" ]; then
            pass "practice.c strict sanitizer run: rc 0, no runtime error"
        else
            fail "practice.c strict sanitizer run: rc $rc $(first_err "$W/run.log")"
        fi
    else
        fail "practice.c does not build"; cat "$W/build.log"
    fi
    # ---- 3p. trap build
    if cc $CFLAGS -O2 $TRAP practice.c -o "$W/practice_trap" 2>"$W/build_t.log"; then
        rc=$(run_rc "$W/trap.log" "$W/practice_trap" 2>/dev/null)
        if [ "$rc" -eq 0 ]; then pass "practice.c trap build (-O2): rc 0"; else fail "practice.c trap build: rc $rc"; fi
    else
        fail "practice.c trap build does not compile"; cat "$W/build_t.log"
    fi
    # ---- 4p. negative control: without the no_sanitize annotations the strict set must fire
    if cc $CFLAGS -O1 $STRICT -DWRAP_OK= practice.c -o "$W/practice_noannot" 2>"$W/build_n.log"; then
        rc=$(run_rc "$W/noannot.log" "$W/practice_noannot" 2>/dev/null)
        if [ "$rc" -ne 0 ] && grep -q 'runtime error: unsigned integer overflow' "$W/noannot.log"; then
            pass "negative control -DWRAP_OK= : $(first_err "$W/noannot.log" | sed 's/: runtime error:.*//') unsigned wrap reported (rc $rc)"
        else
            fail "negative control: removing WRAP_OK did not trigger unsigned-integer-overflow"
        fi
    else
        fail "negative control build"; cat "$W/build_n.log"
    fi
    echo "$P/$((P + F)) passed"
    [ "$F" -eq 0 ]
    exit
fi

# ---- 2. per case under the UBSan runtime
if ! cc $CFLAGS -O1 $UB driver.c "$V.c" -o "$W/ub" 2>"$W/build.log"; then
    cat "$W/build.log"; fail "build $V.c"; echo "$P/$((P + F)) passed"; exit 1
fi
CASES=$("$W/ub" --list)
for c in $CASES; do
    rc=$(run_rc "$W/$c.log" "$W/ub" "$c" 2>/dev/null)
    err=$(first_err "$W/$c.log")
    if [ "$rc" -eq 0 ] && [ -z "$err" ]; then
        pass "$c"
    elif [ -n "$err" ]; then
        fail "$c  (rc $rc) $(echo "$err" | sed 's/: runtime error: / /')"
    else
        fail "$c  (rc $rc) $(head -1 "$W/$c.log")"
    fi
done

# ---- 3. trap mode (what firmware ships): no runtime, UB -> brk/udf -> SIGTRAP
if cc $CFLAGS -O2 $TRAP driver.c "$V.c" -o "$W/trap" 2>"$W/build_t.log"; then
    trapped=""; wrong=""; n=0
    for c in $CASES; do
        n=$((n + 1))
        rc=$(run_rc "$W/trap_$c.log" "$W/trap" "$c" 2>/dev/null)
        if [ "$rc" -eq 133 ]; then trapped="$trapped $c"; elif [ "$rc" -ne 0 ]; then wrong="$wrong $c(rc $rc)"; fi
    done
    if [ -z "$trapped$wrong" ]; then
        pass "trap build (-O2 -fsanitize-trap=undefined): all $n cases rc 0"
    else
        fail "trap build: SIGTRAP (rc 133) in:${trapped:- none}${wrong:+ | other rc in:$wrong}"
    fi
else
    fail "trap build does not compile"; cat "$W/build_t.log"
fi

# ---- 4. same source as a Cortex-M4 object (firmware build), plain and trap mode
if clang $M4 $CFLAGS -c "$V.c" -o "$W/m4.o" 2>"$W/m4.log" \
   && clang $M4 $CFLAGS $TRAP -c "$V.c" -o "$W/m4_trap.o" 2>>"$W/m4.log"; then
    t0=$("$SIZE" "$W/m4.o" | awk 'NR==2{print $1}')
    t1=$("$SIZE" "$W/m4_trap.o" | awk 'NR==2{print $1}')
    pass "Cortex-M4 object (thumbv7em -Os -Werror): text $t0 B, with UBSan traps $t1 B"
else
    fail "Cortex-M4 object build"; head -5 "$W/m4.log"
fi

echo "$P/$((P + F)) passed"
[ "$F" -eq 0 ]
