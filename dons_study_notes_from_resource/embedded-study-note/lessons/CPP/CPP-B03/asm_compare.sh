#!/bin/sh
# CPP-B03: C enum 버전과 enum class 버전의 -O2 어셈블리를 명령어 단위로 비교한다.
#   sh asm_compare.sh   → ① 호스트 arm64 (Mach-O)  ② Cortex-M4 Thumb-2 (clang --target=thumbv7em-none-eabi)
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
OUT=${TMPDIR:-/tmp}/cppb03_asm; mkdir -p "$OUT"
# 지시어(.section/.globl/...)·주석·빈 줄을 지우고 라벨+명령어만 남긴다
clean() { grep -v -E '^[[:space:]]*(\.|;|@|//|$)' "$1" | sed -E 's/[[:space:]]*(;|@).*$//'; }
compare() {  # $1=이름 $2=추가 플래그
    cc  $2 -std=c11   -O2 -S -fno-asynchronous-unwind-tables uart_c.c   -o "$OUT/$1_c.s"
    c++ $2 -std=c++20 -O2 -S -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti uart_cpp.cpp -o "$OUT/$1_cpp.s"
    clean "$OUT/$1_c.s" > "$OUT/$1_c.txt"; clean "$OUT/$1_cpp.s" > "$OUT/$1_cpp.txt"
    echo "== [$1] C (enum + #define) =="; cat "$OUT/$1_c.txt"
    echo "== [$1] diff C vs C++ (enum class) =="
    if diff "$OUT/$1_c.txt" "$OUT/$1_cpp.txt"; then echo "IDENTICAL: $(wc -l < "$OUT/$1_c.txt" | tr -d ' ') lines"; fi
}
compare arm64 ""
compare cm4 "--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -nostdinc -isystem $(cc -print-resource-dir)/include"
