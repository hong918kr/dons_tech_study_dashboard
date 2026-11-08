#!/usr/bin/env bash
# CPP-B02 demo: C HAL(hal.c, startup.c) + C++ 앱(app.cpp) 혼합 링크 3가지 시나리오.
#   bash build.sh            # macOS(clang, Mach-O) 또는 Linux(gcc, ELF)
#   ARM=1 bash build.sh      # arm-none-eabi-gcc 가 있으면 Cortex-M4 ELF 로도 빌드 + nm
# 산출물은 임시 디렉터리(OUT)에 만든다. 레슨 폴더는 더럽히지 않는다.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$(mktemp -d)}
[ "$(uname)" = Darwin ] && export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
CFLAGS="-std=c11 -Wall -Wextra -Werror -O2"
CXXFLAGS="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
cd "$OUT" || exit 1
echo "# OUT=$OUT"

hr() { echo; echo "=== $* ==="; }

hr "1) 정상: hal.h 에 extern \"C\" 가드 있음"
cc  $CFLAGS   -c "$SRC/hal.c"     -o hal.o
cc  $CFLAGS   -c "$SRC/startup.c" -o startup.o
c++ $CXXFLAGS -I"$SRC" -c "$SRC/app.cpp" -o app.o
c++ app.o hal.o startup.o -o app_ok && ./app_ok
echo "--- nm hal.o (C 컴파일러가 만든 정의, T=text 정의)"
nm hal.o | grep ' T '
echo "--- nm app.o (U=미정의 참조: C 이름 그대로 찾는다)"
nm app.o | grep -E ' U .*hal_'

hr "2) 실수: extern \"C\" 가드 없이 C++ 로 hal.h 포함 → 링크 에러"
c++ $CXXFLAGS -DHAL_FORGET_EXTERN_C -I"$SRC" -c "$SRC/app.cpp" -o app_bad.o
echo "--- nm app_bad.o (맹글링된 이름으로 찾는다)"
nm app_bad.o | grep -E ' U .*hal_'
echo "--- c++filt 로 해석"
nm app_bad.o | grep -E ' U .*hal_' | awk '{print $2}' | c++filt
echo "--- 링크"
c++ app_bad.o hal.o startup.o -o app_bad > link_err.txt 2>&1; rc=$?
grep -v '_main in app_bad.o' link_err.txt | head -14
echo "(link exit=$rc)"

hr "3) 조용한 버그: ISR 에 extern \"C\" 를 빼먹음 → 링크 성공, Default_Handler 실행"
c++ $CXXFLAGS -DISR_FORGET_EXTERN_C -I"$SRC" -c "$SRC/app.cpp" -o app_isr.o
c++ app_isr.o hal.o startup.o -o app_isr && ./app_isr
echo "(run exit=$?)"
echo "--- 두 개의 서로 다른 심볼이 생겼다"
if [ "$(uname)" = Darwin ]; then nm -m app_isr | grep -E 'USART1_IRQHandler|Default_Handler'
else nm app_isr | grep -E 'USART1_IRQHandler|Default_Handler'; fi
nm app_isr | grep USART1_IRQHandler | awk '{print $3}' | c++filt

if [ "${ARM:-0}" = 1 ] && command -v arm-none-eabi-gcc >/dev/null; then
  A="-mcpu=cortex-m4 -mthumb -O2 -Wall -Wextra -Werror"
  hr "ARM) arm-none-eabi (Cortex-M4, ELF): uint32_t 맹글링 + weak alias"
  arm-none-eabi-gcc $A -std=c11 -c "$SRC/hal.c" -o arm_hal.o
  arm-none-eabi-gcc $A -std=c11 -c "$SRC/startup.c" -o arm_startup.o
  arm-none-eabi-g++ $A -std=c++20 -fno-exceptions -fno-rtti -DHAL_FORGET_EXTERN_C -I"$SRC" -c "$SRC/app.cpp" -o arm_app_bad.o
  echo "--- 가드 없는 경우 참조 심볼 (uint32_t 가 'm' = unsigned long)"
  arm-none-eabi-nm arm_app_bad.o | grep -E ' U .*hal_'
  for v in OK ISR; do
    def=""; [ $v = ISR ] && def="-DISR_FORGET_EXTERN_C"
    arm-none-eabi-g++ $A -std=c++20 -fno-exceptions -fno-rtti $def -I"$SRC" -c "$SRC/app.cpp" -o arm_app.o
    arm-none-eabi-g++ $A --specs=nosys.specs arm_app.o arm_hal.o arm_startup.o -o fw_$v.elf 2>/dev/null
    echo "--- fw_$v.elf : 핸들러 주소 (W = weak)"
    arm-none-eabi-nm fw_$v.elf | grep -E 'Default_Handler|USART1_IRQHandler'
  done
fi
