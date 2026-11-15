#!/usr/bin/env bash
# demo_weak.sh — s4 weak 기본 핸들러: Mach-O(weak 정의) vs ELF(alias), 벡터 테이블 덤프,
#                컴파일 단계 가드, C++ 핸들러 맹글링 버그, 연습 3 의 dynamic_lookup / GNU ld 아카이브.
#   bash demo_weak.sh > demo_weak.txt 2>&1
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-B05_demo_weak
rm -rf "$D"
mkdir -p "$D"
CF="-std=c11 -Wall -Wextra -O2"
stage() { mkdir -p "$3"; cp "$L/$1/common/"* "$L/$1/$2/"* "$3/"; }
run() { printf '\n$ %s\n' "$*"; eval "$@" 2>&1; echo "[rc=$?]"; }

echo "################ MAC ################"
echo "=== alias 속성은 Darwin 에서 안 된다 ==="
cd "$D" || exit 1
printf 'void Default_Handler(void) {}\nvoid USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));\n' >alias.c
run "cc -c alias.c"
run "clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -c alias.c -o alias_elf.o && nm alias_elf.o"

echo
echo "=== s4 starter (Mach-O): 링크 성공, 인터럽트는 기본 핸들러로 ==="
for v in starter solution; do
	stage s4_weak $v "$D/s4$v"
	(cd "$D/s4$v" && for f in $(cat SRCS); do cc $CF -c "$f"; done && cc $(sed 's/\.c/.o/g' SRCS) -o app)
done
cd "$D/s4starter" || exit 1
run "cat SRCS"
run "./app"
run "nm startup.o | grep -E 'Handler'"
run "nm -m startup.o | grep -E 'Handler'"
run "nm -m app | grep -E 'IRQHandle'"
cd "$D/s4solution" || exit 1
run "./app"
run "nm -m app | grep -E 'IRQHandle'"

echo
echo "=== 컴파일 단계 가드: -Wmissing-prototypes 는 오타를 잡는다 (빠진 파일은 못 잡는다) ==="
cd "$D/s4starter" || exit 1
run "cc $CF -Wmissing-prototypes -c uart.c"

echo
echo "=== 연습 1 의 버그 버전: TIM2 핸들러를 C++ 로 옮기면서 extern \"C\" 를 빼먹음 ==="
mkdir -p "$D/p1bug"
cp "$L/s4_weak/common/"* "$L/s4_weak/solution/uart.c" "$D/p1bug/"
rm -f "$D/p1bug/timer.c"
cd "$D/p1bug" || exit 1
cat >timer.cpp <<'CPP'
// timer.cpp (버그 버전)
#include "events.h"
void TIM2_IRQHandler(void) { event_post(EV_TIM2_TICK); }
CPP
run "cat timer.cpp"
for f in startup.c events.c test_main.c uart.c; do cc $CF -c "$f"; done
c++ -std=c++17 -Wall -Wextra -O2 -c timer.cpp
run "c++ startup.o events.o test_main.o uart.o timer.o -o app && ./app"
run "nm timer.o | grep TIM2"
run "nm timer.o | grep TIM2 | c++filt"
run "c++ -std=c++17 -Wall -Wextra -Wmissing-declarations -O2 -c timer.cpp"
run "c++ -std=c++17 -Wall -Wextra -Wmissing-prototypes -O2 -c timer.cpp"

echo
echo "=== 연습 3 c6: -undefined dynamic_lookup — 링크 에러를 런타임 크래시로 미룬다 ==="
mkdir -p "$D/p3"
cp "$L/practice/p3_predict/"*.c "$D/p3/"
cd "$D/p3" || exit 1
cc $CF -c main.c
run "cc main.o -o t6"
run "cc main.o -Wl,-undefined,dynamic_lookup -o t6; echo link rc=\$?; ./t6"
run "dyld_info -fixups t6 2>/dev/null | grep -i board || true"

echo
echo "################ VM elinux ################"
limactl shell elinux -- bash -s -- "$L" <<'VM'
set -u
L=$1; D=/tmp/TOOL-B05_demo_weak; rm -rf $D; mkdir -p $D
run() { printf '\n$ %s\n' "$*"; eval "$@" 2>&1; echo "[rc=$?]"; }
stage() { mkdir -p "$3"; cp "$L/$1/common/"* "$L/$1/$2/"* "$3/"; }
arm-none-eabi-gcc --version | head -1
FW="arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -std=c11 -Wall -Wextra -ffreestanding -nostdlib -Wl,-e,Reset_Handler"
echo "=== startup.c 를 ELF 로: alias = 같은 주소의 또 다른 이름 ==="
stage s4_weak starter $D/st; cd $D/st
run "arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -std=c11 -c startup.c && arm-none-eabi-nm startup.o | grep Handler"
run "arm-none-eabi-readelf -s startup.o | grep -E 'Num|Handler'"
for v in starter solution; do
  echo; echo "=== s4 $v → fw.elf (arm-none-eabi-gcc 14.2, GNU ld) ==="
  stage s4_weak $v $D/$v; cd $D/$v
  SRC=$(sed "s/test_main.c//" SRCS)
  run "$FW $SRC -o fw.elf"
  run "arm-none-eabi-nm -n fw.elf | grep -E 'Handler|IRQHandle'"
  run "arm-none-eabi-objdump -s -j .rodata fw.elf | tail -2"
  run "gcc -std=c11 -Wall -Wextra -O2 \$(cat SRCS) -o app && ./app   # aarch64 Linux 네이티브, alias 버전"
done
echo
echo "=== strong 두 개면? (weak 는 '하나만 있으면' 조용히 진다, strong 끼리는 에러) ==="
cd $D/solution
printf '#include "irq.h"\nvoid USART1_IRQHandler(void) {}\n' > dup.c
run "$FW startup.c events.c uart.c timer.c dup.c -o dup.elf"
echo
echo "=== 연습 1 버그 버전을 g++ 로: GCC 는 -Wmissing-declarations 가 C++ 용 이름 ==="
mkdir -p $D/p1bug; cp $L/s4_weak/common/events.h $D/p1bug/; cd $D/p1bug
printf '#include "events.h"\nvoid TIM2_IRQHandler(void) { event_post(EV_TIM2_TICK); }\n' > timer.cpp
run "g++ -std=c++17 -Wall -Wextra -Wmissing-declarations -O2 -c timer.cpp"
echo
echo "=== 연습 3 의 c1~c5 를 GNU ld 로 ==="
mkdir -p $D/p3; cp $L/practice/p3_predict/*.c $D/p3/; cd $D/p3
for f in main main2 a_name b_name; do gcc -O2 -c $f.c; done
ar rcs liba.a a_name.o; ar rcs libb.a b_name.o
run "gcc main.o liba.a libb.a -o t1 && ./t1"
run "gcc main.o libb.a liba.a -o t2 && ./t2"
run "gcc main2.o liba.a libb.a -o t3 && ./t3"
run "gcc main2.o libb.a liba.a -o t4 && ./t4"
run "gcc main.o a_name.o b_name.o -o t5"
run "gcc main.o -o t6 -Wl,--unresolved-symbols=ignore-all && ./t6"
VM
