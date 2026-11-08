#!/bin/sh
# C-B11 데모: 같은 64 KB 배열을 init / zero / const 로 바꿔가며 섹션 크기 측정
#   sh size_demo.sh        (macOS Mach-O 실행 파일 + Cortex-M ELF 오브젝트 둘 다)
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
OUT=${TMPDIR:-/tmp}/C-B11_size; mkdir -p "$OUT"
for v in 0 1 2 3; do
  cc -std=c11 -O2 -DVARIANT=$v size_demo.c -o "$OUT/v$v"
  echo "=== VARIANT=$v  (macOS)  file size: $(wc -c < "$OUT/v$v" | tr -d ' ') bytes"
  size -m "$OUT/v$v" | grep -E 'Section __(text|const|data|bss|common):'
done
LLVM_SIZE=$(command -v llvm-size || command -v arm-none-eabi-size)
for v in 0 1 2 3; do
  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -DVARIANT=$v -c size_demo.c -o "$OUT/v$v.o"
  echo "=== VARIANT=$v  (Cortex-M4 ELF .o)"
  "$LLVM_SIZE" -A "$OUT/v$v.o" | grep -E '^\.(text|rodata|data|bss)'
done
