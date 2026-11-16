#!/usr/bin/env bash
# TOOL-M02: build the demo Cortex-M4 firmware twice (v1, v2) with GNU ld map files.
#   bash gen_maps.sh [OUT_DIR]      (default /tmp/TOOL-M02_work/gen)
# Produces OUT_DIR/v{1,2}.elf v{1,2}.map memusage_v*.txt gc_v*.txt (+ objects in OUT_DIR/v{1,2}/). Mac arm-none-eabi-gcc,
# no newlib -> -nostdlib, our own memcpy/memset in libutil.a, libgcc for 64-bit divide.
# Links run inside each build dir with relative names so the map is path-independent.
set -u
cd "$(dirname "$0")" || exit 2
FW=$(pwd)/fw
OUT=${1:-/tmp/TOOL-M02_work/gen}
CC=arm-none-eabi-gcc
AR=arm-none-eabi-ar
CFLAGS="-mcpu=cortex-m4 -mthumb -Os -std=c11 -Wall -Wextra -Werror -ffreestanding \
-ffunction-sections -fdata-sections"
LIBGCC=$($CC -mcpu=cortex-m4 -mthumb -print-libgcc-file-name)

build() { # build <v1|v2> <extra cflags> <extra objects>
	local v=$1 extra=$2 objs=$3 b=$OUT/$1
	rm -rf "$b"
	mkdir -p "$b"
	(
		cd "$b" || exit 1
		for f in startup main comm sensor $objs; do
			# shellcheck disable=SC2086
			$CC $CFLAGS $extra -c "$FW/$f.c" -o "$f.o" || exit 1
		done
		# shellcheck disable=SC2086
		$CC $CFLAGS -fcommon -c "$FW/legacy_board.c" -o legacy_board.o || exit 1
		for f in crc16 fmt mem selftest; do
			# shellcheck disable=SC2086
			$CC $CFLAGS -c "$FW/util/$f.c" -o "$f.o" || exit 1
		done
		rm -f libutil.a
		$AR rcsD libutil.a crc16.o fmt.o mem.o selftest.o || exit 1
		# shellcheck disable=SC2086
		$CC -mcpu=cortex-m4 -mthumb -nostdlib -T "$FW/fw.ld" \
			-Wl,--gc-sections -Wl,--print-gc-sections -Wl,-Map=$v.map -Wl,--print-memory-usage \
			startup.o main.o comm.o sensor.o legacy_board.o $([ -n "$objs" ] && echo "$objs.o") \
			libutil.a "$LIBGCC" -o $v.elf >memusage.txt 2>gc.txt || { cat gc.txt; exit 1; }
		cp $v.map $v.elf "$OUT/"
		cp memusage.txt "$OUT/memusage_$v.txt"
		cp gc.txt "$OUT/gc_$v.txt"
	) || { echo "gen_maps: build $v failed" >&2; exit 1; }
}

build v1 "" ""
build v2 "-DRX_BUF_SIZE=4096 -DFEATURE_TELEMETRY" "telemetry"
echo "gen_maps: wrote $OUT/v1.map $OUT/v2.map $OUT/v1.elf $OUT/v2.elf"
