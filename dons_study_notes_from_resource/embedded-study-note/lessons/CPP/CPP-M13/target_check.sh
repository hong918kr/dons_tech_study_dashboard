#!/bin/bash
# CPP-M13 target_check.sh — 표가 어디에 놓였는지 Cortex-M4 ELF 로 확인한다 (맥에서 실행)
#   bash target_check.sh obj      demo_sections.cpp VARIANT 1~6 을 clang(thumbv7em) .o 로: 섹션/심볼
#   bash target_check.sh rodata   VARIANT 1 의 .rodata 바이트 (llvm-objdump -s)
#   bash target_check.sh o0       -O0 에서도 VARIANT 1 은 .rodata 인지
#   bash target_check.sh fold     demo_fold_probe.cpp: 동적 초기화를 최적화기가 지우나 (clang/gcc, O0/O2)
#   bash target_check.sh local    함수 안 constexpr vs static constexpr: clang / gcc 어셈블리
#   bash target_check.sh elf      Lima VM 'elinux' 의 arm-none-eabi-g++ 로 실제 링크 → size / 섹션 / soft-float 심볼
#   bash target_check.sh errors   demo_errors.cpp -DSHOW_ERROR (clang, gcc)
#   bash target_check.sh odr      헤더 constexpr vs inline constexpr: 두 TU 링크 후 표 개수 (Lima VM)
#   bash target_check.sh big      demo_big_table.cpp: 표 크기별 상수 평가 한도 / 컴파일 시간 (이 맥)
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
HERE=$PWD
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

m4_flags="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -ffreestanding -nostdinc++ \
 -isystem $ARM_INC/c++/14.2.1 -isystem $ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard -isystem $ARM_INC"

need_arm_inc() {
    if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
        echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
        mkdir -p "$ARM_INC"
        limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
    fi
}
sections() { xcrun llvm-objdump -h "$1" | grep -E ' \.(text|rodata|data|bss|init_array) ' || true; }
strip_dir() { grep -vE '^\s*\.|^\s*@|^$' || true; }

case "${1:-obj}" in
obj)
    need_arm_inc
    for v in 1 2 3 4 5 6; do
        clang++ $m4_flags -O2 -DVARIANT=$v -c demo_sections.cpp -o "$T/v$v.o"
        echo "=== VARIANT=$v (clang -O2)"; sections "$T/v$v.o"
        xcrun llvm-nm -S "$T/v$v.o" | grep -E 'crc_table|sin_table|GLOBAL__sub' || true
        echo "soft-float 호출: $(xcrun llvm-nm "$T/v$v.o" | grep -cE 'U __aeabi_d' || true)개"
    done ;;
rodata)
    need_arm_inc
    clang++ $m4_flags -O2 -DVARIANT=1 -c demo_sections.cpp -o "$T/v1.o"
    xcrun llvm-objdump -s -j .rodata "$T/v1.o" | head -8
    echo ...
    xcrun llvm-objdump -s -j .rodata "$T/v1.o" | tail -4 ;;
o0)
    need_arm_inc
    for v in 1 3 4; do
        clang++ $m4_flags -O0 -DVARIANT=$v -c demo_sections.cpp -o "$T/v$v.o"
        echo "=== VARIANT=$v (clang -O0)"; sections "$T/v$v.o"
    done ;;
fold)
    for c in 1 2; do for o in O0 O2; do
        echo "--- clang CASE=$c -$o"
        clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -$o -fno-exceptions -fno-rtti -DCASE=$c -c demo_fold_probe.cpp -o "$T/f.o"
        xcrun llvm-objdump -h "$T/f.o" | grep -E ' \.(rodata|bss|init_array) ' || true
    done; done
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-M13; mkdir -p /tmp/CPP-M13 && cp '$HERE'/demo_*.cpp /tmp/CPP-M13/ && cd /tmp/CPP-M13 &&
    for c in 1 2; do for o in O0 O2; do
        echo \"--- gcc CASE=\$c -\$o\"
        arm-none-eabi-g++ -std=c++20 -\$o -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -DCASE=\$c -c demo_fold_probe.cpp -o f.o
        arm-none-eabi-objdump -h f.o | grep -E ' \.(rodata|bss|init_array) ' | awk '{print \$2,\$3}'
    done; done" ;;
local)
    echo "#### clang -O2"
    clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -S demo_local_constexpr.cpp -o - | strip_dir
    limactl shell elinux -- bash -c "mkdir -p /tmp/CPP-M13 && cp '$HERE'/demo_*.cpp /tmp/CPP-M13/ && cd /tmp/CPP-M13 &&
    echo '#### arm-none-eabi-g++ -O2' &&
    arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -S demo_local_constexpr.cpp -o - | grep -vE '^\s*\.|^\s*@|^$'" ;;
elf)
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-M13; mkdir -p /tmp/CPP-M13 && cp '$HERE'/demo_*.cpp /tmp/CPP-M13/ && cd /tmp/CPP-M13 &&
    arm-none-eabi-g++ --version | head -1 &&
    F='-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs'
    for v in 1 2 3 4 5 6; do
        arm-none-eabi-g++ \$F -DVARIANT=\$v demo_sections.cpp -o v\$v.elf 2>&1 | grep -vE 'not implemented|will always fail|in function' || true
        echo \"=== VARIANT=\$v\"; arm-none-eabi-size v\$v.elf
        arm-none-eabi-objdump -h v\$v.elf | grep -E ' \.(rodata|data|bss|init_array) ' | awk '{print \"  \"\$2,\$3}'
        echo \"  soft-float 심볼: \$(arm-none-eabi-nm v\$v.elf | grep -cE ' T __(aeabi_d[a-z0-9]+|adddf3|muldf3|divdf3|subdf3|fixdfsi)\$' || true)개\"
    done
    arm-none-eabi-objdump -s -j .rodata v1.elf | head -6" ;;
errors)
    c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR demo_errors.cpp 2>&1 | grep -E 'error:|note: (value|reinterpret|non-constexpr|constexpr evaluation)' || true
    echo "#### gcc"
    limactl shell elinux -- bash -c "mkdir -p /tmp/CPP-M13 && cp '$HERE'/demo_*.cpp /tmp/CPP-M13/ && cd /tmp/CPP-M13 &&
    arm-none-eabi-g++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR demo_errors.cpp 2>&1 | grep -E 'error:' || true" ;;
odr)
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-M13; mkdir -p /tmp/CPP-M13 && cp '$HERE'/demo_odr* /tmp/CPP-M13/ && cd /tmp/CPP-M13 &&
    F='-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs'
    for m in '' -DUSE_INLINE; do
        echo \"=== 플래그: [\$m]\"
        arm-none-eabi-g++ \$F \$m demo_odr_a.cpp demo_odr_b.cpp -o o.elf 2>&1 | grep -vE 'not implemented|will always fail|in function' || true
        arm-none-eabi-size o.elf | tail -1
        arm-none-eabi-nm -C --size-sort o.elf | grep -E ' table\$'
    done" ;;
big)
    for sz in 1024 16384 32768; do
        echo "--- N=$sz, 기본 한도: 'step limit' 줄 수 = $(c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -DSZ=$sz -c demo_big_table.cpp -o "$T/b.o" 2>&1 | grep -c 'step limit' || true)"
    done
    for sz in 1024 16384 65536; do
        echo "--- N=$sz, -fconstexpr-steps=200000000 컴파일 시간"
        /usr/bin/time -p c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -fconstexpr-steps=200000000 -DSZ=$sz -c demo_big_table.cpp -o "$T/b.o" 2>&1 | grep -E 'real|error' || true
    done ;;
*) echo "usage: $0 {obj|rodata|o0|fold|local|elf|errors|odr|big}"; exit 2 ;;
esac
