#!/usr/bin/env bash
# TOOL-M01 데모 (맥, arm-none-eabi-gcc 16 + binutils 2.47): 링커 스크립트가 만든 결과를 도구로 읽는다.
#   bash demo_mac.sh > demo_mac.txt
# 작업 파일은 /tmp/TOOL-M01_work/demo 에만 만든다 (402 MB .bin 실험도 거기서 만들고 바로 지운다).
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
export PATH=/opt/homebrew/bin:$PATH
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-M01_work/demo
rm -rf "$D"
mkdir -p "$D"
cd "$D" || exit 2
CF="-mcpu=cortex-m4 -mthumb -Os -g -std=c11 -Wall -Wextra -Werror -ffreestanding -nostdlib -ffunction-sections -fdata-sections"
hdr() { echo; echo "===== $* ====="; }
# link <out.elf> <script> [추가 -D...] : 컴파일 + 링크, ld 메시지는 <out>.ld.log
link() {
	local out=$1 lds=$2
	shift 2
	arm-none-eabi-gcc $CF "$@" -c "$L/fw.c" -o "${out%.elf}.o" &&
		arm-none-eabi-gcc $CF -Wl,--gc-sections -Wl,--print-gc-sections -Wl,--print-memory-usage \
			-Wl,-Map="${out%.elf}.map" -T "$lds" "${out%.elf}.o" -o "$out" >"${out%.elf}.ld.log" 2>&1
	echo "link $out rc=$?"
}

hdr "0. 도구 버전"
arm-none-eabi-gcc --version | head -1
arm-none-eabi-ld --version | head -1

hdr "1. fw.o 의 입력 섹션 (링커가 받는 재료)"
arm-none-eabi-gcc $CF -c "$L/fw.c" -o fw.o
arm-none-eabi-readelf -S -W fw.o | grep -E '\] \.(isr|text|rodata|data|bss)' | sed 's/^ *//'

hdr "2. solution.ld 링크 — ld 메시지 (--print-gc-sections / --print-memory-usage)"
link sol.elf "$L/solution.ld"
sed 's#^.*arm-none-eabi-ld: ##' sol.ld.log

hdr "3. readelf -S (출력 섹션)"
arm-none-eabi-readelf -S -W sol.elf | sed -n '/\[ 0\]/,/\[ 6\]/p'

hdr "4. objdump -h (VMA / LMA 를 한 줄에)"
arm-none-eabi-objdump -h sol.elf | sed -n '/Idx/,/bss/p'

hdr "5. readelf -l (프로그램 헤더: VirtAddr = VMA, PhysAddr = LMA)"
arm-none-eabi-readelf -l -W sol.elf | sed -n '/Entry point/p;/Program Headers/,/^ *01 /p'

hdr "6. readelf -s (링커 스크립트 심볼 + 함수: FUNC 값은 Thumb bit 1)"
arm-none-eabi-readelf -s -W sol.elf | awk 'NR<=3 || $8 ~ /^(_s|_e|Reset_Handler|main|Default_Handler|g_vectors|g_boot_count|g_fw_tag|g_tick|g_rx_buf|k_banner)/' | sed 's/^ *//'
echo "-- nm 은 같은 심볼을 짝수로 보여 준다:"
arm-none-eabi-nm sol.elf | grep -E ' (Reset_Handler|main)$'

hdr "7. 맵 파일: .data 줄의 load address (BM-B05 에서 눈으로 읽던 그것)"
grep -nE '^\.(isr_vector|text|rodata|data|bss) |load address|_sidata = ' sol.map

hdr "8. objcopy -O binary → fw.bin (0x130 부터 끝까지 = .rodata 꼬리 + .data 초기값 20 바이트)"
arm-none-eabi-objcopy -O binary sol.elf sol.bin
ls -l sol.bin | awk '{print $5, "bytes  sol.bin"}'
od -An -tx1 -v -j $((0x130)) sol.bin
echo "-- 벡터 표 처음 8 바이트 (리틀 엔디언 워드: [0]=_estack, [1]=Reset_Handler|1):"
od -An -tx4 -N8 sol.bin
echo "-- objcopy -O ihex (주소 레코드):"
arm-none-eabi-objcopy -O ihex sol.elf sol.hex
head -2 sol.hex
tail -3 sol.hex

hdr "9. Reset_Handler 디스어셈블리 (_sidata/_sdata/_edata 는 리터럴 풀 상수)"
arm-none-eabi-objdump -d --no-show-raw-insn sol.elf | sed -n '/<Reset_Handler>:/,/^$/p'

hdr "10. 버그 B1: .data 에 AT> FLASH 없음 → LMA = VMA = RAM"
sed 's/} > RAM AT> FLASH/} > RAM/' "$L/solution.ld" >b1.ld
link b1.elf b1.ld
arm-none-eabi-readelf -l -W b1.elf | grep -E 'LOAD'
grep -E 'FLASH:|RAM:' b1.ld.log
arm-none-eabi-objcopy -O binary b1.elf b1.bin
ls -l b1.bin | awk '{print $5, "bytes  b1.bin  (0x08000000 .. 0x20000014 을 0 으로 채움)"}'
rm -f b1.bin
echo "-- ihex 로 뽑으면: 확장 선형 주소 레코드(:02000004 HHHH) 가 RAM(0x2000) 을 가리킨다"
arm-none-eabi-objcopy -O ihex b1.elf b1.hex
grep -n ':02000004' b1.hex

hdr "11. 버그 B2: KEEP 없음 → --gc-sections 가 벡터 표를 지운다"
sed 's/KEEP(\*(.isr_vector))/*(.isr_vector)/' "$L/solution.ld" >b2.ld
link b2.elf b2.ld
sed 's#^.*arm-none-eabi-ld: ##' b2.ld.log | grep removing
arm-none-eabi-objcopy -O binary b2.elf b2.bin
echo "-- 0x08000000 의 첫 두 워드 (이제 코드다):"
od -An -tx4 -N8 b2.bin
arm-none-eabi-objdump -d --no-show-raw-insn b2.elf | sed -n '/<crc16_nibble>:/,+3p'
echo "-- 'used' 속성만으로는? (fw.c 의 g_vectors 는 이미 used — 그래도 지워졌다)"
grep -n 'section(".isr_vector"), used' "$L/fw.c"

hdr "12. 버그 B3: _sidata = ADDR(.data)"
sed 's/_sidata = LOADADDR(.data);/_sidata = ADDR(.data);/' "$L/solution.ld" >b3.ld
link b3.elf b3.ld
arm-none-eabi-readelf -s -W b3.elf | awk '$8=="_sidata" || $8=="_sdata"' | sed 's/^ *//'

hdr "13. 버그 B4: 끝 ALIGN(4) 없음 → _edata / _ebss 가 홀수"
awk 'BEGIN{n=0} /^        \. = ALIGN\(4\);$/ {n++; if (n==8 || n==10) next} {print}' "$L/solution.ld" >b4.ld
diff "$L/solution.ld" b4.ld
link b4.elf b4.ld
arm-none-eabi-readelf -s -W b4.elf | awk '$8 ~ /^_(s|e)(data|bss)$/ || $8=="g_fw_tag" || $8=="g_rx_buf"' | sed 's/^ *//'

hdr "14. .bss (NOLOAD) 를 붙이면? (입력이 이미 NOBITS 라 결과 동일)"
sed 's/^    \.bss :/    .bss (NOLOAD) :/' "$L/solution.ld" >nl.ld
link nl.elf nl.ld
arm-none-eabi-readelf -S -W nl.elf | grep -E '\] \.bss'
arm-none-eabi-readelf -S -W sol.elf | grep -E '\] \.bss'

hdr "15. 고아(orphan) 섹션: -DPRACTICE 코드를 solution.ld 로 링크 (.noinit/.ccmram 규칙 없음)"
link orph.elf "$L/solution.ld" -DPRACTICE
grep -E 'FLASH:|RAM:' orph.ld.log
arm-none-eabi-readelf -S -W orph.elf | grep -E '\] \.(data|ccmram|bss|noinit)'
arm-none-eabi-readelf -l -W orph.elf | grep -E 'LOAD|^ +0[0-9] '
echo "-- --orphan-handling=warn (alloc 섹션만 추림):"
arm-none-eabi-gcc $CF -Wl,--gc-sections -Wl,--orphan-handling=warn -T "$L/solution.ld" orph.o -o orph2.elf 2>&1 |
	sed 's#^.*arm-none-eabi-ld: ##' | grep -vE "debug|comment|ARM.attributes|glue|veneer|v4_bx"

hdr "16. practice.ld 에서 .ccmram 의 (NOLOAD) 를 빼면?"
sed 's/^    \.ccmram (NOLOAD) :/    .ccmram :/' "$L/practice.ld" >p_noload.ld
link pn.elf p_noload.ld -DPRACTICE
arm-none-eabi-readelf -S -W pn.elf | grep -E '\] \.ccmram'
arm-none-eabi-readelf -l -W pn.elf | grep LOAD
echo "-- objcopy -O binary 를 하면 0x08004000..0x10001000 = $((0x10001000 - 0x08004000)) 바이트 (만들지 않음)"

hdr "17. 맥 ld64 에는 링커 스크립트가 없다"
printf 'int main(void){return 0;}\n' >m.c
cc m.c -Wl,-T,"$L/solution.ld" -o m 2>&1 | head -2
echo "-- 대신 Mach-O 는 세그먼트 주소를 옵션으로: -Wl,-segaddr,__DATA,<addr> / -Wl,-image_base,<addr>"
cc m.c -Wl,-image_base,0x200000000 -o m2 2>&1 | head -2
otool -l m2 | grep -A3 'segname __TEXT' | head -4
