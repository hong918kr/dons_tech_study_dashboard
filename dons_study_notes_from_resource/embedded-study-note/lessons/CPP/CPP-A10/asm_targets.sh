#!/bin/bash
# CPP-A10 asm_targets.sh — 같은 fw_main.cpp 를 맥의 Apple clang 으로 Cortex-M4 오브젝트로 (recipe b). 맥에서 실행.
#   bash asm_targets.sh size   함수별 .text 크기 + .ARM.exidx (unwind 표) 유무, -fno-unwind-tables 비교
#   bash asm_targets.sh asm    LowPass<8>::step 디스어셈블 (smlal / ssat 확인)
#   bash asm_targets.sh crc    demo_crc_size.cpp: CRC-32 바이트 표 / 니블 표 / 비트 루프의 .text·.rodata (연습 1)
# clang 은 thumb 코드를 만들지만 bare-metal C++ 헤더가 없어서 arm-none-eabi 의 libstdc++/newlib 헤더를 쓴다 (CPP-A04 와 같은 방식).
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
OUT=${TMPDIR:-/tmp}/cpp_a10_asm; mkdir -p "$OUT"
cxx() {
    clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=soft -std=c++20 -Wall -Wextra -Werror -Os \
        -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -ffreestanding -nostdinc++ \
        -isystem "$ARM_INC/c++/14.2.1" -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m/nofp" -isystem "$ARM_INC" "$@"
}
sum() { xcrun llvm-size -A "$1" | awk -v p="$2" 'index($1, p) == 1 { s += $2; n++ } END { printf "%-14s %5d B in %d sections\n", p, s, n }'; }
case "${1:-size}" in
size)
    cxx -c fw_main.cpp -o "$OUT/fw.o"
    echo "== clang $(clang++ --version | head -1 | sed 's/.*version //')  -Os  (오브젝트, 링크 전)"
    sum "$OUT/fw.o" .text; sum "$OUT/fw.o" .ARM.exidx; sum "$OUT/fw.o" .rodata; sum "$OUT/fw.o" .data; sum "$OUT/fw.o" .bss
    echo "== 함수별 (llvm-nm --size-sort, 10진수)"
    xcrun llvm-nm -C --size-sort -S --radix=d "$OUT/fw.o" | awk '$3 ~ /[TtWw]/ { n = ""; for (i = 4; i <= NF; i++) n = n " " $i; printf "%6d%s\n", $2, substr(n, 1, 110) }'
    echo "== 외부 의존 (U)"; xcrun llvm-nm -u "$OUT/fw.o"
    cxx -fno-unwind-tables -fno-asynchronous-unwind-tables -c fw_main.cpp -o "$OUT/fw_nouw.o"
    echo "== -fno-unwind-tables -fno-asynchronous-unwind-tables"; sum "$OUT/fw_nouw.o" .text; sum "$OUT/fw_nouw.o" .ARM.exidx ;;
asm)
    cxx -c fw_main.cpp -o "$OUT/fw.o"
    xcrun llvm-objdump -d --no-show-raw-insn -C "$OUT/fw.o" | sed -n '/<a10::dsp::LowPass<8>::step(short)>:/,/^$/p' ;;
crc)
    clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -Werror -Os -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections -c demo_crc_size.cpp -o "$OUT/crc.o"
    xcrun llvm-size -A "$OUT/crc.o" | grep -E '^\.(text|rodata)\.'
    for O in -O2 -Os; do
        clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -std=c++20 $O -fno-exceptions -fno-rtti -S demo_crc_size.cpp -o - |
            awk -v o=$O '/^crc_[a-z]+:/ { f = $1 } f && /^\t[a-z]/ { n[f]++ } /^\.Lfunc_end/ { f = "" } END { for (k in n) printf "%s %-14s %3d instructions\n", o, k, n[k] }' | sort
    done ;;
esac
