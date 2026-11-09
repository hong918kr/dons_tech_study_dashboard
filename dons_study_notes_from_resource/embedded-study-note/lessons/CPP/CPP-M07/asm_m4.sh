#!/bin/bash
# CPP-M07 asm_m4.sh — demo_codegen.cpp 를 Cortex-M4 로 컴파일해 어셈블리를 본다 (맥에서 실행).
# 맥에 bare-metal C++ 헤더가 없어 arm-none-eabi 헤더를 빌려 쓴다 (CPP-A04 asm_targets.sh 와 같은 방식).
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti \
    -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
    -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" -isystem "$ARM_INC" \
    -S demo_codegen.cpp -o - | grep -v -E '^\s*(\.|@ --|@ %bb|$)|^\.L|^\s*@'
