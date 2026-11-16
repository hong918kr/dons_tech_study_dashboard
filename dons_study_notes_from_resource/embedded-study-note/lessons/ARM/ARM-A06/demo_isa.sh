#!/bin/bash
# ARM-A06 demo: 어떤 타깃에 CRC32 / PMULL 명령이 있나 — 어셈블러(LLVM)에 직접 물어본다 (맥에서 실행, 오브젝트만 만들고 버림)
# 실행: bash demo_isa.sh
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
try() {
    printf '%-56s ' "$*"
    out=$(printf '%s\n' "$ASM" | clang "$@" -x assembler -c - -o /dev/null 2>&1 | grep -m1 error | sed 's/.*error: //')
    [ -z "$out" ] && echo OK || echo "ERR  $out"
}
echo "--- AArch32 / M-profile: crc32b r0, r0, r1"
ASM='crc32b r0, r0, r1'
try --target=armv7a-none-eabi -marm
try --target=armv8a-none-eabi -marm
try --target=armv8a-none-eabi -mthumb
try --target=armv8r-none-eabi -mcpu=cortex-r52
try --target=thumbv7em-none-eabi -mcpu=cortex-m7
try --target=thumbv8m.main-none-eabi -mcpu=cortex-m33
try --target=thumbv8.1m.main-none-eabi -mcpu=cortex-m55
try --target=thumbv8.1m.main-none-eabi -mcpu=cortex-m85
echo "--- AArch64: crc32cx w0, w0, x1"
ASM='crc32cx w0, w0, x1'
try --target=aarch64-none-elf -march=armv8-a
try --target=aarch64-none-elf -march=armv8-a+crc
try --target=aarch64-none-elf -march=armv8.1-a
try --target=aarch64-none-elf -mcpu=cortex-a53
try --target=aarch64-none-elf -mcpu=cortex-r82
echo "--- AArch64: pmull v0.1q, v0.1d, v1.1d (64x64 다항식 곱 = FEAT_PMULL, AES 확장에 묶임)"
ASM='pmull v0.1q, v0.1d, v1.1d'
try --target=aarch64-none-elf -march=armv8.1-a
try --target=aarch64-none-elf -march=armv8-a+aes
echo "--- 이 맥 (Apple clang 기본 타깃) 의 기능 매크로"
echo | cc -dM -E - | grep -E '__ARM_FEATURE_(CRC32|AES|CRYPTO)'
sysctl hw.optional.arm.FEAT_CRC32 hw.optional.arm.FEAT_PMULL
