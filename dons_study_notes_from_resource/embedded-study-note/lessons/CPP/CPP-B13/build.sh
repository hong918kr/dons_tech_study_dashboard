#!/bin/sh
# CPP-B13 build.sh — demo_flags.cpp 하나를 {예외 on/off} x {RTTI on/off} 4조합으로 빌드해
#   섹션 크기 / typeinfo 심볼 / 런타임 의존 심볼을 표로 비교한다.
#   sh build.sh       : macOS arm64 Mach-O 실행 파일 4개
#   sh build.sh arm   : + Cortex-M4 (thumbv7em-none-eabi) ELF 오브젝트 .o 로도 컴파일
#                       (arm-none-eabi-g++ 가 없어도 Apple clang 의 ARM 백엔드로 .o 까지는 만들 수 있다)
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
OUT=${OUT:-/tmp/cpp_b13_build}; mkdir -p "$OUT"
SRC=$(cd "$(dirname "$0")" && pwd)/demo_flags.cpp

msect() {  # $1=file $2=segment $3=section → bytes (없으면 0)
  size -m "$1" | awk -v g="$2" -v s="$3" '
    /^Segment/ {seg=$2; sub(":","",seg)}
    /Section/  {n=$2; sub(":","",n); if (seg==g && n==s) {v=$3}}
    END {print v+0}'
}
flags() { F=""; [ "$1" = off ] && F="$F -fno-exceptions"; [ "$2" = off ] && F="$F -fno-rtti"; echo "$F"; }

echo "--- macOS arm64 Mach-O, c++ -std=c++20 -O2 (bytes) ---"
printf '%-16s %6s %6s %8s %8s %8s %11s %6s %6s %6s\n' \
  combo __text __stubs exc_tab unw_info eh_frame DATA_const TI_def U_rt file_KB
for e in on off; do for r in on off; do
  B="$OUT/exc-$e.rtti-$r"
  c++ -std=c++20 -O2 -Wall -Wextra -Werror $(flags $e $r) "$SRC" -o "$B"
  ti=$(nm -U "$B" | c++filt | grep -c 'typeinfo for' || true)
  rt=$(nm -u "$B" | grep -cE '__cxa_|_Unwind|__gxx_personality|__dynamic_cast|__class_type_info|__si_class_type_info' || true)
  printf '%-16s %6s %6s %8s %8s %8s %11s %6s %6s %6s\n' "exc-$e/rtti-$r" \
    "$(msect "$B" __TEXT __text)" "$(msect "$B" __TEXT __stubs)" "$(msect "$B" __TEXT __gcc_except_tab)" \
    "$(msect "$B" __TEXT __unwind_info)" "$(msect "$B" __TEXT __eh_frame)" \
    "$(msect "$B" __DATA_CONST __const)" "$ti" "$rt" "$(( $(stat -f %z "$B") / 1024 ))"
done; done

[ "$1" = arm ] || exit 0

esect() {  # $1=objdump -h 출력, $2=섹션 이름 접두사 → 합계 bytes
  echo "$1" | awk -v p="$2" 'index($2,p)==1 {s+=sprintf("%d","0x"$3)} END{print s+0}'
}
echo
echo "--- Cortex-M4 ELF .o: clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os (bytes) ---"
printf '%-30s %6s %8s %8s %8s %8s %6s\n' combo .text .exidx .extab .rodata TI_bytes TI_def
arm_row() {  # $1=label, rest = extra flags
  L=$1; shift
  O="$OUT/arm.$(echo "$L" | tr '/ ' '._').o"
  clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -Os -ffreestanding \
          -fno-threadsafe-statics -DDEMO_FREESTANDING -Wall -Wextra -Werror "$@" -c "$SRC" -o "$O"
  h=$(objdump -h "$O")
  tib=$(( $(esect "$h" .rodata._ZTI) + $(esect "$h" .rodata._ZTS) ))
  ti=$(nm -U "$O" | c++filt | grep -c 'typeinfo for' || true)
  printf '%-30s %6s %8s %8s %8s %8s %6s\n' "$L" "$(esect "$h" .text)" "$(esect "$h" .ARM.exidx)" \
    "$(esect "$h" .ARM.extab)" "$(esect "$h" .rodata)" "$tib" "$ti"
}
for e in on off; do for r in on off; do arm_row "exc-$e/rtti-$r" $(flags $e $r); done; done
arm_row "exc-off/rtti-off/no-unwind" -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables
