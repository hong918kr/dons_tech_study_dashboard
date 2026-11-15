#!/usr/bin/env bash
# TOOL-B03 demo_delay.sh — how long is "for (i = 0; i < n; i++);" at each -O level?
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/TOOL-B03_work/delay
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
echo "# ns per iteration, n = 20,000,000, best of 5 (Apple clang, $(uname -m)); timings are noisy"
printf '%-4s %8s %8s %10s %8s\n' level plain sink vcounter asm
for O in O0 O1 O2 O3 Os Oz; do cc -std=c11 -Wall -Wextra -Werror -$O "$SRC/demo_delay.c" -o d.$O; done
for round in 1 2 3; do # three passes over the levels: look at all three before trusting a number
	echo "# pass $round"
	for O in O0 O1 O2 O3 Os Oz; do printf '%-4s %s\n' -$O "$(./d.$O)"; done
done
for f in _delay_plain _delay_sink _delay_vcounter _delay_asm; do
	echo "### -O2 $f"
	/usr/bin/objdump -d --no-show-raw-insn --disassemble-symbols=$f d.O2 | sed -n '/^[0-9a-f]* <_/,$p'
done
echo "### Cortex-M4 (clang --target=thumbv7em-none-eabi -O2 -S): delay_plain / delay_sink"
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -O2 -DDELAY_KERNEL_ONLY \
	-S "$SRC/demo_delay.c" -o - |
	awk '/^delay_(plain|sink):/ { p = 1 } /^\t\.(fnend|size)/ { p = 0 } p && !/^\t\.(cfi|p2align|code|thumb_func)/'
