#!/usr/bin/env bash
# TOOL-M13 checker: code-size diet of a Cortex-M4 firmware image.
#   bash check.sh <starter|solution|practice>
# Reads <variant>.flags (CFLAGS / LDFLAGS / AR) and <variant>.ld, then on this Mac with
# arm-none-eabi-gcc 16 (no newlib -> -nostdlib, fw/fmt.c instead of printf):
#   1. builds fw/*.c, archives uart.o + sensor.o into libdrv.a with $AR, links fw.elf
#   2. checks the archive index, the vector table, the ISR symbols, what --gc-sections removed,
#      which dead symbols survived (nm), text/data/bss against the budget, debug info, .bin size
#   3. boots fw.elf in qemu-system-arm -M mps2-an386 and compares UART output with golden_uart.txt
# newlib / newlib-nano / printf stages run in the Lima VM: demo_stages.sh (captured stages_vm.txt).
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export PATH=/opt/homebrew/bin:$PATH

W=/tmp/TOOL-M13_work/$V
rm -rf "$W"
mkdir -p "$W"
P=0
F=0
pass() { echo "PASS $*"; P=$((P + 1)); }
fail() { echo "FAIL $*"; F=$((F + 1)); }
finish() {
	echo "$P/$((P + F)) passed"
	[ "$F" -eq 0 ] && exit 0
	exit 1
}

# ---- budget (the requirement, not part of the variant) --------------------
TEXT_MAX=2816 DATA_MAX=32 BSS_MAX=256
[ "$V" = practice ] && TEXT_MAX=1600
DEAD="crc16_ccitt crc32_bitwise crc16_table sensor_selftest sensor_calibrate g_cal_table uart_read_line g_dma_rx log_hexdump"
ISRS="Reset_Handler HardFault_Handler SysTick_Handler Default_Handler"

# ---- read the variant ------------------------------------------------------
getv() { sed -e 's/#.*//' "$V.flags" | sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" | tail -1 | sed 's/[[:space:]]*$//'; }
[ -f "$V.flags" ] && [ -f "$V.ld" ] || { fail "missing $V.flags or $V.ld"; finish; }
CFLAGS=$(getv CFLAGS)
LDFLAGS=$(getv LDFLAGS)
AR=$(getv AR)
[ -n "$AR" ] || AR=ar
echo "CFLAGS =$CFLAGS"
echo "LDFLAGS=$LDFLAGS"
echo "AR     =$AR   ld=$V.ld   budget: text<=$TEXT_MAX data<=$DATA_MAX bss<=$BSS_MAX"

BASE="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -std=c11 -ffreestanding -Wall -Wextra -Werror -Ifw"
for f in startup main crc fmt libc_min uart sensor; do
	# shellcheck disable=SC2086
	arm-none-eabi-gcc $BASE $CFLAGS -c "fw/$f.c" -o "$W/$f.o" 2>>"$W/compile.err" || {
		fail "compile fw/$f.c: $(head -3 "$W/compile.err")"
		finish
	}
done

# ---- 1. archive: does the symbol index know the members' symbols? ------------
rm -f "$W/libdrv.a"
"$AR" rcs "$W/libdrv.a" "$W/uart.o" "$W/sensor.o" >"$W/ar.log" 2>&1
idx=$(arm-none-eabi-nm -s "$W/libdrv.a" 2>/dev/null | sed -n '/^Archive index:/,/^$/p')
if echo "$idx" | grep -q '^uart_init in uart.o' && echo "$idx" | grep -q '^sensor_push in sensor.o'; then
	pass "libdrv.a index lists uart_init/sensor_push ($AR)"
else
	fail "libdrv.a index has no uart_init/sensor_push ($AR: $(head -1 "$W/ar.log")) — re-archiving with arm-none-eabi-gcc-ar to keep grading"
	rm -f "$W/libdrv.a"
	arm-none-eabi-gcc-ar rcs "$W/libdrv.a" "$W/uart.o" "$W/sensor.o"
fi

# ---- 2. link ---------------------------------------------------------------
# shellcheck disable=SC2086
arm-none-eabi-gcc $BASE $CFLAGS $LDFLAGS -nostdlib -T "$V.ld" -Wl,-Map="$W/fw.map" -Wl,--print-gc-sections \
	"$W/startup.o" "$W/main.o" "$W/crc.o" "$W/fmt.o" "$W/libc_min.o" -L"$W" -ldrv -lgcc \
	-o "$W/fw.elf" >"$W/link.out" 2>"$W/link.err"
rc=$?
grep -v "removing unused section" "$W/link.err" >"$W/link.realerr"
if [ $rc -eq 0 ] && [ -f "$W/fw.elf" ]; then
	pass "link fw.elf"
else
	fail "link fw.elf (rc=$rc): $(head -2 "$W/link.realerr" | sed 's#.*/arm-none-eabi-ld: #ld: #' | tr '\n' ' ')"
	finish
fi
arm-none-eabi-nm "$W/fw.elf" >"$W/nm.txt"
sym() { awk -v s="$1" '$3==s{print $1; exit}' "$W/nm.txt"; }
read -r TEXT DATA BSS _ < <(arm-none-eabi-size "$W/fw.elf" | awk 'NR==2')
echo "     size: text=$TEXT data=$DATA bss=$BSS"

# ---- 3. vector table: at 0x0, word0 = _estack, word1 = Reset_Handler|1 -------
arm-none-eabi-objcopy -O binary "$W/fw.elf" "$W/fw.bin"
vline=$(arm-none-eabi-readelf -SW "$W/fw.elf" | grep ' \.isr_vector ')
vaddr=$(echo "$vline" | awk '{for(i=1;i<=NF;i++) if($i==".isr_vector"){print $(i+2); exit}}')
vsize=$(echo "$vline" | awk '{for(i=1;i<=NF;i++) if($i==".isr_vector"){print $(i+4); exit}}')
read -r w0 w1 < <(od -An -tx4 -N8 "$W/fw.bin" | awk '{print $1, $2}')
es=$(sym _estack)
rh=$(sym Reset_Handler)
rh1=""
[ -n "$rh" ] && rh1=$(printf '%08x' $((0x$rh | 1)))
if [ "$vaddr" = "00000000" ] && [ "$vsize" = "000060" ] && [ "$w0" = "$es" ] && [ "$w1" = "$rh1" ]; then
	pass "vector table at 0x0 (0x60 B): SP=0x$w0 Reset=0x$w1"
else
	fail "vector table: .isr_vector addr=${vaddr:-none} size=${vsize:-none}, word0=0x$w0 (want _estack 0x$es) word1=0x$w1 (want 0x$rh1)"
fi

# ---- 4. handlers reachable only through the vector table still exist --------
miss=""
for s in $ISRS; do [ -n "$(sym "$s")" ] || miss="$miss $s"; done
if [ -z "$miss" ]; then pass "ISR symbols present: $ISRS"; else fail "ISR symbols gone:$miss"; fi

# ---- 5. what --gc-sections removed (stderr of --print-gc-sections) ---------
gcl="$W/gc.txt"
grep "removing unused section" "$W/link.err" | sed "s#.*removing unused section '\([^']*\)' in file '$W/\([^']*\)'#\1  \2#" >"$gcl"
if grep -q '^\.isr_vector ' "$gcl"; then
	fail "--gc-sections removed .isr_vector (no KEEP in $V.ld): $(grep '^\.isr_vector ' "$gcl")"
else
	pass "--gc-sections kept .isr_vector"
fi
ndead=$(grep -cE '^\.(text|rodata|data|bss)\.' "$gcl")
if [ "$V" = practice ]; then
	pass "--gc-sections removed $ndead per-function/per-object sections (LTO already dropped dead code before ld)"
elif [ "$ndead" -ge 8 ]; then
	pass "--gc-sections removed $ndead per-function/per-object sections, e.g. $(grep -E '^\.(text|rodata|data|bss)\.' "$gcl" | head -3 | awk '{print $1}' | tr '\n' ' ')"
else
	fail "--gc-sections removed only $ndead per-function sections (need >= 8): $(awk '{print $1}' "$gcl" | sort | uniq -c | tr -s ' ' | tr '\n' ';')"
fi

# ---- 6. dead code / data must be gone from the image -------------------------
left=""
for s in $DEAD; do grep -qE " ${s}(\\.|$)" "$W/nm.txt" && left="$left $s"; done
if [ -z "$left" ]; then pass "dead code/data gone (nm): none of $(echo $DEAD | wc -w | tr -d ' ') dead symbols left"; else fail "dead symbols still in fw.elf:$left"; fi

# ---- 7. budget ---------------------------------------------------------------
[ "$TEXT" -le "$TEXT_MAX" ] && pass "text $TEXT <= $TEXT_MAX" || fail "text $TEXT > $TEXT_MAX (over by $((TEXT - TEXT_MAX)))"
[ "$DATA" -le "$DATA_MAX" ] && pass "data $DATA <= $DATA_MAX" || fail "data $DATA > $DATA_MAX (over by $((DATA - DATA_MAX)))"
[ "$BSS" -le "$BSS_MAX" ] && pass "bss $BSS <= $BSS_MAX" || fail "bss $BSS > $BSS_MAX (over by $((BSS - BSS_MAX)))"

# ---- 8. -g: debug info must be in the ELF (field crash -> line, TOOL-M14) ----
dbg=$(arm-none-eabi-readelf -SW "$W/fw.elf" | grep -c ' \.debug_line ')
elf_sz=$(wc -c <"$W/fw.elf" | tr -d ' ')
bin_sz=$(wc -c <"$W/fw.bin" | tr -d ' ')
if [ "$dbg" -ge 1 ]; then pass "debug info kept (.debug_line in fw.elf)"; else fail "no .debug_line in fw.elf — -g0 saves 0 B of Flash, only makes field crashes unreadable"; fi
if [ "$bin_sz" -eq $((TEXT + DATA)) ]; then
	pass "flash image fw.bin = text+data = $bin_sz B (fw.elf file = $elf_sz B)"
else
	fail "fw.bin $bin_sz B != text+data $((TEXT + DATA)) B"
fi

# ---- 9. it still boots: QEMU mps2-an386, output == golden_uart.txt ----------
# no `timeout` on macOS: perl alarm kills qemu after 10 s (a broken vector table can hang it)
(perl -e 'alarm shift; exec @ARGV' 10 qemu-system-arm -M mps2-an386 -nographic -monitor none \
	-semihosting-config enable=on,target=native -serial file:"$W/uart.txt" -kernel "$W/fw.elf"; exit $?) \
	>"$W/qemu.log" 2>&1   # (… ; exit $?): the "Abort trap" job message lands in qemu.log, not on the screen
qrc=$?
if [ $qrc -eq 0 ] && cmp -s "$W/uart.txt" golden_uart.txt; then
	pass "QEMU boot: exit 0, UART output == golden_uart.txt ($(wc -l <golden_uart.txt | tr -d ' ') lines)"
else
	fail "QEMU boot: rc=$qrc, $(head -1 "$W/qemu.log" | cut -c1-70), uart: $(head -c 60 "$W/uart.txt" 2>/dev/null | tr '\n' '|')"
fi

# ---- 10. practice only: LTO really on, linker-enforced Flash region ---------
if [ "$V" = practice ]; then
	if arm-none-eabi-readelf -SW "$W/main.o" 2>/dev/null | grep -q '\.gnu\.lto_'; then
		pass "objects are LTO IR (.gnu.lto_* sections in main.o)"
	else
		fail "main.o has no .gnu.lto_* sections — CFLAGS needs -flto"
	fi
	flen=$(awk '$1=="FLASH"{print $3; exit}' "$W/fw.map")
	if [ -n "$flen" ] && [ $((flen)) -le 2048 ]; then
		pass "practice.ld FLASH region length $flen <= 2 KB (the linker enforces the budget)"
	else
		fail "practice.ld FLASH region length ${flen:-?} > 2 KB"
	fi
	if grep -qE '^ *FLASH: +[0-9]+ B +2 KB' "$W/link.out"; then
		pass "--print-memory-usage: $(grep -E '^ *FLASH:' "$W/link.out" | tr -s ' ')"
	else
		fail "--print-memory-usage table missing from link output"
	fi
fi

finish
