#!/bin/bash
# ENV: qemu
# BM-A11 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, python3).
# Mac 홈은 VM 에서 읽기 전용 → 소스를 /tmp/BM-A11_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-A11/run_qemu.sh MODE'
# MODE
#   solution | starter : 빌드 → mkimg.py 로 헤더 도장 → 한 번의 QEMU 실행(5 부팅) → UART 로그 vs expected_uart.txt
#   practice           : practice.c (호스트 C) 를 VM 의 cc 로 빌드·실행
#   probe              : demo_probe.c — QEMU 사실 확인 (ID, UART TX IRQ, timer0, watchdog NMI→리셋)
#   bugs               : 결함 주입 빌드 2 개 (BUG_KICK_IN_ISR, BUG_UNCOND_KICK)
#   corrupt            : 헤더 도장 뒤 payload/헤더 비트 뒤집기, payload_size 거짓말 → 부팅 거부 (exit 3)
#   mem                : arm-none-eabi-size -A/-B, 맵 발췌, 큰 심볼
#   syms               : 주입 라벨 / 헤더 / .noinit 주소, ELF 세그먼트, objcopy 와 mkimg 평평한 이미지 비교
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-A11_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$SRC"/*.py "$SRC"/*.txt "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib \
-fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks"
# -icount shift=4,sleep=off: 가상 시간 = 명령 수 × 16 ns, WFI 중엔 다음 타이머까지 바로 건너뜀 → 결정적이고 빠름
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -icount shift=4,sleep=off \
-semihosting-config enable=on,target=native"

build() {   # build <src.c> <out.elf> [extra flags...]
    local src=$1 out=$2; shift 2
    arm-none-eabi-gcc $CFLAGS "$@" -T a11.ld -Wl,-Map="${out%.elf}.map" "$src" -o "$out" -lgcc || exit 1
}
stamp() {   # stamp <elf> [mkimg options...]: 헤더 자리 주소를 nm 으로 찾아 mkimg.py 로 도장
    local elf=$1; shift
    local a; a=$(arm-none-eabi-nm "$elf" | awk '$3=="_img_hdr"{print "0x"$1}')
    python3 -I mkimg.py "$elf" "$a" --version 1.1.0 "$@" || exit 1
}
run() {     # run <elf> <uart file>: 한 QEMU 프로세스 안에서 여러 번 리셋, 마지막 부팅의 SYS_EXIT 로 끝
    rm -f "$2"
    timeout 20 $QEMU -serial file:"$2" -kernel "$1" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
solution|starter)
    build "$1.c" "$1.elf"
    stamp "$1.elf"
    arm-none-eabi-size "$1.elf"
    run "$1.elf" "$1.uart"; q=$?
    echo "--- UART0 capture ($1.uart, $(wc -l < "$1.uart") lines) ---"
    cat "$1.uart"
    python3 -I logtool.py check expected_uart.txt "$1.uart"; u=$?
    arm-none-eabi-nm -n "$1.elf" > "$1.nm.txt"
    if [ $u -eq 0 ]; then echo "--- logtool decode ---"; python3 -I logtool.py decode "$1.uart" "$1.nm.txt"; fi
    if [ $q -eq 0 ] && [ $u -eq 0 ]; then echo "$1: ALL PASS"; exit 0; fi
    echo "$1: FAIL (qemu exit=$q, uart check=$u)"; exit 1 ;;
practice)
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    rc=$?; echo "practice exit=$rc"; exit $rc ;;
probe)
    build demo_probe.c demo_probe.elf
    rm -f probe.uart
    timeout 20 $QEMU -serial file:probe.uart -kernel demo_probe.elf 2>&1 | cat
    echo "qemu exit=${PIPESTATUS[0]}"; echo "UART0 bytes: $(od -An -c probe.uart | tr -s ' ')" ;;
bugs)
    for b in BUG_KICK_IN_ISR BUG_UNCOND_KICK; do
        echo "=== solution.c -D$b"
        build solution.c "$b.elf" -D$b
        stamp "$b.elf" > /dev/null
        run "$b.elf" "$b.uart" | grep -v '^  logged=' | tail -8
        echo "UART lines: $(wc -l < "$b.uart"), last: $(tail -1 "$b.uart")"
    done ;;
corrupt)
    build solution.c clean.elf
    for v in "payload bit 0x1000 (= byte 0x200, in .text)|--flip-payload-bit 0x1000" \
             "header bit 0x60 (= byte 12 = payload_size LSB, no CRC fix-up)|--flip-hdr-bit 0x60" \
             "payload_size lies (+4, header CRC recomputed)|--payload-size LIE"; do
        label=${v%%|*}; opt=${v#*|}
        cp clean.elf bad.elf
        if [ "$opt" = "--payload-size LIE" ]; then
            a=$(arm-none-eabi-nm bad.elf | awk '$3=="_img_hdr"{print strtonum("0x"$1)}')
            opt="--payload-size $((a + 4))"
        fi
        echo "=== corrupt: $label"
        stamp bad.elf $opt
        run bad.elf bad.uart | grep -v '^  logged='
        echo "UART: $(tr '\n' '|' < bad.uart)"
    done ;;
mem)
    build solution.c solution.elf
    stamp solution.elf > /dev/null
    echo "--- arm-none-eabi-size -B ---"; arm-none-eabi-size -B solution.elf
    echo "--- arm-none-eabi-size -A -x ---"; arm-none-eabi-size -A -x solution.elf | grep -v '^\.debug\|^\.ARM.attr\|^$'
    echo "--- map: memory configuration + section placement ---"
    sed -n '/^Memory Configuration/,/^Linker script/p' solution.map | grep -v '^$\|^Linker script'
    grep -E '^\.(isr_vector|text|data|img_hdr|bss|noinit) ' solution.map
    echo "--- 16 biggest symbols (nm --size-sort; asm 라벨·링커 심볼 제외) ---"
    arm-none-eabi-nm --size-sort -S -r solution.elf | grep -v ' pc_\| _estack$\| _sbss$' | head -16 ;;
syms)
    build solution.c solution.elf
    stamp solution.elf
    arm-none-eabi-nm -n solution.elf | grep -E ' (pc_hang|pc_bus|pc_isr|_img_hdr|g_scen|g_crumb|g_dump|_snoinit|_enoinit|_estack|Timer0_Handler|Uart0Tx_Handler|NMI_Handler|HardFault_Handler|run_loop|main)$'
    arm-none-eabi-readelf -lW solution.elf | sed -n '/Program Headers/,/^$/p'
    readelf -SW solution.elf | grep -E 'img_hdr|noinit'
    echo "--- objcopy -O binary vs mkimg --bin ---"
    arm-none-eabi-objcopy -O binary -j .isr_vector -j .text -j .data -j .img_hdr solution.elf objcopy.bin
    python3 -I mkimg.py solution.elf "0x$(arm-none-eabi-nm solution.elf | awk '$3=="_img_hdr"{print $1}')" \
        --version 1.1.0 --bin mkimg.bin > /dev/null
    ls -l objcopy.bin mkimg.bin | awk '{print $5, $9}'
    cmp objcopy.bin mkimg.bin && echo "identical"
    echo "--- header bytes (last 64 B) ---"; tail -c 64 mkimg.bin | od -An -tx4 ;;
*) echo "usage: $0 [solution|starter|practice|probe|bugs|corrupt|mem|syms]"; exit 2 ;;
esac
