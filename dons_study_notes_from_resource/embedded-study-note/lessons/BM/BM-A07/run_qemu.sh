#!/bin/bash
# ENV: qemu
# BM-A07 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/BM-A07_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-A07/run_qemu.sh [solution|starter|practice|su|paintbug]'
# 빌드 2 단계: ① frames.c 를 -fstack-usage 로 컴파일 → frames.su → su_gen.h (#define SU_<func> <bytes>)
#              ② starter/solution.c (su_gen.h include) + frames.o 링크.  frames.c 는 su_gen.h 를 안 보므로 ① 의 숫자 = 링크된 코드의 숫자.
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-A07_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -fstack-usage"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

frames() {  # frames.o + frames.su + su_gen.h
    arm-none-eabi-gcc $CFLAGS -fstack-protector-explicit -c frames.c -o frames.o || return 1
    awk -F'\t' '{ n = $1; sub(/.*:/, "", n); printf "#define SU_%s %s\n", n, $2 }' frames.su > su_gen.h
}
build() {   # build <src.c> <out.elf> [extra flags...]
    local src=$1 out=$2; shift 2
    frames || return 1
    arm-none-eabi-gcc $CFLAGS "$@" -I. -c "$src" -o "${out%.elf}.o" || return 1
    arm-none-eabi-gcc $CFLAGS -T a07.ld -Wl,-Map="${out%.elf}.map" "${out%.elf}.o" frames.o -o "$out" -lgcc
}
run() {     # run <elf>
    timeout 20 $QEMU -kernel "$1" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
solution|starter)
    build "$1.c" "$1.elf" || exit 1
    arm-none-eabi-size "$1.elf"
    run "$1.elf" ;;
su)         # 정적 스택 정보: .su 두 개 + 호출 간선(objdump 의 bl/blx/b.w) + 배치 심볼
    build solution.c solution.elf || exit 1
    echo "--- frames.su ---";   cat frames.su
    echo "--- solution.su ---"; cat solution.su
    echo "--- su_gen.h ---";    cat su_gen.h
    echo "--- call edges (caller callee) from objdump -d solution.elf ---"
    arm-none-eabi-objdump -d solution.elf | awk '
        /^[0-9a-f]+ <[^>]+>:$/ { cur = $2; gsub(/[<>:]/, "", cur); next }
        /\t(bl|b\.w)\t[0-9a-f]+ <[^+>]+>/ { t = $0; sub(/.*</, "", t); sub(/>.*/, "", t)
            if ($0 ~ /\tbl\t/ || t != cur) print cur, t; next }      # bl 자기 자신 = 재귀, b.w 자기 자신 = 루프
        /\tblx\tr[0-9]+/ { print cur, "*INDIRECT*" }' | sort -u
    echo "--- symbols ---"
    arm-none-eabi-nm -n solution.elf | grep -E ' (_sneighbor|_sguard|_sstack|_estack|g_neighbor|__stack_chk_guard)$'
    echo "--- objdump: ssp_copy prologue/epilogue (canary slot) ---"
    arm-none-eabi-objdump -d solution.elf | sed -n '/<ssp_copy>:/,/^$/p' | head -16 ;;
paintbug)   # 결함 주입: main 안에서 스택 "전체" 를 칠한다 → 자기 복귀 주소를 0xDEADBEEF 로 덮는다
    build solution.c paintbug.elf -DPAINT_BUG || exit 1
    run paintbug.elf ;;
practice)   # practice.c 는 호스트 C — VM 의 gcc 로도 빌드·실행 (Mac 에선 cc 로)
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    rc=$?; echo "practice exit=$rc"; exit $rc ;;
*) echo "usage: $0 [solution|starter|practice|su|paintbug]"; exit 2 ;;
esac
