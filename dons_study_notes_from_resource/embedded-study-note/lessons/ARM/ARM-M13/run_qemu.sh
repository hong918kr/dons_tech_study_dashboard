#!/bin/bash
# ENV: qemu
# ARM-M13 — Lima VM `elinux` (aarch64 Ubuntu) 안에서 실행: 네이티브 gcc 로 베어메탈 AArch64 ELF 를 만들고
# qemu-system-aarch64 -M virt -cpu cortex-a53 에서 돌린다. Mac 홈은 VM 에서 읽기 전용 → /tmp/ARM-M13_work 로 복사.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-M13/run_qemu.sh [starter|solution|practice|el2|gdb|objdump|bug_noadvance|bug_svcadvance|bug_simd|bug_norw]'
# 소스 규칙: <v>.c + <v>.S + shared_*.S + shared_*.c  (+ arm_m13.ld)
# starter/solution/practice 는 두 번 부팅한다: (1) -M virt → EL1 시작, (2) -M virt,virtualization=on → EL2 시작.
# 반환값 = 처음으로 0 이 아니었던 펌웨어 종료 코드 (전부 0 이면 0).
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-M13_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CC=gcc            # VM 네이티브 aarch64 gcc 15. clang 21 + ld.lld 도 된다: -no-pie 빼고 -fuse-ld=lld (md 3.)
CFLAGS="-std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -nostartfiles -static -fno-pie -no-pie
        -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-asynchronous-unwind-tables
        -fno-tree-loop-distribute-patterns -mbranch-protection=none -Wl,--build-id=none -Wl,--no-warn-rwx-segments"
QEMU="qemu-system-aarch64 -cpu cortex-a53 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <variant> [extra flags...] → <variant>.elf
    local v=$1; shift
    $CC $CFLAGS "$@" -T arm_m13.ld "$v.c" "$v.S" shared_*.S shared_*.c -o "$v.elf" -lgcc
}
run() {     # run <machine> <elf>: 펌웨어가 SYS_EXIT 로 QEMU 를 끝낸다. 혹시 몰라 timeout 20 s.
    echo "=== qemu-system-aarch64 -M $1 -cpu cortex-a53 -kernel $2 ==="
    timeout 20 $QEMU -M "$1" -kernel "$2" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}
runs() {    # runs <elf> <machine>... : 여러 부팅을 차례로, 첫 실패 코드를 돌려준다
    local elf=$1; shift; local first=0 rc
    for m in "$@"; do run "$m" "$elf"; rc=$?; [ $first -eq 0 ] && first=$rc; done
    return $first
}

case "${1:-solution}" in
starter|solution)
    build "$1" || exit 2
    runs "$1.elf" virt virt,virtualization=on ;;
practice)
    build practice || exit 2
    runs practice.elf virt virt,virtualization=on virt,secure=on virt,secure=on,virtualization=on ;;
el2)        # EL2 부팅만 (Part C)
    build solution || exit 2
    run virt,virtualization=on solution.elf ;;
bug_noadvance)   # 7. 실수 1: BRK 뒤 ELR 을 안 옮김 → 같은 brk 로 영원히 → 폭주 감시가 exit 3
    build solution -DBUG_NO_ADVANCE || exit 2
    run virt solution.elf ;;
bug_svcadvance)  # 7. 실수 2: SVC 에도 ELR += 4 → svc 다음 명령이 건너뛰어진다
    build solution -DBUG_SVC_ADVANCE || exit 2
    run virt solution.elf ;;
bug_simd)   # 7. 실수 3: -mgeneral-regs-only 없이 빌드 → 컴파일러가 NEON 을 쓴다 → CPACR_EL1.FPEN = 0 이라 트랩
    $CC ${CFLAGS/-mgeneral-regs-only/} -T arm_m13.ld solution.c solution.S shared_*.S shared_*.c -o simd.elf -lgcc || exit 2
    echo "SIMD/FP instructions in image: $(objdump -d simd.elf | grep -cE '\b(q[0-9]+|v[0-9]+\.[0-9]*[bhsd])\b')"
    rm -f int.log
    timeout 5 $QEMU -M virt -d int -D int.log -kernel simd.elf 2>&1 | grep -v '^PASS' | cat
    echo "qemu exit=${PIPESTATUS[0]} (124 = timeout: 끝나지 않았다)"
    echo "--- qemu -d int (semihosting 제외, 처음 12 줄) ---"
    grep -v -E 'Semihosting|from EL1 to EL0|semihosting call' int.log | head -12 ;;
bug_norw)   # 7. 실수 4: HCR_EL2.RW 를 안 켜고 EL2 → EL1 ERET
    build solution -DBUG_NO_RW || exit 2
    rm -f int.log
    timeout 5 $QEMU -M virt,virtualization=on -d int -D int.log -kernel solution.elf 2>&1 | cat
    echo "qemu exit=${PIPESTATUS[0]} (124 = timeout: 끝나지 않았다)"
    echo "--- qemu -d int (처음 14 줄) ---"
    head -14 int.log ;;
objdump)    # 벡터 테이블 앞부분과 el2_to_el1 / run_el0 의 실제 인코딩
    build solution || exit 2
    objdump -d solution.elf | sed -n '/<vectors_el1>:/,/^$/p' | head -12
    objdump -d solution.elf | sed -n '/<el2_to_el1>:/,/^$/p'
    objdump -h solution.elf | grep -E 'text|data|bss'
    nm -n solution.elf | grep -E ' (_start|vectors_el1|exc_common|__stack_top|__bss_start)$' ;;
gdb)        # -s -S 로 멈춘 QEMU 에 gdb-multiarch 를 붙여 시스템 레지스터를 본다
    build solution || exit 2
    $QEMU -M virt -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_sysregs.gdb solution.elf
    wait $QPID; echo "qemu exit=$?"
    echo "--- firmware output (semihosting, last lines) ---"; tail -3 qemu_gdb.log ;;
*) echo "usage: $0 [starter|solution|practice|el2|gdb|objdump|bug_noadvance|bug_svcadvance|bug_simd|bug_norw]"; exit 2 ;;
esac
