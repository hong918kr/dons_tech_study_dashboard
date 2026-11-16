#!/usr/bin/env bash
# TOOL-M13 demo (Mac, arm-none-eabi-gcc 16, -nostdlib): the side questions of the diet.
#   1. -g0 / -g / -g3 (and -O2 / -Oz for contrast): size vs ELF file size vs fw.bin bytes
#   2. KEEP missing but -ffunction-sections on: what --gc-sections takes with the vector table
#   3. linker-enforced budget: solution flags + practice.ld (FLASH 2K) -> "region overflowed"
#   bash demo_mac.sh   -> mac_debug.txt, mac_keep.txt, mac_overflow.txt
set -u
cd "$(dirname "$0")" || exit 2
export PATH=/opt/homebrew/bin:$PATH
W=/tmp/TOOL-M13_work/mac
rm -rf "$W"
mkdir -p "$W"
BASE="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -std=c11 -ffreestanding -Wall -Wextra -Werror -Ifw"
SECT="-ffunction-sections -fdata-sections"

build() { # build <dir> <ld> <CFLAGS> [extra LDFLAGS]  -> $W/<dir>/fw.elf (+fw.bin, link.err)
	local d=$W/$1 ld=$2 cf=$3 lf=${4-}
	mkdir -p "$d"
	for f in startup main crc fmt libc_min uart sensor; do
		# shellcheck disable=SC2086
		arm-none-eabi-gcc $BASE $cf -c "fw/$f.c" -o "$d/$f.o" || return 1
	done
	arm-none-eabi-gcc-ar rcs "$d/libdrv.a" "$d/uart.o" "$d/sensor.o"
	# shellcheck disable=SC2086
	arm-none-eabi-gcc $BASE $cf -nostdlib -T "$ld" -Wl,--gc-sections $lf "$d"/startup.o "$d"/main.o "$d"/crc.o \
		"$d"/fmt.o "$d"/libc_min.o -L"$d" -ldrv -lgcc -o "$d/fw.elf" >"$d/link.out" 2>"$d/link.err" || return 1
	arm-none-eabi-objcopy -O binary "$d/fw.elf" "$d/fw.bin"
}

{
	echo "# Mac $(arm-none-eabi-gcc --version | head -1), solution.ld, $SECT --gc-sections"
	printf '%-10s %5s %4s %4s | %8s %7s | %-16s | %s\n' CFLAGS text data bss elf_file fw.bin "fw.bin sha256" ".debug_* bytes"
	for cf in "-Os -g0" "-Os -g" "-Os -g3" "-O2 -g" "-Oz -g"; do
		n=$(echo "$cf" | tr -d ' -')
		build "$n" solution.ld "$cf $SECT" || { echo "build failed: $cf"; continue; }
		read -r t d b _ < <(arm-none-eabi-size "$W/$n/fw.elf" | awk 'NR==2')
		dbg=0 # macOS awk has no strtonum -> add the hex sizes in bash
		for h in $(arm-none-eabi-readelf -SW "$W/$n/fw.elf" | awk '{for(i=1;i<=NF;i++) if($i ~ /^\.debug_/) print $(i+4)}'); do
			dbg=$((dbg + 0x$h))
		done
		printf '%-10s %5d %4d %4d | %8d %7d | %-16s | %d\n' "$cf" "$t" "$d" "$b" "$(wc -c <"$W/$n/fw.elf")" \
			"$(wc -c <"$W/$n/fw.bin")" "$(shasum -a 256 "$W/$n/fw.bin" | cut -c1-16)" "$dbg"
	done
	echo
	echo "\$ cmp fw.bin(-Os -g0) fw.bin(-Os -g3)"
	cmp "$W/Osg0/fw.bin" "$W/Osg3/fw.bin" && echo "identical: debug info never reaches the chip"
	echo "\$ arm-none-eabi-readelf -SW fw.elf | grep debug    # -Os -g (name, type, addr, off, size, flags)"
	arm-none-eabi-readelf -SW "$W/Osg/fw.elf" | grep ' \.debug_' | sed -E 's/^ +\[ ?[0-9]+\] //' |
		awk '{printf "%-16s %-9s %s %s %s %s\n", $1, $2, $3, $4, $5, $7}'
} >mac_debug.txt 2>&1

{
	echo "# solution.flags (-ffunction-sections -fdata-sections) + starter.ld (no KEEP on .isr_vector)"
	build keep starter.ld "-Os -g $SECT" "-Wl,--print-gc-sections"
	echo "\$ grep -E \"startup.o\" <(--print-gc-sections)"
	grep "removing unused section" "$W/keep/link.err" | grep 'startup.o' | grep -v debug_ |
		sed "s#.*arm-none-eabi-ld: ##; s#$W/keep/##"
	echo "\$ arm-none-eabi-nm fw.elf | grep -E 'Handler|vectors'"
	arm-none-eabi-nm "$W/keep/fw.elf" | grep -E 'Handler|vectors'
	echo "\$ arm-none-eabi-size fw.elf"
	arm-none-eabi-size "$W/keep/fw.elf" | sed "s#$W/keep/##"
	echo "\$ od -An -tx4 -N8 fw.bin    # what the CPU loads as SP and PC at reset"
	od -An -tx4 -N8 "$W/keep/fw.bin"
	echo
	echo "# 2x2: section granularity x KEEP (all -Os -g --gc-sections, gcc-ar). vec = .isr_vector size"
	printf '%-32s %-11s %5s %4s %5s | %-8s %-8s %s\n' CFLAGS ld text data bss vec SysTick "QEMU"
	for cf in "-Os -g" "-Os -g $SECT"; do
		for ld in starter.ld solution.ld; do
			n=m_$(echo "$cf$ld" | tr -c 'a-zA-Z0-9' '_')
			build "$n" "$ld" "$cf" || { echo "build failed"; continue; }
			read -r t d b _ < <(arm-none-eabi-size "$W/$n/fw.elf" | awk 'NR==2')
			vec=$(arm-none-eabi-readelf -SW "$W/$n/fw.elf" | awk '{for(i=1;i<=NF;i++) if($i==".isr_vector") print "0x" $(i+4)}')
			st=$(arm-none-eabi-nm "$W/$n/fw.elf" | grep -q ' SysTick_Handler$' && echo kept || echo GONE)
			(perl -e 'alarm shift; exec @ARGV' 10 qemu-system-arm -M mps2-an386 -nographic -monitor none \
				-semihosting-config enable=on,target=native -serial file:"$W/$n/uart.txt" -kernel "$W/$n/fw.elf"; exit $?) >"$W/$n/qemu.log" 2>&1
			q=$?
			if [ $q -eq 0 ] && cmp -s "$W/$n/uart.txt" golden_uart.txt; then q="golden"; else q="rc=$q $(head -1 "$W/$n/qemu.log" | cut -c1-40)"; fi
			printf '%-32s %-11s %5d %4d %5d | %-8s %-8s %s\n' "${cf/$SECT/+ -ffunc/-fdata-sections}" "$ld" "$t" "$d" "$b" "${vec:-none}" "$st" "$q"
		done
	done
} >mac_keep.txt 2>&1

{
	echo "# solution.flags + practice.ld (FLASH LENGTH = 2K), -Wl,--print-memory-usage"
	build over practice.ld "-Os -g $SECT" "-Wl,--print-memory-usage"
	echo "link exit code: $?"
	sed "s#^/opt/homebrew/opt/arm-none-eabi-binutils/bin/##; s#$W/over/##" "$W/over/link.out" "$W/over/link.err"
	echo
	echo "# practice.flags (+ -flto -DCRC_NIBBLE) + practice.ld"
	build fits practice.ld "-Os -g $SECT -flto -DCRC_NIBBLE" "-Wl,--print-memory-usage"
	echo "link exit code: $?"
	cat "$W/fits/link.out"
} >mac_overflow.txt 2>&1
cat mac_debug.txt mac_keep.txt mac_overflow.txt
