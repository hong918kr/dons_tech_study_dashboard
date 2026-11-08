#!/bin/bash
# CPP-A04 asm_targets.sh — std::atomic 을 M0+ / M3 / M4 / arm64(호스트) 로 컴파일해 비교한다 (맥에서 실행)
#   bash asm_targets.sh asm    demo_targets.cpp 의 -S 출력 (지시어 제거)
#   bash asm_targets.sh probe  is_always_lock_free / ATOMIC_*_LOCK_FREE / -dM -E 매크로
#   bash asm_targets.sh policy demo_policy_targets.cpp 를 타깃별로 (+ -DSHOW_ERROR)
#
# 맥의 Apple clang 은 thumb 타깃 코드를 만들 수 있지만 <atomic> 같은 C++ 헤더가 없다 (bare-metal 용 libc++ 없음).
# 그래서 arm-none-eabi 의 libstdc++/newlib 헤더(Lima VM elinux 의 /usr/lib/arm-none-eabi/include)를 한 번 복사해 쓴다.
# Arm GNU Toolchain(맥용 arm-none-eabi-gcc)을 설치했다면 ARM_INC 를 그 include 경로로 바꾸면 된다.
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
F="-std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti"
cxx() {   # cxx <m0|m3|m4|host> args...
    local k=$1; shift
    case $k in
    m0) T="--target=thumbv6m-none-eabi -mcpu=cortex-m0plus";  M=thumb/v6-m/nofp ;;
    m3) T="--target=thumbv7m-none-eabi -mcpu=cortex-m3";      M=thumb/v7-m/nofp ;;
    m4) T="--target=thumbv7em-none-eabihf -mcpu=cortex-m4";   M=thumb/v7e-m+fp/hard ;;
    host) c++ $F "$@"; return ;;
    esac
    clang++ $T $F -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
        -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/$M" -isystem "$ARM_INC" "$@"
}
strip_dir() { grep -v -E '^\s*(\.[a-z_]+|@ --|; --|@ %bb|;  *%|Lloh|$)|^\s*\.(LCPI|Lfunc_end)' || true; }
case "${1:-asm}" in
asm)
    for k in m0 m3 m4 host; do echo "=================== $k"; cxx $k demo_targets.cpp -S -o - | strip_dir; done ;;
probe)
    for k in m0 m3 m4 host; do
        echo "== $k"
        cxx $k demo_probe.cpp -fsyntax-only 2>&1 | grep -o 'Show<[^>]*>' | head -1 || true
        cxx $k demo_probe.cpp -dM -E | grep -E '__(GCC|CLANG)_ATOMIC_(INT|LLONG)_LOCK_FREE' | sort
    done ;;
policy)
    for k in host m4 m3 m0; do
        echo "== $k"; cxx $k demo_policy_targets.cpp -fsyntax-only 2>&1 | grep -E 'error|static assertion|required from|requested here' | head -4 || true
    done
    echo "== m3 -DSHOW_ERROR"; cxx m3 demo_policy_targets.cpp -DSHOW_ERROR -fsyntax-only 2>&1 | grep -E 'error|requested here' | head -4 || true
    echo "== m3 -S (on_rx, read_bytes)"; cxx m3 demo_policy_targets.cpp -S -o - | strip_dir ;;
esac
