#!/bin/bash
# CPP-M01 asm_target.sh — demo_target.cpp / demo_instances.cpp 를 Cortex-M4 로 컴파일해서 본다 (맥에서 실행)
#   bash asm_target.sh asm [opt]      Reg vs C 스타일 어셈블리 (opt 기본 -O2)
#   bash asm_target.sh size [opt]     demo_target.o size + nm
#   bash asm_target.sh instances      UART 3개: 템플릿 3벌 vs 런타임 베이스 1벌 size
# 맥의 clang 은 Thumb 코드를 내지만 C++ 헤더가 없으므로 arm-none-eabi 헤더를 쓴다 (CPP-A04 asm_targets.sh 와 같은 방식).
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
: "${OUT:=/tmp/cpp_m01_out}"; mkdir -p "$OUT"
cd "$(dirname "$0")"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
cxx() {
    clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -ffreestanding \
        -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" \
        -isystem "$ARM_INC" "$@"
}
case "${1:-asm}" in
asm)
    O="${2:--O2}"
    cxx "$O" -c demo_target.cpp -o "$OUT/t.o"
    xcrun llvm-objdump -d --no-show-raw-insn "$OUT/t.o" ;;
size)
    O="${2:--O2}"
    cxx "$O" -c demo_target.cpp -o "$OUT/t.o"
    xcrun llvm-size "$OUT/t.o"; xcrun llvm-nm -C --size-sort "$OUT/t.o" ;;
instances)
    cxx -O2 -c demo_instances.cpp -o "$OUT/run.o"
    cxx -O2 -DUSE_TEMPLATE -c demo_instances.cpp -o "$OUT/tpl.o"
    xcrun llvm-size "$OUT/run.o" "$OUT/tpl.o"; xcrun llvm-nm -C --size-sort "$OUT/tpl.o" ;;
esac
