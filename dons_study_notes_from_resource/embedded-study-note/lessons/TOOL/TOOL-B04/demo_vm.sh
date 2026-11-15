#!/usr/bin/env bash
# TOOL-B04 demo (Lima VM "elinux"): GNU binutils view of the same anatomy.c.
#   bash demo_vm.sh        (run on the Mac; the VM sees this dir read-only at the same path)
# Builds in VM /tmp/TOOL-B04_work/vm: clang 21 aarch64 (same flags as the Mac ELF), gcc 15 native,
# arm-none-eabi-gcc 14 Cortex-M4, then readelf -h -S -s -r and GNU nm.
set -u
LESSON=$(cd "$(dirname "$0")" && pwd)
limactl shell elinux -- bash -s "$LESSON" <<'VM'
set -u
W=/tmp/TOOL-B04_work/vm
rm -rf "$W" && mkdir -p "$W" && cp "$1/anatomy.c" "$W/" && cd "$W" || exit 1
clang --target=aarch64-linux-gnu -O2 -c anatomy.c -o clang_a64.o || exit 1
gcc -O2 -c anatomy.c -o gcc_a64.o || exit 1
gcc -O0 -c anatomy.c -o gcc_a64_O0.o || exit 1
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 -c anatomy.c -o gcc_m4.o || exit 1
part() { echo "### $1"; echo "\$ ${*:2}"; "${@:2}"; echo; }
part "versions"            bash -c 'clang --version | head -1; gcc --version | head -1; arm-none-eabi-gcc --version | head -1; readelf --version | head -1'
part "clang readelf -h"    readelf -h clang_a64.o
part "clang readelf -S"    readelf -S -W clang_a64.o
part "clang readelf -s"    readelf -s -W clang_a64.o
part "clang readelf -r"    readelf -r -W clang_a64.o
part "clang GNU nm"        nm clang_a64.o
part "gcc -O2 GNU nm"      nm gcc_a64.o
part "gcc -O2 readelf -S"  readelf -S -W gcc_a64.o
part "gcc -O2 readelf -r"  readelf -r -W gcc_a64.o
part "gcc -O0 GNU nm"      nm gcc_a64_O0.o
part "m4 gcc nm"           nm gcc_m4.o
part "m4 gcc readelf -h"   readelf -h gcc_m4.o
part "m4 gcc readelf -r"   readelf -r -W gcc_m4.o
part "m4 gcc disasm"       arm-none-eabi-objdump -d -r gcc_m4.o
VM
