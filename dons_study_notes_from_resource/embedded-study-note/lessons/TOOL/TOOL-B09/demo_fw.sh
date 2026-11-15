#!/usr/bin/env bash
# TOOL-B09 demo: Part C -- Cortex-M firmware, newlib-nano + nosys, retarget _write in libuart.a.
# Runs entirely in the Lima VM (arm-none-eabi-gcc 14.2.1).   bash demo_fw.sh > fw.txt 2>&1
set -u
LESSON=$(cd "$(dirname "$0")" && pwd)
limactl shell elinux -- bash -s <<VM
set -u
D=/tmp/TOOL-B09_demo_fw; rm -rf \$D; mkdir -p \$D/objs; cd \$D
CPU="-mcpu=cortex-m4 -mthumb"
for f in uart_hw uart_retarget fw_main; do arm-none-eabi-gcc \$CPU -Os -Wall -Wextra -c $LESSON/src/c/\$f.c -o objs/\$f.o; done
cp objs/fw_main.o .
arm-none-eabi-ar rcs libuart.a objs/uart_hw.o objs/uart_retarget.o
arm-none-eabi-nm -s libuart.a | sed -n '1,6p'
SP="--specs=nano.specs --specs=nosys.specs"
NOISE='_(close|fstat|isatty|lseek|read)(_r)?\b|^/build/.*(closer|fstatr|isattyr|lseekr|readr)\.c'
echo '##### what gcc appends after YOUR objects/libs (collect2 args, -v)'
arm-none-eabi-gcc \$CPU \$SP fw_main.o -L. -luart -o fw.elf -v 2>&1 | grep collect2 | tr ' ' '\n' |
	grep -E '^-l|group|^fw_main|crt0' | sed 's|.*/||' | tr '\n' ' '; echo
for line in "fw_main.o -L. -luart" "fw_main.o -L. -Wl,--undefined=_write -luart" "fw_main.o -L. -luart -Wl,-u,_write"; do
	echo "##### arm-none-eabi-gcc ... \$line"
	arm-none-eabi-gcc \$CPU \$SP \$line -Wl,-y,_write -Wl,-Map=fw.map -o fw.elf 2>&1 | grep -vE "\$NOISE" | sed 's|/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/|<sysroot>/|g'
	echo "rc=\${PIPESTATUS[0]}"
	awk '/^Archive member included/{f=1} f&&/^(Discarded|Memory|Allocating)/{exit} f' fw.map | grep -B1 -E '\((_write|uart_init)\)' | sed 's|/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/|<sysroot>/|g'
	arm-none-eabi-nm -n fw.elf | grep -E ' (uart_init|uart_putc|_write|_write_r)$'
	arm-none-eabi-size fw.elf
	arm-none-eabi-objdump -d fw.elf | grep -A6 '<_write>:'
done
VM
