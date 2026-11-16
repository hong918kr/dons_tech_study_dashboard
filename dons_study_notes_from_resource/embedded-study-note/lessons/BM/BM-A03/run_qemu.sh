#!/bin/bash
# ENV: qemu
# BM-A03 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/BM-A03_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-A03/run_qemu.sh [solution|starter|practice|probe|layout|loader|appalone|nopad]'
# 이미지 두 개 → 하나: boot.elf --objcopy(0xFF 로 0x4000 까지 채움)--> boot_pad.bin + app.bin = merged.bin
# QEMU 는 -kernel merged.bin 을 ELF 가 아니면 raw 이미지로 주소 0 에 로드한다 (hw/arm/armv7m.c armv7m_load_kernel).
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-A03_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.h "$SRC"/*.ld "$W"/ 2>/dev/null
cd "$W" || exit 1

# -Wl,-n (nmagic): 페이지 정렬을 끈다 → ELF 헤더가 주소 0 의 LOAD 세그먼트에 끼어들지 않는다 (아래 appalone 참고)
CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build_app() {
    arm-none-eabi-gcc $CFLAGS -T app.ld -Wl,-n -Wl,-Map=app.map app.c -o app.elf -lgcc || return 1
    arm-none-eabi-objcopy -O binary app.elf app.bin
}
build_boot() {   # build_boot <src.c> <out.elf>
    arm-none-eabi-gcc $CFLAGS -T boot.ld -Wl,-n -Wl,-Map="${2%.elf}.map" "$1" -o "$2" -lgcc || return 1
}
merge() {        # merge <boot.elf> → merged.bin
    arm-none-eabi-objcopy -O binary --pad-to=0x4000 --gap-fill=0xFF "$1" boot_pad.bin
    local n; n=$(stat -c %s boot_pad.bin)
    if [ "$n" -ne 16384 ]; then echo "merge: boot_pad.bin is $n bytes (want 16384) — bootloader too big?"; return 1; fi
    cat boot_pad.bin app.bin > merged.bin
}
run() {          # run <qemu args...>: 여러 번 리셋해도 QEMU 프로세스는 하나. 마지막 부팅의 SYS_EXIT 로 끝난다.
    timeout 20 $QEMU "$@" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
solution|starter)
    build_app && build_boot "$1.c" "$1.elf" && merge "$1.elf" || exit 1
    arm-none-eabi-size "$1.elf" app.elf
    echo "merged.bin = $(stat -c %s merged.bin) bytes (boot 0x0000-0x3fff, app from 0x4000)"
    run -kernel merged.bin ;;
loader)     # 같은 시험을 "합치지 않고": 부트로더 ELF + generic loader 로 app.bin 을 0x4000 에
    build_app && build_boot solution.c solution.elf || exit 1
    run -kernel solution.elf -device loader,file=app.bin,addr=0x4000 | tail -4 ;;
appalone)   # 흔한 실수: 앱 ELF 만 -kernel 로 → 리셋은 주소 0 의 벡터를 읽는다 (거기엔 아무것도 없다)
    build_app || exit 1
    timeout 5 $QEMU -kernel app.elf 2>&1 | cat > appalone.log     # 파이프: QEMU abort 때 셸의 "Aborted" 잡음 방지
    rc=${PIPESTATUS[0]}
    grep -E 'fatal|^R12|PSR' appalone.log
    echo "qemu exit=$rc" ;;
nopad)      # 흔한 실수: 패딩 없이 cat boot.bin app.bin → 앱이 0x4000 이 아닌 곳에 놓인다
    build_app && build_boot solution.c solution.elf || exit 1
    arm-none-eabi-objcopy -O binary solution.elf boot_nopad.bin
    cat boot_nopad.bin app.bin > bad_merged.bin
    echo "boot.bin (no pad) = $(stat -c %s boot_nopad.bin) bytes -> app lands at $(printf '0x%x' $(stat -c %s boot_nopad.bin))"
    run -kernel bad_merged.bin | grep -E 'step 0|app vector|result|guard|passed|qemu exit' | head -8 ;;
layout)     # 크기, 세그먼트, 합친 이미지의 0x0 / 0x4000 내용, 심볼
    build_app && build_boot solution.c solution.elf && merge solution.elf || exit 1
    echo "--- arm-none-eabi-size ---"; arm-none-eabi-size solution.elf app.elf
    echo "--- readelf -l (boot / app) ---"
    arm-none-eabi-readelf -lW solution.elf | grep -E '^  (Type|LOAD)'
    arm-none-eabi-readelf -lW app.elf | grep -E '^  (Type|LOAD)'
    echo "--- merged.bin: offset 0x0000 (boot vectors) / 0x3ff0 (padding) / 0x4000 (app vectors) ---"
    xxd -s 0x0000 -l 16 merged.bin; xxd -s 0x3ff0 -l 16 merged.bin; xxd -s 0x4000 -l 16 merged.bin
    echo "--- symbols ---"
    arm-none-eabi-nm -n solution.elf | grep -E ' (g_vectors|Reset_Handler|SysTick_Handler|boot_jump|_estack)$'
    arm-none-eabi-nm -n app.elf | grep -E ' (g_vectors|Reset_Handler|app_entry|SysTick_Handler|_estack)$'
    echo "--- boot_jump (objdump) ---"
    arm-none-eabi-objdump -d solution.elf | sed -n '/<boot_jump>:/,/^$/p' | head -20 ;;
probe)
    build_boot demo_probe.c demo_probe.elf || exit 1
    run -kernel demo_probe.elf ;;
practice)   # practice.c 는 호스트 C — VM 의 gcc 로도 빌드·실행 (Mac 에선 cc 로)
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    rc=$?; echo "practice exit=$rc"; exit $rc ;;
*) echo "usage: $0 [solution|starter|practice|probe|layout|loader|appalone|nopad]"; exit 2 ;;
esac
