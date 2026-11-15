#!/bin/bash
# ENV: qemu
# BM-B05 — Lima VM `elinux` 안에서 fw.c 를 good/bad 두 번 빌드하고 size/map 출력을 캡처한다.
# (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1). Mac 홈은 VM 에서 읽기 전용 → /tmp/BM-B05_work 에서 빌드.
#   L=<ROOT>/lessons/BM/BM-B05
#   limactl shell elinux -- bash -lc "bash $L/run_vm.sh capture"   # 빌드 + 캡처 파일을 /tmp/BM-B05_work/out 에
#   limactl copy elinux:/tmp/BM-B05_work/out/'*' "$L"/              # (Mac 에서) 캡처를 레슨 폴더로 가져오기
#   limactl shell elinux -- bash -lc "bash $L/run_vm.sh run"       # 두 이미지를 QEMU netduinoplus2 에서 실행
#   limactl shell elinux -- bash -lc "bash $L/run_vm.sh overflow"  # .bss 를 키워 ld 의 region overflow 에러 보기
#   limactl shell elinux -- bash -lc "bash $L/run_vm.sh probe"     # objdump -h, Reset_Handler 루프, rx_buf 최대 크기
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-B05_work
mkdir -p "$W/out"
cp "$SRC/fw.c" "$SRC/fw.ld" "$W"/
cd "$W" || exit 1

# 4096 점 12-bit 사인 (2048 ± 2047) — 생성된 헤더 (DAC 파형 테이블)
awk 'BEGIN { pi = atan2(0, -1); for (i = 0; i < 4096; i++) {
        v = 2048 + 2047 * sin(2 * pi * i / 4096); printf "%d,%s", int(v + 0.5), (i % 16 == 15) ? "\n" : " " } }' > lut_gen.h

CFLAGS="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -std=c11 -O2 -g -Wall -Wextra -Werror \
 -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -ffunction-sections -fdata-sections"
LDFLAGS="-T fw.ld -Wl,--gc-sections -Wl,--print-memory-usage"

build() {   # $1 = good|bad, $2.. = 추가 플래그
    local v=$1; shift
    arm-none-eabi-gcc $CFLAGS "$@" -c fw.c -o fw_$v.o &&
    arm-none-eabi-gcc $CFLAGS fw_$v.o $LDFLAGS -Wl,-Map=fw_$v.map -o fw_$v.elf -lgcc
}

case "${1:-capture}" in
capture)
    for v in good bad; do
        if [ $v = bad ]; then F=-DBAD; else F=; fi
        echo "=== build $v ($F) ==="
        build $v $F > out/ldmem_$v.txt 2>&1 || { cat out/ldmem_$v.txt; exit 1; }
        cat out/ldmem_$v.txt
        arm-none-eabi-size -A -x fw_$v.elf > out/size_A_$v.txt
        arm-none-eabi-size -B fw_$v.elf    > out/size_B_$v.txt
        # map: 메모리 설정 + 출력 섹션 배치(Linker script and memory map) 부분만. 디버그 섹션 이후는 자른다
        awk '/^Memory Configuration/{p=1} /^\.debug_info|^\.ARM\.attributes/{p=0} p' fw_$v.map > out/fw_$v.map
        arm-none-eabi-nm -S --size-sort fw_$v.elf | grep -v _estack | tail -8 > out/nm_$v.txt
        cat out/size_A_$v.txt out/size_B_$v.txt out/nm_$v.txt
    done
    ls -l out/ ;;
run)
    for v in good bad; do
        [ -f fw_$v.elf ] || { echo "run capture first"; exit 1; }
        timeout 10 qemu-system-arm -M netduinoplus2 -nographic -monitor none -serial none \
            -semihosting-config enable=on,target=native -kernel fw_$v.elf
        echo "fw_$v: qemu exit=$?"
    done ;;
overflow)
    echo "=== good build with RX_BUF_SIZE = 120 KB ==="
    build ovf -DRX_BUF_SIZE=122880u > out/ldmem_ovf.txt 2>&1
    rc=$?
    sed -i 's|/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/||' out/ldmem_ovf.txt   # 긴 경로만 줄임
    cat out/ldmem_ovf.txt
    echo "link exit=$rc" ;;
probe)      # 연습 2·4 와 풀이의 근거: 섹션 플래그, Reset_Handler 루프, rx_buf 최대 크기 경계
    [ -f fw_good.elf ] || { echo "run capture first"; exit 1; }
    arm-none-eabi-objdump -h fw_good.elf | sed -n '/Idx/,/debug_info/p' | head -14
    for v in good bad; do arm-none-eabi-objcopy -O binary fw_$v.elf fw_$v.bin; done
    wc -c fw_good.elf fw_good.bin fw_bad.bin          # ELF 파일 크기 ≠ Flash 사용량
    arm-none-eabi-objdump -d --no-show-raw-insn fw_good.elf | sed -n '/<Reset_Handler>:/,/^$/p' | head -24
    for sz in 122844 122845; do
        build edge$sz -DRX_BUF_SIZE=${sz}u 2>&1 | sed 's|.*/bin/ld:|ld:|' | grep -E "overflow|RAM:"
        echo "RX_BUF_SIZE=$sz link exit=${PIPESTATUS[0]}"
    done ;;
esac
