#!/bin/bash
# CPP-A02 asm_targets.sh — ISR 트램펄린을 Cortex-M4 로 컴파일해서 본다 (맥에서 실행, vm 모드는 Lima VM 'elinux')
#   bash asm_targets.sh asm       demo_targets.cpp → thumbv7em asm (지시어 제거): irq_trampoline vs Trampoline::isr
#   bash asm_targets.sh sections  .o 의 섹션 헤더와 관심 심볼 (xcrun llvm-objdump -h -t)
#   bash asm_targets.sh badsp     초기 SP 를 reinterpret_cast 로 넣은 벡터 테이블: 일반 / constinit
#   bash asm_targets.sh linkage   demo_linkage.cpp: 기본 빌드 + 각 SHOW_ERROR 의 실제 에러
#   bash asm_targets.sh vm        VM 에서 arm-none-eabi-g++ 링크 → 벡터 테이블 바이트, weak 덮어쓰기
#
# 맥 Apple clang 에는 bare-metal 용 C++ 헤더가 없어서 arm-none-eabi 의 libstdc++/newlib 헤더를 한 번 복사해 쓴다
# (CPP-A04 asm_targets.sh 와 같은 방식). 다른 경로에 있으면 ARM_INC=... 로 지정.
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/cppa02.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
fetch_inc() {
    if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
        echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
        mkdir -p "$ARM_INC"
        limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
    fi
}
F="-std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti"
m4() {
    clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 $F -ffreestanding -nostdinc++ \
        -isystem "$ARM_INC/c++/14.2.1" -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" \
        -isystem "$ARM_INC" "$@"
}
strip_dir() { grep -v -E '^\s*(\.[a-z_]+|@ --|@ %bb|$)|^\s*\.(LCPI|Lfunc_end)' || true; }
case "${1:-asm}" in
asm)
    fetch_inc
    m4 demo_targets.cpp -S -o - | strip_dir | awk '
        /^[A-Za-z_][^ \t]*:/ { sub(/[ \t]+@.*/, ""); show = ($0 ~ /irq_trampoline|TrampolineILj37|member_thunk|attach_uart5/) }
        show' ;;
sections)
    fetch_inc
    m4 demo_targets.cpp -c -o "$OUT/t.o"
    xcrun llvm-objdump -h "$OUT/t.o" | grep -E 'Idx|isr_vector|\.text |\.data|\.bss|\.rodata|init_array' || true
    echo "--- symbols"
    xcrun llvm-objdump -t "$OUT/t.o" | grep -E 'g_vectors|g_slots|g_ram_vectors|g_uart1|Handler|irq_trampoline|g_pmf' \
        | grep -v -E ' \$t|l    d ' | sort -k5
    echo "--- .isr_vector 재배치 (앞 16개 = 시스템 예외 슬롯)"
    xcrun llvm-objdump -r "$OUT/t.o" | sed -n '/isr_vector\]/,/^$/p' | sed -n '3,17p'
    echo "--- 멤버 함수 포인터 g_pmf_nv(.data+0x30) / g_pmf_v(.data+0x38)"
    xcrun llvm-objdump -s -j .data "$OUT/t.o" | grep -E '^ 00(2|3)0'
    xcrun llvm-objdump -r "$OUT/t.o" | sed -n '/\.data\]/,/^$/p' | grep -E 'Dev' || true ;;
badsp)
    fetch_inc
    echo "== -DBAD_SP_CAST (reinterpret_cast, constinit 없음)"
    m4 demo_targets.cpp -DBAD_SP_CAST -c -o "$OUT/b.o"
    xcrun llvm-objdump -h "$OUT/b.o" | grep -E 'isr_vector|init_array' || true
    xcrun llvm-objdump -r "$OUT/b.o" | sed -n '/isr_vector\]/,/^$/p' | sed -n '3,4p'
    echo "== -DBAD_SP_CAST -DWITH_CONSTINIT"
    m4 demo_targets.cpp -DBAD_SP_CAST -DWITH_CONSTINIT -fsyntax-only 2>&1 | grep -E 'error|note' | head -4 || true
    echo "== -DBAD_SP_CAST -DDYN_ENTRY (엔트리 하나가 함수 호출 결과)"
    m4 demo_targets.cpp -DBAD_SP_CAST -DDYN_ENTRY -c -o "$OUT/d.o"
    xcrun llvm-objdump -h "$OUT/d.o" | grep -E 'isr_vector|init_array' || true
    xcrun llvm-objdump -s -j .isr_vector "$OUT/d.o" | sed -n '4p'
    echo "--- .rel.isr_vector:"; xcrun llvm-objdump -r "$OUT/d.o" | sed -n '/isr_vector\]/,/^$/p' | sed -n '3,4p'
    echo "--- _GLOBAL__sub_I 가 부르는/쓰는 것:"
    xcrun llvm-objdump -r "$OUT/d.o" | sed -n '/\[.text\]/,/^$/p' | grep -E 'g_vectors|memclr|_estack|Reset_Handler|pick_hardfault' ;;
linkage)
    echo "== 기본 빌드 (-Wall -Wextra -pedantic -Werror)"
    c++ $F -pedantic -Werror demo_linkage.cpp -o "$OUT/dl" && "$OUT/dl"
    for e in SHOW_ERROR_TEMPLATE SHOW_ERROR_OVERLOAD SHOW_ERROR_LOCAL SHOW_ERROR_TEMP SHOW_ERROR_RANGE SHOW_ERROR_ALIAS; do
        echo "== -D$e"
        c++ $F -D$e -fsyntax-only demo_linkage.cpp 2>&1 | grep -E 'error:' | head -3 || true
    done
    echo "== -DSHOW_ERROR_ALIAS on thumbv7em (ELF 는 alias 지원)"
    fetch_inc
    m4 -DSHOW_ERROR_ALIAS -c -o "$OUT/al.o" demo_linkage.cpp 2>&1 | grep -E 'error:|note:' | head -3 || true ;;
vm)
    limactl shell elinux -- bash -lc "rm -rf /tmp/CPP-A02 && mkdir -p /tmp/CPP-A02"
    tar -cf - demo_targets.cpp demo_strong_uart4.cpp vm_link.ld | limactl shell elinux -- bash -lc 'tar -xf - -C /tmp/CPP-A02'
    limactl shell elinux -- bash -lc 'cd /tmp/CPP-A02 && G="arm-none-eabi-g++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -ffunction-sections -fdata-sections -nostartfiles -nostdlib -Wl,--gc-sections -T vm_link.ld"
        $G demo_targets.cpp -o weak.elf && $G demo_targets.cpp demo_strong_uart4.cpp -o strong.elf
        $G -DFORGET_EXTERN_C demo_targets.cpp demo_strong_uart4.cpp -o forgot.elf
        echo "=== forgot: demo_strong_uart4.o 가 실제로 정의한 심볼"
        arm-none-eabi-g++ -std=c++20 -O2 -mcpu=cortex-m4 -mthumb -DFORGET_EXTERN_C -c demo_strong_uart4.cpp -o forgot4.o
        arm-none-eabi-nm forgot4.o | grep -i uart4
        for e in weak strong forgot; do
            echo "=== $e.elf"
            arm-none-eabi-size $e.elf
            arm-none-eabi-nm -n $e.elf | grep -E "Default_Handler|UART4_IRQHandler|g_uart4_hits|HardFault_Handler|TrampolineILj37|_estack|irq_trampoline| g_vectors"
            echo "--- .isr_vector 행 0x00=[0..3] 0xd0=[52..55] 0xe0=[56..59] 0x110=[68..71] (리틀엔디언 워드, 홀수 = Thumb 비트)"
            arm-none-eabi-objdump -s -j .isr_vector $e.elf | sed -n "5p;18,19p;22p"
        done
        echo "=== g++ 로 본 Trampoline<37>::isr 와 irq_trampoline"
        arm-none-eabi-objdump -d --no-show-raw-insn -C weak.elf | awk "/<Trampoline<37u|<irq_trampoline>:/{s=1} /^$/{s=0} s"' ;;
esac
