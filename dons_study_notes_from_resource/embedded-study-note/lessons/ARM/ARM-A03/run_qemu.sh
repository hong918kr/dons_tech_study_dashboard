#!/bin/bash
# ENV: qemu
# ARM-A03 — Lima VM `elinux` (aarch64 Ubuntu) 안에서 실행: 네이티브 gcc 로 베어메탈 AArch64 ELF 를 만들고
# qemu-system-aarch64 -M virt -cpu cortex-a53 (EL1 부팅) 에서 돌린다. Mac 홈은 VM 에서 읽기 전용 → /tmp/ARM-A03_work 로 복사.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A03/run_qemu.sh [starter|solution|practice|gdb|objdump|bug_noaf|bug_t0sz|bug_mair|bug_noisb|bug_notlbi]'
# 소스 규칙: <v>.c + <v>.S + shared_*.S + shared_*.c (+ arm_a03.ld). PL011 출력은 uart_<v>.txt 로 받아 뒤에 보여 준다.
# 반환값 = 펌웨어 종료 코드 (starter 1, solution/practice 0). bug_* 데모는 관찰용 (항상 0 이 아니어도 된다).
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-A03_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CC=gcc            # VM 네이티브 aarch64 gcc 15
CFLAGS="-std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -nostartfiles -static -fno-pie -no-pie
        -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-asynchronous-unwind-tables
        -fno-tree-loop-distribute-patterns -mbranch-protection=none -Wl,--build-id=none -Wl,--no-warn-rwx-segments"
QEMU="qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -monitor none -semihosting-config enable=on,target=native"

build() {   # build <variant> [extra flags...] → <variant>.elf
    local v=$1; shift
    $CC $CFLAGS "$@" -T arm_a03.ld "$v.c" "$v.S" shared_*.S shared_*.c -o "$v.elf" -lgcc
}
run() {     # run <elf> <tag>: 펌웨어가 SYS_EXIT 로 QEMU 를 끝낸다. 혹시 몰라 timeout 20 s.
    echo "=== qemu-system-aarch64 -M virt -cpu cortex-a53 -kernel $1 ==="
    rm -f "uart_$2.txt"
    timeout 20 $QEMU -serial "file:uart_$2.txt" -kernel "$1" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "--- PL011 (uart_$2.txt) ---"; cat "uart_$2.txt" 2>/dev/null
    echo "qemu exit=$rc"
    return $rc
}
hang() {    # hang <elf> <tag>: MMU 를 켜자마자 멈추는 버그 데모 — 5 s 뒤 강제 종료, -d int 로그 앞부분
    rm -f int.log
    timeout 5 $QEMU -serial none -d int -D int.log -kernel "$1" 2>&1 | cat
    echo "qemu exit=${PIPESTATUS[0]} (124 = timeout: 끝나지 않았다)"
    echo "--- qemu -d int (세미호스팅 줄 제외, 처음 8 줄) ---"
    grep -v -iE 'semihosting' int.log | grep -E 'Taking exception|ESR|FAR|ELR|PC' | head -8
    echo "--- 같은 예외가 몇 번? ---"; grep -c 'Taking exception' int.log
}

case "${1:-solution}" in
starter|solution|practice)
    build "$1" || exit 2
    run "$1.elf" "$1" ;;
bug_noaf)    # 7. 실수 2: 모든 블록에서 AF 를 빠뜨림 → MMU 를 켠 직후 명령 가져오기부터 access flag fault, 벡터도 같은 폴트
    build solution -DBUG_NO_AF || exit 2
    hang solution.elf noaf ;;
bug_t0sz)    # 7. 실수 4: 테이블은 L1 부터인데 T0SZ = 16 (시작 레벨 0)
    build solution -DBUG_T0SZ16 || exit 2
    hang solution.elf t0sz ;;
bug_mair)    # 7. 실수 3: UART 를 MAIR 인덱스 0 (Normal WB) 으로 매핑 — QEMU 에서 무엇이 달라지나
    build solution -DBUG_MAIR_UART || exit 2
    run solution.elf mair ;;
bug_noisb)   # 7. 실수 5: mmu_enable 의 ISB 3 개를 뺌 — QEMU 에서는 관찰되지 않는다
    build solution -DBUG_NO_ISB || exit 2
    run solution.elf noisb ;;
bug_notlbi)  # 7. 실수 1: break-before-make 에서 TLBI 를 뺌
    build solution -DBUG_NO_TLBI || exit 2
    run solution.elf notlbi ;;
objdump)     # mmu_enable / at_s1e1r 의 실제 인코딩, 테이블 위치
    build solution || exit 2
    objdump -d solution.elf | awk '/<mmu_enable>:/{f=1} /<at_s1e1w>:/{f=0} f'
    nm -n solution.elf | grep -E ' (_start|vectors_el1|l1|l2_low|l2_ram|__stack_top|__image_end)$' ;;
gdb)         # -s -S 로 멈춘 QEMU 에 gdb-multiarch 를 붙여 MMU 레지스터와 VA/PA 를 본다
    build solution || exit 2
    $QEMU -serial none -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_mmu.gdb solution.elf
    wait $QPID; echo "qemu exit=$?"
    echo "--- firmware output (semihosting, last lines) ---"; tail -3 qemu_gdb.log ;;
*) echo "usage: $0 [starter|solution|practice|gdb|objdump|bug_noaf|bug_t0sz|bug_mair|bug_noisb|bug_notlbi]"; exit 2 ;;
esac
