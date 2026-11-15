#!/usr/bin/env bash
# TOOL-B07 checker — build-variant matrix for board_config.h
#   bash check.sh starter|solution|practice
# The variant header (<variant>.h) is copied to $W/board_config.h next to app.c and
# built with many -D combinations:
#   illegal combo -> `cc -E` must FAIL and print the header's #error text
#   type-level    -> `cc -E` passes, `cc -fsyntax-only` fails with the _Static_assert text
#   legal combo   -> `cc -E -dM` shows the derived macros, `cc -E -P` has/omits code
#                    paths, and the program builds with -Wundef -Werror and prints the
#                    expected config line.
# Cross mode (Lima VM, see demo_vm.sh):
#   CC="arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16" NORUN=1 bash check.sh solution
#   (preprocess + compile only, nothing is linked or run)
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in
starter|solution|practice) ;;
*) echo "usage: check.sh starter|solution|practice"; exit 2 ;;
esac
[ -d /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk ] &&
	export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
CC=${CC:-cc}
NORUN=${NORUN:-0}
W=/tmp/TOOL-B07_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2
cp "$V.h" "$W/board_config.h" && cp app.c "$W/" || exit 2
cd "$W" || exit 2

CFLAGS="-std=c11 -Wall -Wextra -Wundef -Werror"
pass=0
total=0
ok()  { pass=$((pass + 1)); total=$((total + 1)); echo "PASS $1"; }
bad() { total=$((total + 1)); echo "FAIL $1 -- $2"; }
first_err() { grep -m1 -E 'error' <<<"$1" | cut -c1-110; }

# expect_error NAME "text" -D...    : preprocessing must fail with "text"
expect_error() {
	local name=$1 text=$2 out
	shift 2
	if out=$($CC $CFLAGS "$@" -E app.c -o /dev/null 2>&1); then
		bad "$name" "preprocessed fine, expected #error \"$text\""
		return
	fi
	case $out in
	*"$text"*) ok "$name" ;;
	*) bad "$name" "failed, but not with \"$text\": $(first_err "$out")" ;;
	esac
}

# expect_static NAME "text" -D...   : -E passes (not a preprocessor error), compile fails with "text"
expect_static() {
	local name=$1 text=$2 out
	shift 2
	if ! out=$($CC $CFLAGS "$@" -E app.c -o /dev/null 2>&1); then
		bad "$name" "already fails in -E: $(first_err "$out")"
		return
	fi
	if out=$($CC $CFLAGS "$@" -fsyntax-only app.c 2>&1); then
		bad "$name" "compiled fine, expected _Static_assert \"$text\""
		return
	fi
	case $out in
	*"$text"*) ok "$name" ;;
	*) bad "$name" "failed, but not with \"$text\": $(first_err "$out")" ;;
	esac
}

# expect_ok NAME "DM1|DM2" "has1|has2" "not1|not2" "run line" -D...
#   DMn  : exact line "#define DMn" must be in `-E -dM` output
#   has  : fixed string that must appear in `-E -P` output (active code path)
#   not  : fixed string that must NOT appear (dead code path removed)
expect_ok() {
	local name=$1 dms=$2 has=$3 nots=$4 line=$5 out s got
	shift 5
	if ! out=$($CC $CFLAGS "$@" -E -dM app.c -o dm.txt 2>&1); then
		bad "$name" "-E -dM failed: $(first_err "$out")"
		return
	fi
	IFS='|' read -r -a arr <<<"$dms"
	for s in ${arr[@]+"${arr[@]}"}; do
		grep -Fxq "#define $s" dm.txt || { bad "$name" "-dM: missing \"#define $s\" (have: $(grep -E "^#define ${s%% *} " dm.txt | head -1))"; return; }
	done
	$CC $CFLAGS "$@" -E -P app.c -o app.i 2>/dev/null
	IFS='|' read -r -a arr <<<"$has"
	for s in ${arr[@]+"${arr[@]}"}; do
		[ -z "$s" ] && continue
		grep -Fq "$s" app.i || { bad "$name" "-E: active path should contain \"$s\""; return; }
	done
	IFS='|' read -r -a arr <<<"$nots"
	for s in ${arr[@]+"${arr[@]}"}; do
		[ -z "$s" ] && continue
		grep -Fq "$s" app.i && { bad "$name" "-E: dead path still contains \"$s\""; return; }
	done
	if [ "$NORUN" = 1 ]; then
		out=$($CC $CFLAGS "$@" -c app.c -o app.o 2>&1) || { bad "$name" "compile: $(first_err "$out")"; return; }
		ok "$name"
		return
	fi
	out=$($CC $CFLAGS "$@" app.c -o app 2>&1) || { bad "$name" "build: $(first_err "$out")"; return; }
	got=$(./app | head -1)
	[ "$got" = "$line" ] || { bad "$name" "run: got \"$got\""; return; }
	ok "$name"
}

A5="LED_PIN 5|UART_BAUD 115200"
B13="LED_PIN 13|UART_BAUD 921600"
AF='assert_fail("app.c"'

if [ "$V" != practice ]; then
	echo "== $V: illegal combinations must stop in the preprocessor with your #error"
	expect_error  E01_no_board        "exactly one of BOARD_A / BOARD_B (got none)" -DBUILD_DEBUG
	expect_error  E02_both_boards     "exactly one of BOARD_A / BOARD_B (got both)" -DBOARD_A -DBOARD_B -DBUILD_DEBUG
	expect_error  E03_no_build        "define BUILD_DEBUG or BUILD_RELEASE"         -DBOARD_A
	expect_error  E04_both_builds     "mutually exclusive"                          -DBOARD_B -DBUILD_DEBUG -DBUILD_RELEASE
	expect_error  E05_usb_on_A        "FEATURE_USB requires BOARD_B"                -DBOARD_A -DBUILD_DEBUG -DFEATURE_USB=1
	expect_error  E06_usb_on_A_bare   "FEATURE_USB requires BOARD_B"                -DBOARD_A -DBUILD_RELEASE -DFEATURE_USB
	expect_error  E07_shell_release   "FEATURE_SHELL is not allowed in BUILD_RELEASE" -DBOARD_B -DBUILD_RELEASE -DFEATURE_SHELL=1
	expect_error  E08_usb_is_2        "FEATURE_USB must be 0 or 1"                  -DBOARD_B -DBUILD_DEBUG -DFEATURE_USB=2
	expect_error  E09_log_level_7     "LOG_LEVEL must be 0..4"                      -DBOARD_A -DBUILD_RELEASE -DLOG_LEVEL=7
	expect_error  E10_rx_not_pow2     "power of two >= 16"                          -DBOARD_B -DBUILD_DEBUG -DRX_BUF_SIZE=100
	echo "== $V: type-level rule -> _Static_assert (passes -E, fails at compile)"
	expect_static S01_rx_512_on_A     "does not fit rx_idx_t"                       -DBOARD_A -DBUILD_DEBUG -DRX_BUF_SIZE=512
	echo "== $V: legal variants: -dM values, -E code path, -Wundef -Werror build, run"
	expect_ok L01_A_debug "$A5|LOG_LEVEL 4|HAS_USB 0|HAS_SHELL 0|RX_BUF_SIZE 64" \
		"$AF|dbg: boot done" "usb_init|shell_start" \
		"board=A build=debug led=5 baud=115200 log=4 usb=0 shell=0 rx=64" -DBOARD_A -DBUILD_DEBUG
	expect_ok L02_A_release "$A5|LOG_LEVEL 2|HAS_USB 0|HAS_SHELL 0" \
		"" "$AF|dbg: boot done|usb_init|shell_start" \
		"board=A build=release led=5 baud=115200 log=2 usb=0 shell=0 rx=64" -DBOARD_A -DBUILD_RELEASE
	expect_ok L03_B_debug_usb_shell "$B13|LOG_LEVEL 4|HAS_USB 1|HAS_SHELL 1|RX_BUF_SIZE 256" \
		"$AF|usb_init();|shell_start();" "" \
		"board=B build=debug led=13 baud=921600 log=4 usb=1 shell=1 rx=256" -DBOARD_B -DBUILD_DEBUG -DFEATURE_USB=1 -DFEATURE_SHELL=1
	expect_ok L04_B_release_usb0 "$B13|LOG_LEVEL 2|HAS_USB 0" \
		"" "usb_init|$AF" \
		"board=B build=release led=13 baud=921600 log=2 usb=0 shell=0 rx=256" -DBOARD_B -DBUILD_RELEASE -DFEATURE_USB=0
	expect_ok L05_B_release_usb_bare "$B13|HAS_USB 1" \
		"usb_init();" "$AF" \
		"board=B build=release led=13 baud=921600 log=2 usb=1 shell=0 rx=256" -DBOARD_B -DBUILD_RELEASE -DFEATURE_USB
	expect_ok L06_A_debug_log0_shell "$A5|LOG_LEVEL 0|HAS_SHELL 1" \
		"shell_start();|$AF" "dbg: boot done" \
		"board=A build=debug led=5 baud=115200 log=0 usb=0 shell=1 rx=64" -DBOARD_A -DBUILD_DEBUG -DLOG_LEVEL=0 -DFEATURE_SHELL=1
	expect_ok L07_A_debug_rx128 "RX_BUF_SIZE 128" \
		"" "" \
		"board=A build=debug led=5 baud=115200 log=4 usb=0 shell=0 rx=128" -DBOARD_A -DBUILD_DEBUG -DRX_BUF_SIZE=128
else
	echo "== practice: value-style board selection -DBOARD=1|2"
	expect_ok P01_board1_debug "BOARD 1|$A5|LOG_LEVEL 4|HAS_USB 0" \
		"$AF|dbg: boot done" "usb_init" \
		"board=A build=debug led=5 baud=115200 log=4 usb=0 shell=0 rx=64" -DBOARD=1 -DBUILD_DEBUG
	expect_ok P02_board2_release_usb "BOARD 2|$B13|HAS_USB 1" \
		"usb_init();" "$AF" \
		"board=B build=release led=13 baud=921600 log=2 usb=1 shell=0 rx=256" -DBOARD=2 -DBUILD_RELEASE -DFEATURE_USB=1
	expect_ok P03_board_symbolic "BOARD BOARD_ID_B|$B13" \
		"$AF" "" \
		"board=B build=debug led=13 baud=921600 log=4 usb=0 shell=0 rx=256" -DBOARD=BOARD_ID_B -DBUILD_DEBUG
	expect_error P04_board_unset   "BOARD is not set"              -DBUILD_DEBUG
	expect_error P05_board_3       "BOARD must be 1 (A) or 2 (B)"  -DBOARD=3 -DBUILD_DEBUG
	expect_error P06_board_letter  "BOARD must be 1 (A) or 2 (B)"  -DBOARD=B -DBUILD_DEBUG
	expect_error P07_old_flag      "old flag -DBOARD_A/-DBOARD_B"  -DBOARD_A -DBUILD_DEBUG
	expect_error P08_usb_on_1      "FEATURE_USB requires BOARD 2"  -DBOARD=1 -DBUILD_DEBUG -DFEATURE_USB=1
	expect_error P09_both_builds   "mutually exclusive"            -DBOARD=2 -DBUILD_DEBUG -DBUILD_RELEASE

	echo "== practice: IS_ENABLED() (Linux kconfig.h style) on undefined / 1 / 0 / empty"
	cat >probe.c <<'P'
#include "board_config.h"
#define F_ON 1
#define F_OFF 0
#define F_EMPTY
int ie_on = IS_ENABLED(F_ON);
int ie_off = IS_ENABLED(F_OFF);
int ie_none = IS_ENABLED(F_NONE);
int ie_empty = IS_ENABLED(F_EMPTY);
#if IS_ENABLED(F_ON) && !IS_ENABLED(F_NONE)
int ie_in_if = 1;
#endif
P
	if out=$($CC $CFLAGS -Wpedantic -DBOARD=1 -DBUILD_DEBUG -E -P probe.c -o probe.i 2>&1); then
		miss=""
		for s in "ie_on = 1;" "ie_off = 0;" "ie_none = 0;" "ie_empty = 0;" "ie_in_if = 1;"; do
			grep -Fq "$s" probe.i || miss="$miss [$s]"
		done
		[ -z "$miss" ] && ok P10_is_enabled || bad P10_is_enabled "missing:$miss"
	else
		bad P10_is_enabled "$(first_err "$out")"
	fi

	echo "== practice: FPU guard via predefined macros (32-bit ARM target, BOARD 2 = Cortex-M4F)"
	if [ "$CC" = cc ] || [ "$CC" = clang ]; then   # Mac: clang cross target; else the GNU cross gcc
		XT="clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding"
	else
		XT="arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb"
	fi
	printf '#include "board_config.h"\nint cfg_led = LED_PIN;\n' >fw.c
	if out=$($XT $CFLAGS -mfloat-abi=soft -DBOARD=2 -DBUILD_RELEASE -E fw.c -o /dev/null 2>&1); then
		bad P11_m4_soft_board2 "preprocessed fine, expected FPU #error"
	else
		case $out in *"needs -mfloat-abi=hard"*) ok P11_m4_soft_board2 ;;
		*) bad P11_m4_soft_board2 "$(first_err "$out")" ;; esac
	fi
	if out=$($XT $CFLAGS -mfloat-abi=hard -mfpu=fpv4-sp-d16 -DBOARD=2 -DBUILD_RELEASE -fsyntax-only fw.c 2>&1); then
		ok P12_m4_hard_board2
	else
		bad P12_m4_hard_board2 "$(first_err "$out")"
	fi
	if out=$($XT $CFLAGS -mfloat-abi=soft -DBOARD=1 -DBUILD_RELEASE -fsyntax-only fw.c 2>&1); then
		ok P13_m4_soft_board1
	else
		bad P13_m4_soft_board1 "$(first_err "$out")"
	fi
fi

echo "$pass/$total passed"
[ "$pass" -eq "$total" ] && [ "$total" -gt 0 ]
