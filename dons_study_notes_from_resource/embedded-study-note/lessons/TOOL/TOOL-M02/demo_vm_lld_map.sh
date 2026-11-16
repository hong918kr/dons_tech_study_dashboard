#!/usr/bin/env bash
# TOOL-M02 demo (not graded, Lima VM elinux): same v1 firmware, VM arm-none-eabi-gcc 14,
# linked once with GNU ld.bfd and once with ld.lld -> lld's -Map is a column table.
#   limactl shell elinux -- bash /path/to/demo_vm_lld_map.sh    (captured: lld_map_sample.txt)
set -u
SRC=$(cd "$(dirname "$0")" && pwd)/fw
W=/tmp/TOOL-M02_work/lld
rm -rf "$W"
mkdir -p "$W"
cd "$W" || exit 1
CF="-mcpu=cortex-m4 -mthumb -Os -std=c11 -ffreestanding -ffunction-sections -fdata-sections"
for f in startup main comm sensor; do arm-none-eabi-gcc $CF -c "$SRC/$f.c" -o $f.o || exit 1; done
arm-none-eabi-gcc $CF -fcommon -c "$SRC/legacy_board.c" -o legacy_board.o || exit 1
for f in crc16 fmt mem selftest; do arm-none-eabi-gcc $CF -c "$SRC/util/$f.c" -o $f.o || exit 1; done
arm-none-eabi-ar rcsD libutil.a crc16.o fmt.o mem.o selftest.o
LIBGCC=$(arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -print-libgcc-file-name)
OBJS="startup.o main.o comm.o sensor.o legacy_board.o libutil.a $LIBGCC"
arm-none-eabi-ld -T "$SRC/fw.ld" --gc-sections -Map=bfd.map $OBJS -o bfd.elf || exit 1
ld.lld -T "$SRC/fw.ld" --gc-sections -Map=lld.map $OBJS -o lld.elf || exit 1
echo "$(arm-none-eabi-gcc --version | head -1) / $(arm-none-eabi-ld --version | head -1) / $(ld.lld --version)"
echo "--- size: bfd vs lld (same objects, same script)"
arm-none-eabi-size -A bfd.elf | sed -n 3,9p
arm-none-eabi-size -A lld.elf | sed -n 3,9p
echo "--- lld.map (head)"
head -40 lld.map | sed "s#$LIBGCC#<libgcc.a>#"
echo "--- lld.map: .bss part"
grep -n -A12 ' \.bss$' lld.map | head -16
