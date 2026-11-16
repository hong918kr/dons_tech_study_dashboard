#!/usr/bin/env bash
# TOOL-M01 링커 스크립트 체커 (맥 전용 — /opt/homebrew/bin 의 arm-none-eabi-gcc 16 + binutils)
#   bash check.sh starter    → starter.ld  (TODO 4곳 → FAIL 이 나와야 정상, exit 1)
#   bash check.sh solution   → solution.ld (전부 PASS, exit 0)
#   bash check.sh practice   → practice.ld + fw.c -DPRACTICE (부트로더 16 KB 오프셋 · .noinit · CCMRAM)
# 링크만 한다 (실행하지 않음). 모든 판정은 readelf / objdump / objcopy / ld 의 실제 출력에서 나온다.
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
V=${1:-}
case "$V" in starter | solution | practice) ;; *) echo "usage: check.sh <starter|solution|practice>"; exit 2 ;; esac

export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk # 규칙상 넣어 둠 (이 체커는 Apple clang 을 안 씀)
export PATH=/opt/homebrew/bin:$PATH
for t in arm-none-eabi-gcc arm-none-eabi-readelf arm-none-eabi-objdump arm-none-eabi-objcopy; do
	command -v $t >/dev/null 2>&1 || { echo "missing tool: $t"; exit 2; }
done

W=/tmp/TOOL-M01_work/$V
rm -rf "$W"
mkdir -p "$W"

CF="-mcpu=cortex-m4 -mthumb -Os -g -std=c11 -Wall -Wextra -Werror -ffreestanding -nostdlib -ffunction-sections -fdata-sections"
LDS=$L/$V.ld
RAM_ORG=$((0x20000000))
RAM_LEN=$((128 * 1024))
if [ "$V" = practice ]; then
	XDEF=-DPRACTICE
	FLASH_ORG=$((0x08004000))
	FLASH_LEN=$((496 * 1024))
else
	XDEF=
	FLASH_ORG=$((0x08000000))
	FLASH_LEN=$((512 * 1024))
fi
FLASH_END=$((FLASH_ORG + FLASH_LEN))
RAM_END=$((RAM_ORG + RAM_LEN))

P=0
T=0
pass() { T=$((T + 1)); P=$((P + 1)); echo "PASS  $1"; }
fail() { T=$((T + 1)); echo "FAIL  $1"; [ -n "${2:-}" ] && echo "        | $2"; }
hex() { printf '0x%08x' "$1"; }

# ---------------------------------------------------------------- build
ELF=$W/fw.elf
arm-none-eabi-gcc $CF $XDEF -c fw.c -o "$W/fw.o" >"$W/cc.log" 2>&1 &&
	arm-none-eabi-gcc $CF -Wl,--gc-sections -Wl,--print-gc-sections -Wl,--print-memory-usage \
		-Wl,-Map="$W/fw.map" -T "$LDS" "$W/fw.o" -o "$ELF" >"$W/ld.log" 2>&1
if [ -f "$ELF" ]; then
	pass "link: arm-none-eabi-gcc -T $V.ld --gc-sections 성공"
else
	fail "link: arm-none-eabi-gcc -T $V.ld 실패" "$(cat "$W/cc.log" "$W/ld.log" 2>/dev/null | head -3)"
	echo "$P/$T passed"
	exit 1
fi

arm-none-eabi-readelf -S -W "$ELF" | sed -nE 's/^ *\[ *[0-9]+\] //p' >"$W/sections.txt" # name type addr off size ...
arm-none-eabi-objdump -h "$ELF" >"$W/objdump_h.txt"
arm-none-eabi-readelf -l -W "$ELF" >"$W/phdrs.txt"
arm-none-eabi-readelf -s -W "$ELF" >"$W/syms.txt" # nm 은 Thumb bit 를 지우고 보여 준다 → readelf -s 원본 값

sec_f() { awk -v n="$1" -v f="$2" '$1==n {print $f; exit}' "$W/sections.txt"; } # f: 2 type 3 addr 5 size
sec_addr() { local a; a=$(sec_f "$1" 3); [ -n "$a" ] && echo $((0x$a)) || echo -1; }
sec_size() { local a; a=$(sec_f "$1" 5); [ -n "$a" ] && echo $((0x$a)) || echo -1; }
sec_type() { sec_f "$1" 2; }
sec_lma() { local a; a=$(awk -v n="$1" '$2==n {print $5; exit}' "$W/objdump_h.txt"); [ -n "$a" ] && echo $((0x$a)) || echo -1; }
sym() { local a; a=$(awk -v n="$1" '$8==n {print $2; exit}' "$W/syms.txt"); [ -n "$a" ] && echo $((0x$a)) || echo -1; }
in_flash() { [ "$1" -ge "$FLASH_ORG" ] && [ "$1" -lt "$FLASH_END" ]; }
in_ram() { [ "$1" -ge "$RAM_ORG" ] && [ "$1" -lt "$RAM_END" ]; }

ENTRY=$(awk '/Entry point/ {print $3}' "$W/phdrs.txt")
RESET=$(sym Reset_Handler)
ESTACK=$(sym _estack)
VEC_A=$(sec_addr .isr_vector)
VEC_S=$(sec_size .isr_vector)
ROD_A=$(sec_addr .rodata)
ROD_S=$(sec_size .rodata)
DATA_A=$(sec_addr .data)
DATA_S=$(sec_size .data)
DATA_L=$(sec_lma .data)
BSS_A=$(sec_addr .bss)
BSS_S=$(sec_size .bss)
SIDATA=$(sym _sidata)
SDATA=$(sym _sdata)
EDATA=$(sym _edata)
SBSS=$(sym _sbss)
EBSS=$(sym _ebss)

# ---------------------------------------------------------------- A. 벡터 표 / 진입점
if [ -n "$ENTRY" ] && [ $((ENTRY)) -eq "$RESET" ] && [ $((RESET & 1)) -eq 1 ]; then
	pass "ENTRY: e_entry $ENTRY == Reset_Handler (Thumb bit 1)"
else
	fail "ENTRY: e_entry=$ENTRY Reset_Handler=$(hex "$RESET")"
fi

if [ "$VEC_A" -eq "$FLASH_ORG" ] && [ "$VEC_S" -eq 64 ]; then
	pass ".isr_vector @ $(hex "$FLASH_ORG"), 0x40 바이트 (16 벡터) — gc 뒤에도 살아 있음"
else
	fail ".isr_vector addr=$(hex "$VEC_A") size=$VEC_S (기대 $(hex "$FLASH_ORG"), 64)"
fi

arm-none-eabi-objcopy -O binary -j .isr_vector "$ELF" "$W/vec.bin" 2>/dev/null
read -r W0 W1 _ <<<"$(od -An -tx4 -N8 "$W/vec.bin" 2>/dev/null) - -"
if [ "${W0:--}" != - ] && [ $((0x$W0)) -eq "$ESTACK" ] && [ "$ESTACK" -eq "$RAM_END" ]; then
	pass "vector[0] = 0x$W0 == _estack == ORIGIN(RAM)+LENGTH(RAM)"
else
	fail "vector[0] = ${W0:-없음}, _estack=$(hex "$ESTACK") (기대 $(hex "$RAM_END"))"
fi
if [ "${W1:--}" != - ] && [ $((0x$W1)) -eq "$RESET" ]; then
	pass "vector[1] = 0x$W1 == Reset_Handler (Thumb bit 1)"
else
	fail "vector[1] = ${W1:-없음}, Reset_Handler=$(hex "$RESET")"
fi

if grep -q "removing unused section '.text.unused_debug_dump'" "$W/ld.log" &&
	! grep -q "removing unused section '.isr_vector'" "$W/ld.log" &&
	[ "$(sym Default_Handler)" -ne -1 ]; then
	pass "--print-gc-sections: unused_debug_dump 제거, .isr_vector · Default_Handler 유지"
else
	fail "--print-gc-sections: 엉뚱한 섹션이 지워졌거나 안 지워짐" "$(grep -o "removing unused section '[^']*'" "$W/ld.log" | tr '\n' ' ')"
fi

# ---------------------------------------------------------------- B. .data VMA / LMA
if in_ram "$DATA_A"; then pass ".data VMA $(hex "$DATA_A") 는 RAM 안"; else fail ".data VMA $(hex "$DATA_A") 가 RAM 밖"; fi

if in_flash "$DATA_L" && [ "$DATA_L" -ne "$DATA_A" ]; then
	pass ".data LMA $(hex "$DATA_L") 는 FLASH 안 (VMA != LMA)"
else
	fail ".data LMA $(hex "$DATA_L") — FLASH 가 아니거나 VMA 와 같다 (AT> FLASH ?)"
fi

ROD_END4=$(((ROD_A + ROD_S + 3) & ~3))
if [ "$DATA_L" -eq "$ROD_END4" ]; then
	pass ".data LMA == .rodata 끝 $(hex "$ROD_END4") (FLASH 에 빈틈 없이 이어 붙음)"
else
	fail ".data LMA $(hex "$DATA_L") != .rodata 끝 $(hex "$ROD_END4")"
fi

PH=$(awk -v v="$(hex "$DATA_A")" '$1=="LOAD" && $3==v {print $4; exit}' "$W/phdrs.txt")
if [ -n "$PH" ] && [ $((PH)) -eq "$DATA_L" ] && in_flash $((PH)); then
	pass "readelf -l: VirtAddr $(hex "$DATA_A") 의 LOAD 세그먼트 PhysAddr $PH (FLASH)"
else
	fail "readelf -l: .data 세그먼트 PhysAddr=${PH:-없음} (FLASH 여야 함)"
fi

if [ "$SIDATA" -eq "$DATA_L" ] && in_flash "$SIDATA"; then
	pass "_sidata $(hex "$SIDATA") == LOADADDR(.data)"
else
	fail "_sidata $(hex "$SIDATA") != LOADADDR(.data) $(hex "$DATA_L") — 스타트업이 RAM 을 RAM 에 복사한다"
fi

if [ "$SDATA" -eq "$DATA_A" ] && [ "$EDATA" -eq $((DATA_A + DATA_S)) ]; then
	pass "_sdata/_edata == .data 경계 [$(hex "$SDATA"), $(hex "$EDATA"))"
else
	fail "_sdata=$(hex "$SDATA") _edata=$(hex "$EDATA") vs .data [$(hex "$DATA_A"), +$DATA_S)"
fi

if [ $((SDATA % 4)) -eq 0 ] && [ $((EDATA % 4)) -eq 0 ] && [ $((SBSS % 4)) -eq 0 ] && [ $((EBSS % 4)) -eq 0 ]; then
	pass "_sdata/_edata/_sbss/_ebss 모두 4-정렬 (uint32_t 루프가 경계를 안 넘음)"
else
	fail "정렬: _edata=$(hex "$EDATA") _ebss=$(hex "$EBSS") — 워드 루프가 끝을 최대 3 바이트 넘는다"
fi

# ---------------------------------------------------------------- C. .bss
if [ "$(sec_type .bss)" = NOBITS ] && [ "$SBSS" -eq "$BSS_A" ] && [ "$EBSS" -eq $((BSS_A + BSS_S)) ] &&
	[ "$BSS_A" -ge $((DATA_A + DATA_S)) ] && in_ram "$BSS_A" && [ $((BSS_A + BSS_S)) -le "$RAM_END" ]; then
	pass ".bss NOBITS, [_sbss,_ebss) == .bss, .data 뒤 RAM 안"
else
	fail ".bss type=$(sec_type .bss) addr=$(hex "$BSS_A") _sbss=$(hex "$SBSS") _ebss=$(hex "$EBSS")"
fi

# ---------------------------------------------------------------- D. 파일 이미지 (.bin)
# objcopy -O binary 는 가장 낮은 LMA 부터 가장 높은 LMA 끝까지를 그대로 쓴다 → 먼저 범위를 계산해 거대 파일을 막는다.
LO=0
HI=0
while read -r _ _ _ pa fs _; do # LOAD Offset VirtAddr PhysAddr FileSiz ...  (FileSiz 0 = NOBITS 뿐)
	[ $((fs)) -gt 0 ] || continue
	if [ "$LO" -eq 0 ] || [ $((pa)) -lt "$LO" ]; then LO=$((pa)); fi
	if [ $((pa + fs)) -gt "$HI" ]; then HI=$((pa + fs)); fi
done < <(awk '$1=="LOAD"' "$W/phdrs.txt")
SPAN=$((HI - LO))
EXPECT_BIN=$((DATA_L + DATA_S - FLASH_ORG))
BIN_OK=0
if [ "$SPAN" -gt $((1024 * 1024)) ]; then
	fail "objcopy -O binary 범위 $SPAN 바이트 ($(hex "$LO")..$(hex "$HI")) — 만들지 않음 (.data LMA 가 RAM?)"
else
	arm-none-eabi-objcopy -O binary "$ELF" "$W/fw.bin"
	BIN=$(wc -c <"$W/fw.bin" | tr -d ' ')
	if [ "$LO" -eq "$FLASH_ORG" ] && [ "$BIN" -eq "$EXPECT_BIN" ]; then
		pass "fw.bin = $BIN 바이트 == LOADADDR(.data)+SIZEOF(.data)-ORIGIN(FLASH) (.bss 는 0 바이트)"
		BIN_OK=1
	else
		fail "fw.bin = $BIN 바이트, 기대 $EXPECT_BIN (시작 $(hex "$LO"))"
	fi
fi

# 스타트업이 읽을 곳(_sidata)에 .data 초기값이 정말 있나: g_boot_count = 7, g_fw_tag = "fw-1.4.2"
if [ "$BIN_OK" -eq 1 ] && ! { [ "$SIDATA" -ge "$FLASH_ORG" ] && [ $((SIDATA + DATA_S)) -le $((FLASH_ORG + BIN)) ]; }; then
	fail "_sidata $(hex "$SIDATA") 는 .bin($(hex "$FLASH_ORG")+$BIN) 밖 — 스타트업이 초기값 대신 다른 것을 복사한다"
elif [ "$BIN_OK" -eq 1 ]; then
	OFF_BC=$((SIDATA - FLASH_ORG + $(sym g_boot_count) - SDATA))
	OFF_TAG=$((SIDATA - FLASH_ORG + $(sym g_fw_tag) - SDATA))
	BC=$(od -An -tu4 -j "$OFF_BC" -N4 "$W/fw.bin" | tr -d ' ')
	TAG=$(dd if="$W/fw.bin" bs=1 skip="$OFF_TAG" count=8 2>/dev/null)
	if [ "$BC" = 7 ] && [ "$TAG" = "fw-1.4.2" ]; then
		pass "_sidata 위치의 .bin 내용: g_boot_count=7, g_fw_tag=\"fw-1.4.2\" (offset 0x$(printf %x "$OFF_BC"), 0x$(printf %x "$OFF_TAG"))"
	else
		fail "_sidata 위치의 .bin 내용: g_boot_count=$BC tag=$TAG"
	fi
else
	fail "_sidata 위치의 .bin 내용 검사 불가 (이미지 없음)"
fi

# --print-memory-usage: FLASH Used == .bin 크기, RAM Used == RAM 맨 끝 섹션 끝 - ORIGIN(RAM)
mem_used() { # region → 바이트 (B / KB 만 처리)
	awk -v r="$1:" '$1==r {n=$2; u=$3; if (u=="KB") n*=1024; print n; exit}' "$W/ld.log"
}
F_USED=$(mem_used FLASH)
R_USED=$(mem_used RAM)
R_TOP=$EBSS # RAM 의 "Used" = 영역 안 마지막 섹션 끝 - ORIGIN (중간 정렬 빈칸 포함)
[ "$V" = practice ] && R_TOP=$(sym _enoinit)
R_EXPECT=$((R_TOP - RAM_ORG))
if [ "${F_USED:-x}" = "$EXPECT_BIN" ] && [ "${R_USED:-x}" = "$R_EXPECT" ]; then
	pass "--print-memory-usage: FLASH $F_USED B (= .bin), RAM $R_USED B (= $([ "$V" = practice ] && echo _enoinit || echo _ebss) - ORIGIN(RAM))"
else
	fail "--print-memory-usage: FLASH=${F_USED:-?} (기대 $EXPECT_BIN) RAM=${R_USED:-?} (기대 $R_EXPECT)"
fi

# ---------------------------------------------------------------- E. practice 전용
if [ "$V" = practice ]; then
	NI_A=$(sec_addr .noinit)
	NI_S=$(sec_size .noinit)
	RR=$(sym g_reset_reason)
	CP=$(sym g_crash_pc)
	if [ "$(sec_type .noinit)" = NOBITS ] && in_ram "$NI_A" && [ "$NI_A" -ge "$EBSS" ] &&
		[ "$(sym _snoinit)" -eq "$NI_A" ] && [ "$(sym _enoinit)" -eq $((NI_A + NI_S)) ] &&
		[ "$RR" -ge "$NI_A" ] && [ "$RR" -lt $((NI_A + NI_S)) ] && [ "$CP" -ge "$NI_A" ] && [ "$CP" -lt $((NI_A + NI_S)) ]; then
		pass ".noinit NOBITS @ $(hex "$NI_A") — _ebss 뒤라 0-채우기 범위 밖, 변수 2개 포함"
	else
		fail ".noinit type=$(sec_type .noinit) addr=$(hex "$NI_A") _ebss=$(hex "$EBSS")"
	fi
	CCM_A=$(sec_addr .ccmram)
	DSP=$(sym g_dsp_buf)
	if [ "$(sec_type .ccmram)" = NOBITS ] && [ "$CCM_A" -eq $((0x10000000)) ] && [ "$DSP" -eq "$CCM_A" ] &&
		[ "$(sec_size .ccmram)" -eq 4096 ]; then
		pass ".ccmram NOBITS @ 0x10000000 (g_dsp_buf 4 KB, 파일·FLASH 0 바이트)"
	else
		fail ".ccmram type=$(sec_type .ccmram) addr=$(hex "$CCM_A") g_dsp_buf=$(hex "$DSP")"
	fi
	if grep -Eq '^ +CCMRAM: +4 KB +64 KB' "$W/ld.log" && grep -Eq '^ +FLASH: .* 496 KB' "$W/ld.log"; then
		pass "--print-memory-usage: FLASH 영역 496 KB (부트로더 16 KB 제외), CCMRAM 4 KB / 64 KB"
	else
		fail "--print-memory-usage 영역 크기" "$(grep -E 'FLASH:|CCMRAM:' "$W/ld.log" | tr -s ' ' | tr '\n' ';')"
	fi
fi

echo "$P/$T passed"
[ "$P" -eq "$T" ]
