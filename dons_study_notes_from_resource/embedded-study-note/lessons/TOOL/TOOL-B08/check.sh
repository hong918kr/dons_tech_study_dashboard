#!/usr/bin/env bash
# TOOL-B08 checker — bash check.sh <starter|solution|practice>
# Copies proj/ + <variant>.mk (as Makefile) to /tmp/TOOL-B08_work/<variant>, then asserts
# incremental-build behaviour with the real macOS GNU Make 3.81.
# Timestamps are pinned with touch -t (make 3.81 on macOS compares whole seconds only),
# so every verdict is deterministic and no sleep is needed.
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;; esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
MAKE=/usr/bin/make
LESSON=$(pwd)
W=/tmp/TOOL-B08_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2
cp -R proj/. "$W/" && cp "$V.mk" "$W/Makefile" || exit 2
cd "$W" || exit 2

B=build; [ "$V" = practice ] && B=build/debug
EXPECT='[app] taps=4 samples=64 crc=0x8C7F'
pass=0; total=0
ok()  { total=$((total + 1)); pass=$((pass + 1)); echo "PASS $1"; }
bad() { total=$((total + 1)); echo "FAIL $1"; [ -n "${2:-}" ] && echo "     $2"; return 0; }

# all sources -> T0, everything under build/ (dirs too) -> T1: a clean "just built" state
settle() {
	find src include tests -exec touch -t 202601010000 {} + 2>/dev/null
	[ -d build ] && find build -exec touch -t 202601010100 {} +
	return 0
}
# objects make WOULD compile (basename, sorted) and whether it would relink the app
rebuilt() { $MAKE -n "$@" 2>&1 | sed -n 's/.* -c [^ ]* -o [^ ]*\/\([A-Za-z0-9_]*\)\.o$/\1/p' | sort | tr '\n' ' ' | sed 's/ $//'; }
relinks() { $MAKE -n "$@" 2>&1 | grep -c -- "-o $B/app\$"; }
expect_set() {  # name  expected-set  expect-relink(1/0)  [make args]
	local name=$1 want=$2 wl=$3; shift 3
	local got gl; got=$(rebuilt "$@"); gl=$(relinks "$@")
	if [ "$got" = "$want" ] && [ "$gl" = "$wl" ]; then ok "$name -> {$got} relink=$gl"
	else bad "$name" "want {$want} relink=$wl, got {$got} relink=$gl"; fi
}

echo "== $V: $($MAKE --version | head -1) =="
# 1 full build from clean
if $MAKE >build.log 2>&1 && [ "$(./$B/app | tail -1)" = "$EXPECT" ]; then ok "full build, app prints '$EXPECT'"
else bad "full build" "$(tail -3 build.log)"; fi

# 2 nothing to do on an unchanged tree
settle
expect_set "second make" "" 0
if $MAKE -q all; then ok "make -q all: up to date (exit 0)"; else bad "make -q all" "exit $?"; fi

# 3-5 touch one header -> exactly its dependents (direct + transitive) + relink
settle; touch include/board.h
expect_set "touch include/board.h (via dsp.h too)" "crc filter main sensor uart" 1
settle; touch include/hal.h
expect_set "touch include/hal.h" "log main sensor uart" 1
settle; touch include/dsp.h
expect_set "touch include/dsp.h" "crc filter main" 1
# 6 touch one .c
settle; touch src/crc.c
expect_set "touch src/crc.c" "crc" 1

# 7 something else changes the build dir's mtime (new file in it) -> no rebuild
settle; touch "$B/notes.txt"
expect_set "new file inside $B/ (dir mtime bumped)" "" 0
rm -f "$B/notes.txt"

# 8 a header that is no longer included is deleted -> no 'No rule to make target'
printf '#define FW_VERSION "1.2.3"\n' > include/version.h
awk '{ print } /#include "hal.h"/ { print "#include \"version.h\"" }' "$LESSON/proj/src/log.c" > src/log.c
$MAKE >step8a.log 2>&1
settle; cp "$LESSON/proj/src/log.c" src/log.c && rm -f include/version.h
if $MAKE >step8b.log 2>&1 && ! grep -q 'No rule to make target' step8b.log && grep -q -- '-o .*log\.o$' step8b.log
then ok "deleted header (no longer included): build continues, log.o rebuilt"
else bad "deleted header" "$(grep -m1 -E 'No rule|Error|error' step8b.log)"; fi

# 9 a new driver file is picked up without editing the Makefile
settle   # (3.81: a .o made in the same second as app would not look newer -> no relink)
printf 'int watchdog_kick(void);\nint watchdog_kick(void) { return 1; }\n' > src/watchdog.c
if $MAKE >step9.log 2>&1 && [ -f "$B/watchdog.o" ] && grep -q "$B/watchdog.o.*-o $B/app" step9.log
then ok "new src/watchdog.c compiled + linked automatically"
else bad "new src/watchdog.c" "watchdog.o $( [ -f "$B/watchdog.o" ] && echo exists || echo missing )"; fi
rm -f src/watchdog.c

# 10 make clean removes the whole out-of-tree build, keeps sources
$MAKE clean >/dev/null 2>&1
nsrc=$(ls src/*.c include/*.h | wc -l | tr -d ' ')
if [ ! -e build ] && [ "$nsrc" = 9 ]; then ok "make clean: build/ gone, 9 sources kept"
else bad "make clean" "build $( [ -e build ] && echo still-there ) sources=$nsrc"; fi

# 11 parallel build from clean
if $MAKE -j8 >j8.log 2>&1 && [ "$(./$B/app | tail -1)" = "$EXPECT" ] && [ "$(ls $B/*.o | wc -l | tr -d ' ')" = 6 ]
then ok "make -j8 from clean: 6 objects, same output"
else bad "make -j8" "$(tail -3 j8.log)"; fi

# 12 a stray FILE named 'clean' must not block 'make clean'
touch clean
$MAKE clean >clean.log 2>&1
if [ ! -e build ]; then ok "file named 'clean' exists, make clean still runs"
else bad "file named 'clean'" "$(cat clean.log)"; fi
rm -f clean

if [ "$V" = practice ]; then
	echo "-- practice --"
	$MAKE >/dev/null 2>&1
	# P1 debug and release live side by side and don't invalidate each other
	if $MAKE CONFIG=release >rel.log 2>&1 && [ "$(./build/release/app | tail -1)" = "$EXPECT" ] \
	   && grep -q -- '-O2 -DNDEBUG .*-c src/main.c' rel.log
	then ok "P1 CONFIG=release -> build/release (-O2 -DNDEBUG)"; else bad "P1 release build"; fi
	settle
	expect_set "P1 debug still up to date after release build" "" 0
	expect_set "P1 release up to date" "" 0 CONFIG=release
	settle; touch include/dsp.h
	if [ "$(rebuilt CONFIG=release)" = "crc filter main" ] && [ "$(rebuilt)" = "crc filter main" ]
	then ok "P1 touch dsp.h: each config rebuilds its own {crc filter main}"; else bad "P1 per-config deps"; fi
	# P2 test binary shares filter.o/crc.o with the app
	settle
	if $MAKE test >test.log 2>&1 && grep -q 'test_dsp: 3/3 passed' test.log \
	   && grep -q -- "tests/test_dsp.o $B/filter.o $B/crc.o -o $B/test_dsp" test.log
	then ok "P2 make test: 3/3 passed, links shared $B/filter.o $B/crc.o"; else bad "P2 make test" "$(tail -2 test.log)"; fi
	settle; touch include/hal.h
	expect_set "P2 touch hal.h: test target compiles nothing" "" 0 test
	settle; touch include/dsp.h
	expect_set "P2 touch dsp.h: test target" "crc filter test_dsp" 0 test
	# P3 changing CFLAGS rebuilds everything once
	$MAKE >/dev/null 2>&1; settle
	expect_set "P3 EXTRA_CFLAGS=-DTRACE" "crc filter log main sensor uart" 1 EXTRA_CFLAGS=-DTRACE
	$MAKE EXTRA_CFLAGS=-DTRACE >/dev/null 2>&1; settle   # same-second stamp would not look newer
	expect_set "P3 same flags again" "" 0 EXTRA_CFLAGS=-DTRACE
	expect_set "P3 flags back to default" "crc filter log main sensor uart" 1
fi

echo "$pass/$total passed"
[ "$pass" = "$total" ] && exit 0 || exit 1
