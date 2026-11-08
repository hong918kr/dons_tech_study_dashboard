#!/bin/sh
# CPP-B09 SIOF 데모: 링크 순서 / constinit 에 따라 결과와 __mod_init_func 이 어떻게 바뀌나
# 사용: sh build.sh        (출력은 ./out_b09/ 아래, 소스 디렉터리는 건드리지 않음)
set -u
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
F="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
O=out_b09; mkdir -p $O

for fix in 0 1 2; do
  echo "================ FIX=$fix ================"
  for f in a b main; do c++ $F -DFIX=$fix -c $f.cpp -o $O/$f.$fix.o || exit 1; done
  echo "-- link order: a.o b.o main.o"
  c++ $O/a.$fix.o $O/b.$fix.o $O/main.$fix.o -o $O/ab.$fix && ./$O/ab.$fix; echo "  exit=$?"
  echo "-- link order: b.o a.o main.o"
  c++ $O/b.$fix.o $O/a.$fix.o $O/main.$fix.o -o $O/ba.$fix && ./$O/ba.$fix; echo "  exit=$?"
  echo "-- sections with init functions (Mach-O):"
  for f in a b; do printf '  %s.o: ' $f; objdump -h $O/$f.$fix.o | grep -E '__mod_init_func' || echo '(no __mod_init_func)'; done
  printf '  final binary: '; objdump -h $O/ab.$fix | grep -E '__mod_init_func|__init_offsets' || echo '(no __mod_init_func / __init_offsets)'
done

echo "================ ELF / Cortex-M4 (Thumb-2) object files, compile only ================"
T="--target=armv7em-none-eabi -mthumb -mcpu=cortex-m4 -ffreestanding"
for fix in 0 1; do
  clang++ $T $F -DFIX=$fix -c b.cpp -o $O/b.$fix.elf.o || exit 1
  echo "-- b.cpp FIX=$fix (ELF sections)"; objdump -h $O/b.$fix.elf.o | grep -E '\.text|\.data|\.bss|\.init_array'
done
echo "-- FIX=0 .init_array relocations + ctor thunk"
objdump -r $O/b.0.elf.o | sed -n '/RELOCATION RECORDS FOR \[.init_array\]/,/^$/p'
objdump -d -r --no-show-raw-insn $O/b.0.elf.o | sed -n '/_GLOBAL__sub_I/,/^$/p'
echo "-- FIX=1 .data contents (g_clock = {16000000, 0})"
objdump -s -j .data $O/b.1.elf.o | tail -n 1
