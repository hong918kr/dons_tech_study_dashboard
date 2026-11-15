#!/usr/bin/env bash
# TOOL-B07 demo (run on the Mac): the same check.sh matrix with GNU compilers in the Lima VM.
#   bash demo_vm.sh > vm_check.txt
# The Mac home is mounted READ-ONLY in the VM, so the lesson files are copied to VM /tmp first.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
limactl shell elinux -- bash -lc "
set -u
D=/tmp/TOOL-B07_work/vm_src
rm -rf \$D && mkdir -p \$D && cp '$SRC'/{check.sh,app.c,solution.h,practice.h,starter.h} \$D/
cd \$D
echo '### arm-none-eabi-gcc (Cortex-M4F, hard float), solution, compile only'
arm-none-eabi-gcc --version | head -1
CC='arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16' NORUN=1 bash check.sh solution | tail -4
echo '### arm-none-eabi-gcc, practice (FPU guard uses the real GNU predefined macros)'
CC='arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16' NORUN=1 bash check.sh practice | tail -5
echo '### host gcc (aarch64 Linux), solution, build + run'
gcc --version | head -1
CC=gcc bash check.sh solution | tail -2
echo '### GNU wording of #error / -Wundef / empty #if (arm-none-eabi-gcc)'
cp solution.h board_config.h
arm-none-eabi-gcc -std=c11 -DBOARD_A -DBOARD_B -DBUILD_DEBUG -E app.c -o /dev/null 2>&1 | head -3
arm-none-eabi-gcc -std=c11 -DBOARD_A -DBUILD_DEBUG -DRX_BUF_SIZE=512 -c app.c -o /dev/null 2>&1 | grep -m2 -E 'error|note'
printf '#if FEATUER_USB\n#endif\n#define EMPTY\n#if EMPTY\n#endif\n' > traps.c
arm-none-eabi-gcc -Wundef -E traps.c -o /dev/null 2>&1 | grep -E 'warning|error'
"
