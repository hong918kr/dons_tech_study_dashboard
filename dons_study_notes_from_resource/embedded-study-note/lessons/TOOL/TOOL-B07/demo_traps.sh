#!/usr/bin/env bash
# TOOL-B07 demo: preprocessor traps, observed for real on the Mac (Apple clang).
#   bash demo_traps.sh > traps.txt
# Works in /tmp/TOOL-B07_work/traps; never writes into the lesson dir itself.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B07_work/traps
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
run() { echo "\$ $*"; "$@" 2>&1 | sed "s|$W/||g"; echo "[rc=${PIPESTATUS[0]}]"; }
show() { echo "\$ $*"; bash -c "$*" 2>&1; }

echo "=== T1  #ifdef vs #if with -DFEATURE_USB=0"
printf '#ifdef FEATURE_USB\nint ifdef_on;\n#endif\n#if FEATURE_USB\nint if_on;\n#endif\n' >t1.c
run cc -DFEATURE_USB=0 -E -P t1.c
run cc -DFEATURE_USB -E -P t1.c

echo "=== T2  undefined identifier in #if silently becomes 0 (typo) -> -Wundef"
printf '#define FEATURE_USB 1\n#if FEATUER_USB\nint usb_on;\n#endif\nint end;\n' >t2.c
run cc -E -P t2.c
run cc -Wundef -E -P t2.c

echo "=== T3  empty macro / empty -D value inside #if"
printf '#define DEBUG\n#if DEBUG\n#endif\n' >t3a.c
run cc -E -P t3a.c
printf '#if FEATURE_USB != 0\nint on;\n#endif\n' >t3b.c
run cc -DFEATURE_USB= -E -P t3b.c

echo "=== T4  the preprocessor knows no types: sizeof / casts in #if"
printf '#if sizeof(int) == 4\n#endif\n' >t4.c
run cc -E -P t4.c

echo "=== T5  #if arithmetic is intmax_t/uintmax_t: -1 > 0u"
printf '#if -1 > 0u\nint minus_one_is_bigger;\n#else\nint normal;\n#endif\n' >t5.c
run cc -E -P t5.c

echo "=== T6  'defined' produced by a macro expansion"
printf '#define HAS(x) defined(x)\n#if HAS(FEATURE_USB)\nint a;\n#endif\nint b;\n' >t6.c
run cc -Wall -Wextra -E -P t6.c
run cc -Wall -Wextra -Wpedantic -E -P t6.c

echo "=== T7  -D order, -U, and a header that redefines without #ifndef"
echo 'int lvl = LOG_LEVEL;' >t7.c
run cc -DLOG_LEVEL=2 -DLOG_LEVEL=4 -E -P t7.c
run cc -DLOG_LEVEL=2 -ULOG_LEVEL -DLOG_LEVEL=3 -E -P t7.c
printf '#define LOG_LEVEL 4\nint lvl = LOG_LEVEL;\n' >t7b.c
run cc -DLOG_LEVEL=2 -E -P t7b.c

echo "=== T8  -dM prints the replacement list as written, not its value"
cp "$SRC/practice.h" board_config.h
printf '#include "board_config.h"\nint probe_board = BOARD; int probe_led = LED_PIN;\n' >t8.c
show "cc -DBOARD=BOARD_ID_B -DBUILD_DEBUG -E -dM t8.c | grep -E '^#define (BOARD|LED_PIN) '"
show "cc -DBOARD=BOARD_ID_B -DBUILD_DEBUG -E -P t8.c | grep probe_"

echo "=== T9  #warning (C23; extension in C11) and #pragma message"
printf '#warning "board_config: RX_BUF_SIZE default used"\n#pragma message("building BOARD_B release")\nint x;\n' >t9.c
run cc -std=c11 -c t9.c -o t9.o
run cc -std=c11 -Wpedantic -c t9.c -o t9.o

echo "=== T10 the starter header: silent defaults instead of errors"
cp "$SRC/starter.h" board_config.h && cp "$SRC/app.c" .
for f in "-DBUILD_DEBUG" "-DBOARD_A -DBOARD_B -DBUILD_DEBUG" "-DBOARD_A" "-DBOARD_B -DBUILD_RELEASE -DFEATURE_USB=0" "-DBOARD_A -DBUILD_DEBUG -DRX_BUF_SIZE=512"; do
	echo "\$ cc -std=c11 -Wall -Wextra -Wundef -Werror $f app.c -o app && ./app | head -1"
	cc -std=c11 -Wall -Wextra -Wundef -Werror $f app.c -o app 2>&1 && ./app | head -1
done

echo "=== T11 release ASSERT: (void)sizeof(c) vs ((void)(c)) vs empty -- is c evaluated? type-checked?"
cat >t11.c <<'C'
#include <stdio.h>
#if defined(USE_SIZEOF)
#define ASSERT(c) do { (void)sizeof(c); } while (0)
#elif defined(USE_VOID)
#define ASSERT(c) ((void)(c))
#else
#define ASSERT(c)
#endif
static int calls;
static int check_hw(void) { return ++calls; }
int main(void)
{
    int n = 0;
    ASSERT(n++ == 0);
    ASSERT(check_hw() > 0);
#ifdef TYPO
    ASSERT(no_such_var == 1);
#endif
    printf("n=%d calls=%d\n", n, calls);
    return 0;
}
C
for v in USE_SIZEOF USE_VOID USE_EMPTY; do
	echo "\$ cc -std=c11 -Wall -Wextra -O2 -D$v t11.c && ./a.out"
	cc -std=c11 -Wall -Wextra -O2 -D$v t11.c -o t11 2>&1 && ./t11
	echo "\$ cc -std=c11 -Wall -Wextra -O2 -D$v -DTYPO -fsyntax-only t11.c"
	cc -std=c11 -Wall -Wextra -O2 -D$v -DTYPO -fsyntax-only t11.c 2>&1 | grep -E 'error|warning' | head -2
	echo "[rc=${PIPESTATUS[0]}]"
done
