#!/bin/bash
# CPP-M03 asm_compare.sh — demo_asm.cpp 를 arm64(호스트)와 thumbv7em(Cortex-M4)로 컴파일해 함수별 명령을 보여준다.
#   bash asm_compare.sh arm64 | m4 | size | gcc      (인자 없으면 모두)
# 맥 clang 에는 bare-metal C++ 헤더가 없어서 arm-none-eabi 헤더를 쓴다 (CPP-A04/asm_targets.sh 참고).
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
: "${OUT:=$(mktemp -d)}"
cd "$(dirname "$0")"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
F="-std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti"
M4="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -ffreestanding -nostdinc++ -isystem $ARM_INC/c++/14.2.1 -isystem $ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard -isystem $ARM_INC"
clean() { grep -v -E '^\s*(\.[a-zA-Z_]|@ --|; --|@ %bb|; %bb|;  *%|Lloh|Lfunc|LBB|$)|^\s*\.' | c++filt; }
case "${1:-all}" in
arm64) c++ $F -S demo_asm.cpp -o - | clean ;;
m4)    clang++ $M4 $F -S demo_asm.cpp -o - | clean ;;
size)
    c++ $F -c demo_asm.cpp -o "$OUT/h.o"
    clang++ $M4 $F -c demo_asm.cpp -o "$OUT/m4.o"
    xcrun llvm-nm --print-size --size-sort --radix=d -C "$OUT/m4.o" | grep -E ' (raw|flags)' ;;
gcc)   # 실제 arm-none-eabi-g++ 14.2.1 (Lima VM elinux). 맥 홈은 읽기 전용이라 VM /tmp 로 복사해서 컴파일
    limactl shell elinux -- bash -c "mkdir -p /tmp/CPP-M03 && cp $PWD/demo_asm.cpp $PWD/flags_final.hpp /tmp/CPP-M03/ && cd /tmp/CPP-M03 && arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Wall -Wextra -S -o - demo_asm.cpp" | grep -v -E '^\s*(\.|@ )' | c++filt ;;
all)  for k in arm64 m4 size gcc; do echo "=================== $k"; bash "$0" "$k"; done ;;
esac
