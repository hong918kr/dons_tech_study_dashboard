#!/bin/bash
# CPP-M08 asm_targets.sh — Cortex-M4 어셈블리 확인 (맥에서 실행)
#   bash asm_targets.sh nullcheck   demo_nullcheck.cpp : noexcept 유무별 new-식 코드
#   bash asm_targets.sh dse         demo_lifetime_dse.cpp : operator new 의 memset, 소멸자 포이즌
#   bash asm_targets.sh pool        solution.cpp 의 Message::operator new
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
T="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti"
strip() { grep -v -E '^\s*(\.[a-z]|@)|^$|^\.Lfunc_end'; }
case "$1" in
nullcheck)
    for ne in "" -DUSE_NOEXCEPT; do echo "=== clang thumbv7em $ne"; clang++ $T $ne -S demo_nullcheck.cpp -o - | strip; done ;;
dse)
    clang++ $T -S demo_lifetime_dse.cpp -o - | strip ;;
pool)
    if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
        echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
        mkdir -p "$ARM_INC"
        limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
    fi
    clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti \
        -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
        -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" -isystem "$ARM_INC" -S solution.cpp -o - \
        | awk '/^_ZN7MessagenwEj:/,/^\.Lfunc_end/' | strip ;;
*) echo "usage: $0 nullcheck|dse|pool"; exit 2 ;;
esac
