#!/usr/bin/env bash
# TOOL-M03 demo (Part C): Cortex-M3 펌웨어 — 명령 테이블 · initcall 을 Flash 의 커스텀 섹션 + 링커 스크립트(KEEP/SORT)로.
#   빌드·검사: Mac arm-none-eabi-gcc 16.2 (-nostdlib, newlib 없음)   실행: VM qemu-system-arm -M mps2-an385
#   bash demo_fw.sh > demo_fw.txt 2>&1
set -u
L=$(cd "$(dirname "$0")" && pwd)
export PATH=/opt/homebrew/bin:$PATH
W=/tmp/TOOL-M03_work/fw; rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
CF="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -ffreestanding -nostdlib -ffunction-sections -fdata-sections -fno-tree-loop-distribute-patterns"
build() {   # build <out.elf> <fw.c> <fw.ld> [extra...]
	local out=$1 src=$2 ld=$3; shift 3
	# shellcheck disable=SC2086
	arm-none-eabi-gcc $CF "$@" -Wl,--gc-sections -T "$ld" "$src" -o "$out" -lgcc 2>&1 | sed -E 's#^/[^ ]*/arm-none-eabi-ld: #ld: #; s#/var/folders/[^ ]*/T//##; s#'"$W"'/##g; s#'"$L"'/##g'
}
qemu() {    # ELF 를 VM 으로 흘려 보내 QEMU 에서 실행
	limactl shell elinux -- bash -c "mkdir -p /tmp/TOOL-M03_work && cat > /tmp/TOOL-M03_work/$1 && timeout 10 qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native -kernel /tmp/TOOL-M03_work/$1; echo \"qemu exit=\$?\"" < "$1"
}
arm-none-eabi-gcc --version | head -1

echo "##### 1. 정상 빌드 (KEEP + SORT + used)"
build fw.elf "$L/fw/fw.c" "$L/fw/fw.ld" -Werror
arm-none-eabi-size -A fw.elf | grep -E '^\.(isr_vector|text|shellcmds|initcalls|data|bss) '
arm-none-eabi-readelf -SW fw.elf | grep -E ' \.(shellcmds|initcalls) '
arm-none-eabi-nm -n fw.elf | grep -E 'shellcmds|initcall|shell_cmd_'
arm-none-eabi-objdump -s -j .shellcmds -j .initcalls fw.elf | tail -n +4
echo "== 함수 포인터 값 (Thumb 비트 = 홀수)"
arm-none-eabi-nm fw.elf | grep -E ' t (cmd_led|cmd_ver|cmd_help|clock_init|gpio_init)$'
echo "== QEMU"
qemu fw.elf

echo; echo "##### 2. .shellcmds 의 KEEP 을 뺀 스크립트 + --gc-sections"
sed 's/KEEP(\*(\.shellcmds))/*(.shellcmds)/' "$L/fw/fw.ld" > nokeep.ld
grep -n '\*(\.shellcmds)' nokeep.ld
build fw_nokeep.elf "$L/fw/fw.c" nokeep.ld -Werror -Wl,--print-gc-sections | grep -E 'shellcmds|initcall'
arm-none-eabi-size -A fw_nokeep.elf | grep -E '^\.(shellcmds|initcalls) '
qemu fw_nokeep.elf

echo; echo "##### 3. KEEP 은 있는데 used 가 없다 (static 객체를 컴파일러가 먼저 버린다)"
sed 's/__attribute__((used, aligned(4), section(".shellcmds")))/__attribute__((aligned(4), section(".shellcmds")))/' "$L/fw/fw.c" > fw_noused.c
build fw_noused.elf fw_noused.c "$L/fw/fw.ld" | grep -E 'warning|error' | head -3
arm-none-eabi-size -A fw_noused.elf | grep -E '^\.(shellcmds) '

echo; echo "##### 4. 섹션에 엉뚱한 2 바이트가 끼면 → ASSERT 가 링크를 멈춘다"
sed 's/KEEP(\*(\.shellcmds))/KEEP(*(.shellcmds)) SHORT(0)/' "$L/fw/fw.ld" > pad.ld
build fw_pad.elf "$L/fw/fw.c" pad.ld -Werror
ls fw_pad.elf 2>&1 | sed 's#.*: ##'
