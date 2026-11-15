#!/usr/bin/env bash
# TOOL-B06 demo: fw.c 변형별 섹션 크기 측정.
#   Mac  : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os -c  → llvm-size -A / Berkeley  (.o 만)
#          + 호스트(arm64 Mach-O) 빌드 → size -m  (대조용)
#   VM   : arm-none-eabi-gcc 14 + newlib-nano 로 startup.c + fw.c + fw.ld 완전 링크 → arm-none-eabi-size
# 결과는 이 디렉터리의 size_*.txt 로 저장된다 (레슨의 starter/solution 테스트가 이 텍스트를 그대로 품고 있다).
set -u
cd "$(dirname "$0")"
HERE=$PWD
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
LSIZE=/Library/Developer/CommandLineTools/usr/bin/llvm-size     # /usr/local/bin/llvm-size 는 LLVM 3.2 — 쓰지 말 것
W=/tmp/TOOL-B06_work/demo
rm -rf "$W"; mkdir -p "$W"

# 이름 | -D 플래그
VARIANTS="
uninit|-DTBL_UNINIT
zero|-DTBL_ZERO
init|-DTBL_INIT
const|-DTBL_CONST
strings|-DTBL_CONST -DWITH_STRINGS
strptrs|-DTBL_CONST -DWITH_STRINGS -DSTR_PTRS_NONCONST
printf|-DTBL_CONST -DWITH_STRINGS -DWITH_PRINTF
"

echo "===== Mac: clang thumbv7em-none-eabi -mcpu=cortex-m4 -Os -c (object only) ====="
MCF="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=soft -Os -ffreestanding -Wall -Wextra -Werror"
printf '%-8s %6s %6s %6s   (llvm-size Berkeley on fw.o)\n' variant text data bss
echo "$VARIANTS" | while IFS='|' read -r name flags; do
    [ -n "$name" ] || continue
    if [ "$name" = printf ]; then inc="-isystem $W/fakeinc"; mkdir -p "$W/fakeinc"
        # bare 타깃이라 Mac 에는 stdio.h 가 없다 → printf 선언만 있는 가짜 헤더 (링크는 VM 에서)
        printf 'int printf(const char *, ...);\n' > "$W/fakeinc/stdio.h"
    else inc=""; fi
    # shellcheck disable=SC2086
    clang $MCF $inc $flags -c fw.c -o "$W/mac_$name.o" || { echo "BUILD FAIL $name"; continue; }
    { echo "\$ llvm-size -A fw.o    # $flags"; "$LSIZE" -A "$W/mac_$name.o" | sed "s#$W/mac_$name.o#fw.o#";
      echo "\$ llvm-size fw.o"; "$LSIZE" "$W/mac_$name.o" | sed "s#$W/mac_$name.o#fw.o#"; } > "size_mac_$name.txt"
    "$LSIZE" "$W/mac_$name.o" | awk -v n="$name" 'NR==2{printf "%-8s %6s %6s %6s\n", n, $1, $2, $3}'
done
# -fcommon: 초기값 없는 전역이 COMMON 심볼이 되면 .o 의 size 에는 안 잡힌다
# shellcheck disable=SC2086
clang $MCF -DTBL_UNINIT -fcommon -c fw.c -o "$W/mac_common.o"
{ echo "\$ llvm-size -A fw.o    # -DTBL_UNINIT -fcommon"; "$LSIZE" -A "$W/mac_common.o" | sed "s#$W/mac_common.o#fw.o#";
  echo "\$ llvm-size fw.o"; "$LSIZE" "$W/mac_common.o" | sed "s#$W/mac_common.o#fw.o#";
  echo "\$ llvm-nm fw.o | grep table"; /Library/Developer/CommandLineTools/usr/bin/llvm-nm "$W/mac_common.o" | grep table; } > size_mac_common.txt
"$LSIZE" "$W/mac_common.o" | awk 'NR==2{printf "%-8s %6s %6s %6s   <- -fcommon\n", "common", $1, $2, $3}'

echo
echo "===== Mac host (arm64 Mach-O) — size -m, 대조용 ====="
cc -Os -DTBL_INIT -c fw.c -o "$W/host.o"
cc -Os -DTBL_INIT fw.c -o "$W/host_fw"
{ echo "\$ size -m fw.o        # cc -Os -DTBL_INIT -c (arm64 Mach-O)"; size -m "$W/host.o" | sed "s#$W/host.o#fw.o#";
  echo "\$ size fw.o"; size "$W/host.o" | sed "s#$W/host.o#fw.o#";
  echo "\$ size -m host_fw     # linked executable"; size -m "$W/host_fw" | sed "s#$W/host_fw#host_fw#";
  echo "\$ size host_fw"; size "$W/host_fw" | sed "s#$W/host_fw#host_fw#"; } > size_mac_host.txt
cat size_mac_host.txt

echo
echo "===== VM elinux: arm-none-eabi-gcc full link (nano.specs + nosys.specs, fw.ld) ====="
VW=/tmp/TOOL-B06_work/vm
# Mac 홈은 VM 에서 읽기 전용 → VM /tmp 로 복사해서 빌드, 결과 텍스트는 stdout 으로 받아 Mac 에서 저장
limactl shell elinux -- bash -lc "rm -rf $VW && mkdir -p $VW && cp '$HERE/fw.c' '$HERE/startup.c' '$HERE/fw.ld' $VW/" || exit 1
VM_SCRIPT='
set -u
cd '"$VW"'
CF="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -Wall -Wextra -Werror"
LF="-T fw.ld -nostartfiles"
build() {   # name, specs, extra, -D flags...
    local name=$1 specs=$2 extra=$3; shift 3
    arm-none-eabi-gcc $CF "$@" startup.c fw.c $LF $specs $extra -o $name.elf 2> $name.ldwarn || { echo "BUILD FAIL $name"; cat $name.ldwarn; return; }
    { echo "\$ arm-none-eabi-size fw.elf    # $* $specs $extra"; arm-none-eabi-size $name.elf | sed "s#$name.elf#fw.elf#";
      echo "\$ arm-none-eabi-size -A fw.elf"; arm-none-eabi-size -A $name.elf | sed "s#$name.elf#fw.elf#"; } > size_vm_$name.txt
}
NANO="--specs=nano.specs --specs=nosys.specs"
build uninit  "$NANO" "" -DTBL_UNINIT
build zero    "$NANO" "" -DTBL_ZERO
build init    "$NANO" "" -DTBL_INIT
build const   "$NANO" "" -DTBL_CONST
build strings "$NANO" "" -DTBL_CONST -DWITH_STRINGS
build strptrs "$NANO" "" -DTBL_CONST -DWITH_STRINGS -DSTR_PTRS_NONCONST
build printf  "$NANO" "" -DTBL_CONST -DWITH_STRINGS -DWITH_PRINTF
build printf_float "$NANO" "-u _printf_float" -DTBL_CONST -DWITH_STRINGS -DWITH_PRINTF
build printf_full "--specs=nosys.specs" "" -DTBL_CONST -DWITH_STRINGS -DWITH_PRINTF
# 같은 init 빌드를 -x(16진) 로 + .data 의 VMA/LMA
{ echo "\$ arm-none-eabi-size -A -x fw.elf    # -DTBL_INIT"; arm-none-eabi-size -A -x init.elf | sed "s#init.elf#fw.elf#"; } > size_vm_init_hex.txt
{ echo "\$ arm-none-eabi-objdump -h fw.elf    # -DTBL_INIT"; arm-none-eabi-objdump -h init.elf | sed "s#init.elf#fw.elf#" | sed -n "2,\$p"; } > size_vm_init_objdump.txt
# 여러 .o 를 한 번에 + -t (합계 줄)
arm-none-eabi-gcc $CF -DTBL_INIT -c startup.c -o startup.o; arm-none-eabi-gcc $CF -DTBL_INIT -c fw.c -o fw.o
{ echo "\$ arm-none-eabi-size -t startup.o fw.o"; arm-none-eabi-size -t startup.o fw.o; } > size_vm_objs_total.txt
# printf 가 끌고 온 가장 큰 코드 심볼들 (nm 크기순, T/t 만, 상위 8개)
for n in printf printf_full; do { echo "\$ arm-none-eabi-nm --size-sort -S fw.elf | grep \" [Tt] \" | tail -8    # $n"; arm-none-eabi-nm --size-sort -S $n.elf | grep " [Tt] " | tail -8; }; done > size_vm_printf_top.txt
# printf 빌드가 링커에서 낸 경고 (nosys 스텁) 한 줄 예시
head -2 printf.ldwarn | sed "s#^.*/bin/ld: #ld: #; s#^/build/newlib-[^/]*/#.../#" > printf_ldwarn_head.txt
for f in size_vm_*.txt printf_ldwarn_head.txt; do echo "@@FILE $f"; cat "$f"; done
'
limactl shell elinux -- bash -lc "$VM_SCRIPT" > "$W/vm_out.txt" 2>&1
grep -q '^@@FILE' "$W/vm_out.txt" || { echo "VM part failed:"; cat "$W/vm_out.txt"; exit 1; }
grep '^BUILD FAIL' "$W/vm_out.txt"
# @@FILE 구분자로 쪼개서 이 디렉터리에 저장
awk -v dir="$HERE" '/^@@FILE /{f=dir "/" $2; printf "" > f; next} f{print >> f}' "$W/vm_out.txt"
mv "$HERE/printf_ldwarn_head.txt" "$HERE/size_vm_printf_ldwarn.txt"

printf '%-13s %6s %6s %6s | %6s %6s\n' variant text data bss Flash RAM
for n in uninit zero init const strings strptrs printf printf_float printf_full; do
    awk -v n="$n" '/^ *text/{getline; t=$1; d=$2; b=$3; printf "%-13s %6d %6d %6d | %6d %6d\n", n, t, d, b, t+d, d+b; exit}' "size_vm_$n.txt"
done | tee "$W/summary.txt"
{ echo "# VM arm-none-eabi-gcc 14.2.1, -mcpu=cortex-m4 -mthumb -Os, fw.ld (FLASH 64K / RAM 16K)"
  echo "# Flash = text + data, RAM = data + bss  (bss 에는 힙+스택 예약 ._user_heap_stack 1536~1540 B 포함 — 8 바이트 정렬 패딩 차이)"
  printf '%-13s %6s %6s %6s | %6s %6s\n' variant text data bss Flash RAM; cat "$W/summary.txt"; } > size_summary.txt
echo "saved: $(ls size_*.txt | tr '\n' ' ')"
