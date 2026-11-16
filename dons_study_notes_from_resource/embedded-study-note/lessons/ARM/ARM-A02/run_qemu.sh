#!/bin/bash
# ENV: qemu
# ARM-A02 — Lima VM `elinux` (aarch64 Ubuntu) 안에서 실행: 네이티브 gcc 로 베어메탈 AArch64 ELF 를 만들고
# qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 에서 돌린다. Mac 홈은 VM 에서 읽기 전용 → /tmp/ARM-A02_work.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A02/run_qemu.sh [starter|solution|practice|gdb|objdump|bug_nolr|bug_noelr|bug_align]'
# 소스 규칙: <v>.c + <v>.S + shared_*.S + shared_*.c (+ arm_a02.ld). 반환값 = 펌웨어 종료 코드 (starter 1, solution/practice 0).
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-A02_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CC=gcc            # VM 네이티브 aarch64 gcc 15 (= AArch64 컴파일러). 플래그 설명은 ARM-M13 3.
CFLAGS="-std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -nostartfiles -static -fno-pie -no-pie
        -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-asynchronous-unwind-tables
        -fno-tree-loop-distribute-patterns -mbranch-protection=none -Wl,--build-id=none -Wl,--no-warn-rwx-segments"
QEMU="qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 -nographic -monitor none -serial none
      -semihosting-config enable=on,target=native"

build() {   # build <variant> [extra flags...] → <variant>.elf
    local v=$1; shift
    $CC $CFLAGS "$@" -T arm_a02.ld "$v.c" "$v.S" shared_*.S shared_*.c -o "$v.elf" -lgcc
}
run() {     # run <elf> [extra qemu args]: 펌웨어가 SYS_EXIT 로 끝낸다. 안전하게 timeout 20 s.
    local elf=$1; shift
    echo "=== qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 -kernel $elf $* ==="
    timeout 20 $QEMU "$@" -kernel "$elf" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
starter|solution|practice)
    build "$1" || exit 2
    run "$1.elf" ;;
gdb)        # -s -S 로 멈춘 QEMU 에 gdb-multiarch: 첫 타이머 IRQ 의 프레임과 시스템 레지스터
    build solution || exit 2
    $QEMU -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_irq.gdb solution.elf
    kill $QPID 2>/dev/null; wait $QPID 2>/dev/null
    echo "--- firmware output (semihosting, first lines) ---"; head -4 qemu_gdb.log ;;
objdump)    # IRQ 엔트리(0x280) 한 칸 전체
    build solution || exit 2
    V=0x$(nm solution.elf | awk '$3=="vectors_el1"{print $1}')
    objdump -d --start-address=$((V + 0x280)) --stop-address=$((V + 0x300)) solution.elf | tail -n +7
    nm -n solution.elf | grep -E ' (vectors_el1|vectors_el1_end|ret_from_exc|exc_c|irq_c|hold_win_start|hold_win_end)$' ;;
bug_nolr)   # 7. 실수 1: SAVE_ALL/RESTORE_ALL 에서 x30 을 빼먹음 (진짜로)
    build solution -DBUG_NO_LR || exit 2
    run solution.elf ;;
bug_noelr)  # 7. 실수 2: ELR/SPSR 를 프레임에서 복원하지 않음 → 중첩 전까지는 멀쩡
    build solution -DBUG_NO_ELR || exit 2
    run solution.elf ;;
bug_align)  # 7. 실수 4: 프레임 280 B (16 의 배수 아님) → 실리콘이면 SP 정렬 폴트, QEMU 는 검사 안 함
    build solution -DBUG_FRAME_ALIGN || exit 2
    run solution.elf ;;
*) echo "usage: $0 [starter|solution|practice|gdb|objdump|bug_nolr|bug_noelr|bug_align]"; exit 2 ;;
esac
